/*
 * Copyright (C) 2016 Chino-e Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#define pr_fmt(fmt)	"[cne-charge][charger] %s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include "charger_type.h"
#include "cne_charger_intf.h"
#include "cne_charger_init.h"
#include "cne_intf.h"
#include "cne_lib.h"
#include "cne_hvdcp.h"

static struct charger_manager *pinfo;
static struct list_head consumer_head = LIST_HEAD_INIT(consumer_head);
static DEFINE_MUTEX(consumer_mutex);
struct wakeup_source *plug_wakelock = NULL;

bool is_power_path_supported(void)
{
	if (pinfo == NULL)
		return false;

	if (pinfo->data.power_path_support == true)
		return true;

	return false;
}

bool is_disable_charger(void)
{
	if (pinfo == NULL)
		return true;

	if (pinfo->disable_charger == true)
		return true;
	else
		return false;
}

void BATTERY_SetUSBState(int usb_state_value)
{
	if (is_disable_charger()) {
		chr_err("[%s] in FPGA/EVB, no service\n", __func__);
	} else {
		if ((usb_state_value < USB_SUSPEND) ||
			((usb_state_value > USB_CONFIGURED))) {
			chr_err("%s Fail! Restore to default value\n",
				__func__);
			usb_state_value = USB_UNCONFIGURED;
		} else {
			chr_err("%s Success! Set %d\n", __func__,
				usb_state_value);
			if (pinfo)
				pinfo->usb_state = usb_state_value;
		}
	}
}

unsigned int set_chr_input_current_limit(int current_limit)
{
	return 500;
}

int get_chr_temperature(int *min_temp, int *max_temp)
{
	*min_temp = 25;
	*max_temp = 30;

	return 0;
}

int set_chr_boost_current_limit(unsigned int current_limit)
{
	return 0;
}

int set_chr_enable_otg(unsigned int enable)
{
	return 0;
}

int cne_chr_is_charger_exist(unsigned char *exist)
{
	if (get_charger_type(pinfo) == CHARGER_UNKNOWN)
		*exist = 0;
	else
		*exist = 1;
	return 0;
}

/*=============== fix me==================*/
// int chargerlog_level = CHRLOG_ERROR_LEVEL;
int chargerlog_level = CHRLOG_DEBUG_LEVEL;

int chr_get_debug_level(void)
{
	return chargerlog_level;
}

void _wake_up_charger(struct charger_manager *info)
{
	unsigned long flags;

	if (info == NULL)
		return;

	spin_lock_irqsave(&info->slock, flags);
	if (!info->charger_wakelock->active)
		__pm_stay_awake(info->charger_wakelock);
	spin_unlock_irqrestore(&info->slock, flags);
	info->charger_thread_timeout = true;
	wake_up(&info->wait_que);
}

/* charger_manager ops  */
static int _cne_charger_change_current_setting(struct charger_manager *info)
{
	if (info != NULL && info->change_current_setting)
		return info->change_current_setting(info);

	return 0;
}

static int _cne_charger_do_charging(struct charger_manager *info, bool en)
{
	if (info != NULL && info->do_charging)
		info->do_charging(info, en);
	return 0;
}
/* charger_manager ops end */


/* user interface */
struct charger_consumer *charger_manager_get_by_name(struct device *dev,
	const char *name)
{
	struct charger_consumer *puser;

	puser = kzalloc(sizeof(struct charger_consumer), GFP_KERNEL);
	if (puser == NULL)
		return NULL;

	mutex_lock(&consumer_mutex);
	puser->dev = dev;

	list_add(&puser->list, &consumer_head);
	if (pinfo != NULL)
		puser->cm = pinfo;

	mutex_unlock(&consumer_mutex);

	return puser;
}
// EXPORT_SYMBOL(charger_manager_get_by_name);

int charger_manager_enable_high_voltage_charging(
			struct charger_consumer *consumer, bool en)
{
	struct list_head *pos = NULL;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr = NULL;
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (!info)
		return -EINVAL;

	pr_debug("%s, %d\n", dev_name(consumer->dev), en);

	if (!en && consumer->hv_charging_disabled == false)
		consumer->hv_charging_disabled = true;
	else if (en && consumer->hv_charging_disabled == true)
		consumer->hv_charging_disabled = false;
	else {
		pr_info("already set: %d %d\n",
			consumer->hv_charging_disabled, en);
		return 0;
	}

	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		if (ptr->hv_charging_disabled == true) {
			info->enable_hv_charging = false;
			break;
		}
		if (list_is_last(pos, phead))
			info->enable_hv_charging = true;
	}
	mutex_unlock(&consumer_mutex);

	pr_info("user: %s, en = %d\n", dev_name(consumer->dev),
		info->enable_hv_charging);

	_wake_up_charger(info);

	return 0;
}
EXPORT_SYMBOL(charger_manager_enable_high_voltage_charging);

int charger_manager_enable_power_path(struct charger_consumer *consumer,
	int idx, bool en)
{
	int ret = 0;
	bool is_en = true;
	struct charger_device *chg_dev = NULL;
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (!info)
		return -EINVAL;

	switch (idx) {
	case MAIN_CHARGER:
		if (!info->chg1_dev)
			info->chg1_dev = get_charger_by_name("primary_chg");
		chg_dev = info->chg1_dev;
		break;
	case SLAVE_CHARGER:
		if (!info->chg2_dev)
			info->chg2_dev = get_charger_by_name("slave_chg");
		chg_dev = info->chg2_dev;
		break;
	default:
		return -EINVAL;
	}

	ret = charger_dev_is_powerpath_enabled(chg_dev, &is_en);
	if (ret < 0) {
		chr_err("%s: get is power path enabled failed\n", __func__);
		return ret;
	}
	if (is_en == en) {
		chr_err("%s: power path is already en = %d\n", __func__, is_en);
		return 0;
	}

	pr_info("enable power path = %d\n", en);
	return charger_dev_enable_powerpath(chg_dev, en);
}

static int _charger_manager_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	chr_err("%s: dev:%s idx:%d en:%d\n", __func__, dev_name(consumer->dev),
		idx, en);

	if (info != NULL) {
		struct charger_data *pdata = NULL;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		if (en == false) {
			_cne_charger_do_charging(info, en);
			pdata->disable_charging_count++;
		} else {
			if (pdata->disable_charging_count == 1) {
				_cne_charger_do_charging(info, en);
				pdata->disable_charging_count = 0;
			} else if (pdata->disable_charging_count > 1)
				pdata->disable_charging_count--;
		}
		chr_err("%s: dev:%s idx:%d en:%d cnt:%d\n", __func__,
			dev_name(consumer->dev), idx, en,
			pdata->disable_charging_count);

		return 0;
	}
	return -EBUSY;

}

int charger_manager_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	int ret = 0;
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (!info)
		return -EINVAL;
	mutex_lock(&info->charger_lock);
	ret = _charger_manager_enable_charging(consumer, idx, en);
	mutex_unlock(&info->charger_lock);
	return ret;
}

int charger_manager_set_input_current_limit(struct charger_consumer *consumer,
	int idx, int input_current)
{
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (info != NULL) {
		struct charger_data *pdata;

		if (info->data.parallel_vbus) {
			if (idx == TOTAL_CHARGER) {
				info->chg1_data.user_input_current_limit =
					input_current;
				info->chg2_data.user_input_current_limit =
					input_current;
			} else
				return -ENOTSUPP;
		} else {
			if (idx == MAIN_CHARGER)
				pdata = &info->chg1_data;
			else if (idx == SLAVE_CHARGER)
				pdata = &info->chg2_data;
			else
				return -ENOTSUPP;
			pdata->user_input_current_limit = input_current;
		}

		chr_err("%s: dev:%s idx:%d en:%d\n", __func__,
			dev_name(consumer->dev), idx, input_current);
		_cne_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_set_charging_current_limit(
	struct charger_consumer *consumer, int idx, int charging_current)
{
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		pdata->user_charging_current_limit = charging_current;
		chr_err("%s: dev:%s idx:%d en:%d\n", __func__,
			dev_name(consumer->dev), idx, charging_current);
		_cne_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_get_input_current_limit(struct charger_consumer *consumer,
	int idx, int* input_current)
{
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			pdata = &info->chg1_data;

		*input_current = pdata->input_current_limit;

		return 0;
	}

	return -EBUSY;
}

int charger_manager_get_charging_current_limit(
	struct charger_consumer *consumer, int idx, int* charging_current)
{
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			pdata = &info->chg1_data;

		*charging_current = pdata->charging_current_limit;

		return 0;
	}

	return -EBUSY;
}

int charger_manager_get_charger_temperature(struct charger_consumer *consumer,
	int idx, int *tchg_min,	int *tchg_max)
{
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (info != NULL) {
		struct charger_data *pdata = NULL;

#if 0
		// todo, charger type detect status
		if (!upmu_get_rgs_chrdet()) {
			pr_debug("[%s] No cable in, skip it\n", __func__);
			*tchg_min = -127;
			*tchg_max = -127;
			return -EINVAL;
		}
#endif
		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		*tchg_min = pdata->junction_temp_min;
		*tchg_max = pdata->junction_temp_max;

		return 0;
	}
	return -EBUSY;
}

int charger_manager_force_charging_current(struct charger_consumer *consumer,
	int idx, int charging_current)
{
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (info != NULL) {
		struct charger_data *pdata = NULL;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		pdata->force_charging_current = charging_current;
		_cne_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_enable_chg_type_det(struct charger_consumer *consumer,
	bool en)
{
	struct charger_manager *info = pinfo; //consumer->cm;
	struct charger_device *chg_dev = NULL;
	int ret = 0;

	if (info != NULL) {
		switch (info->data.bc12_charger) {
		case MAIN_CHARGER:
			if (!info->chg1_dev)
				info->chg1_dev = get_charger_by_name("primary_chg");
			chg_dev = info->chg1_dev;
			break;
		case SLAVE_CHARGER:
			if (!info->chg2_dev)
				info->chg2_dev = get_charger_by_name("slave_chg");
			chg_dev = info->chg2_dev;
			break;
		default:
			if (!info->chg1_dev)
				info->chg1_dev = get_charger_by_name("primary_chg");
			chg_dev = info->chg1_dev;
			chr_err("%s: invalid number, use main charger as default\n",
				__func__);
			break;
		}

		chr_err("%s: chg%d is doing bc12\n", __func__,
			info->data.bc12_charger + 1);
		ret = charger_dev_enable_chg_type_det(chg_dev, en);
		if (ret < 0) {
			chr_err("%s: en chgdet fail, en = %d\n", __func__, en);
			return ret;
		}
	} else
		chr_err("%s: charger_manager is null\n", __func__);

	return 0;
}
EXPORT_SYMBOL(charger_manager_enable_chg_type_det);

int charger_manager_ignore_chg_type_det(struct charger_consumer *consumer,
	bool en)
{
	struct charger_manager *info = pinfo; //consumer->cm;
	struct charger_device *chg_dev = NULL;
	int ret = 0;

	if (info != NULL) {
		switch (info->data.bc12_charger) {
		case MAIN_CHARGER:
			if (!info->chg1_dev)
				info->chg1_dev = get_charger_by_name("primary_chg");
			chg_dev = info->chg1_dev;
			break;
		case SLAVE_CHARGER:
			if (!info->chg2_dev)
				info->chg2_dev = get_charger_by_name("slave_chg");
			chg_dev = info->chg2_dev;
			break;
		default:
			if (!info->chg1_dev)
				info->chg1_dev = get_charger_by_name("primary_chg");
			chg_dev = info->chg1_dev;
			chr_err("%s: invalid number, use main charger as default\n",
				__func__);
			break;
		}

		chr_err("%s: ignore chg%d bc12\n", __func__,
			info->data.bc12_charger + 1);
		ret = charger_dev_ignore_chg_type_det(chg_dev, en);
		if (ret < 0) {
			chr_err("%s: ignore chgdet fail, en = %d\n", __func__, en);
			return ret;
		}
	} else
		chr_err("%s: charger_manager is null\n", __func__);

	return 0;
}
EXPORT_SYMBOL(charger_manager_ignore_chg_type_det);

int charger_manager_enable_hvdcp_det(struct charger_consumer *consumer,
	bool en)
{
	struct charger_manager *info = pinfo; //consumer->cm;
	struct charger_device *chg_dev = NULL;
	int ret = 0;

	if (!info) {
		chr_err("%s: charger_manager is null\n", __func__);
		return -EINVAL;
	}

	switch (info->data.bc12_charger) {
		case MAIN_CHARGER:
			if (!info->chg1_dev)
				info->chg1_dev = get_charger_by_name("primary_chg");
			chg_dev = info->chg1_dev;
			break;
		case SLAVE_CHARGER:
			if (!info->chg2_dev)
				info->chg2_dev = get_charger_by_name("slave_chg");
			chg_dev = info->chg2_dev;
			break;
		default:
			if (!info->chg1_dev)
				info->chg1_dev = get_charger_by_name("primary_chg");
			chg_dev = info->chg1_dev;
			chr_err("%s: invalid number, use main charger as default\n",
				__func__);
			break;
	}

	chr_err("%s: chg%d is doing hvdcp check\n", __func__,
		info->data.bc12_charger + 1);
	ret = charger_dev_enable_hvdcp_det(chg_dev, en);
	if (ret < 0) {
		chr_err("%s: en hvdcp fail, en = %d\n", __func__, en);
		return ret;
	}

	return 0;
}
// EXPORT_SYMBOL(charger_manager_enable_hvdcp_det);

int charger_manager_get_charging_done(struct charger_consumer *consumer,
	int idx, int *chg_done)
{
	int ret = 0;
	struct charger_device *chg_dev = NULL;
	struct charger_manager *info = NULL;
	bool done = false;

	if (!consumer)
		return -EINVAL;

	info = consumer->cm;
	if (!info)
		return -EINVAL;

	switch (idx) {
	case MAIN_CHARGER:
		if (!info->chg1_dev)
			info->chg1_dev = get_charger_by_name("primary_chg");
		chg_dev = info->chg1_dev;
		break;
	case SLAVE_CHARGER:
		if (!info->chg2_dev)
			info->chg2_dev = get_charger_by_name("slave_chg");
		chg_dev = info->chg2_dev;
		break;
	default:
		return -EINVAL;
	}

	ret = charger_dev_is_charging_done(chg_dev, &done);
	if (ret < 0) {
		chr_err("%s: get is power path enabled failed\n", __func__);
		return ret;
	}
	*chg_done = done;

	return done;
}

int register_charger_manager_notifier(struct charger_consumer *consumer,
	struct notifier_block *nb)
{
	int ret = 0;
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;
	info = consumer->cm;

	if (!info)
		return -EINVAL;

	mutex_lock(&consumer_mutex);
	if (info != NULL)
		ret = srcu_notifier_chain_register(&info->evt_nh, nb);
	else
		consumer->pnb = nb;
	mutex_unlock(&consumer_mutex);

	return ret;
}

int unregister_charger_manager_notifier(struct charger_consumer *consumer,
				struct notifier_block *nb)
{
	int ret = 0;
	struct charger_manager *info = NULL;

	if (!consumer)
		return -EINVAL;
	info = consumer->cm;

	if (!info)
		return -EINVAL;

	mutex_lock(&consumer_mutex);
	if (info != NULL)
		ret = srcu_notifier_chain_unregister(&info->evt_nh, nb);
	else
		consumer->pnb = NULL;
	mutex_unlock(&consumer_mutex);

	return ret;
}

/* user interface end*/

/* factory mode */
#define CHARGER_DEVNAME "charger_ftm"
#define GET_IS_SLAVE_CHARGER_EXIST _IOW('k', 13, int)

static struct class *charger_class;
static struct cdev *charger_cdev;
static int charger_major;
static dev_t charger_devno;

static int is_slave_charger_exist(void)
{
	if (get_charger_by_name("secondary_chg") == NULL)
		return 0;
	return 1;
}

static long charger_ftm_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;
	int out_data = 0;
	void __user *user_data = (void __user *)arg;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		out_data = is_slave_charger_exist();
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		chr_err("[%s] SLAVE_CHARGER_EXIST: %d\n", __func__, out_data);
		break;
	default:
		chr_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#ifdef CONFIG_COMPAT
static long charger_ftm_compat_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);
		break;
	default:
		chr_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#endif
static int charger_ftm_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int charger_ftm_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations charger_ftm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = charger_ftm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = charger_ftm_compat_ioctl,
#endif
	.open = charger_ftm_open,
	.release = charger_ftm_release,
};

void charger_ftm_init(void)
{
	struct class_device *class_dev = NULL;
	int ret = 0;

	ret = alloc_chrdev_region(&charger_devno, 0, 1, CHARGER_DEVNAME);
	if (ret < 0) {
		chr_err("[%s]Can't get major num for charger_ftm\n", __func__);
		return;
	}

	charger_cdev = cdev_alloc();
	if (!charger_cdev) {
		chr_err("[%s]cdev_alloc fail\n", __func__);
		goto unregister;
	}
	charger_cdev->owner = THIS_MODULE;
	charger_cdev->ops = &charger_ftm_fops;

	ret = cdev_add(charger_cdev, charger_devno, 1);
	if (ret < 0) {
		chr_err("[%s] cdev_add failed\n", __func__);
		goto free_cdev;
	}

	charger_major = MAJOR(charger_devno);
	charger_class = class_create(THIS_MODULE, CHARGER_DEVNAME);
	if (IS_ERR(charger_class)) {
		chr_err("[%s] class_create failed\n", __func__);
		goto free_cdev;
	}

	class_dev = (struct class_device *)device_create(charger_class,
				NULL, charger_devno, NULL, CHARGER_DEVNAME);
	if (IS_ERR(class_dev)) {
		chr_err("[%s] device_create failed\n", __func__);
		goto free_class;
	}

	pr_debug("%s done\n", __func__);
	return;

free_class:
	class_destroy(charger_class);
free_cdev:
	cdev_del(charger_cdev);
unregister:
	unregister_chrdev_region(charger_devno, 1);
}
/* factory mode end */

void cne_charger_get_atm_mode(struct charger_manager *info)
{
#ifdef CONFIG_QGKI
	char atm_str[64] = {0};
	char *ptr = NULL, *ptr_e = NULL;
	char keyword[] = "androidboot.atm=";
	int size = 0;

	info->atm_enabled = false;

	ptr = strstr(saved_command_line, keyword);
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");
		if (ptr_e == NULL)
			goto end;

		size = ptr_e - (ptr + strlen(keyword));
		if (size <= 0)
			goto end;
		strncpy(atm_str, ptr + strlen(keyword), size);
		atm_str[size] = '\0';

		if (!strncmp(atm_str, "enable", strlen("enable")))
			info->atm_enabled = true;
	}
end:
	pr_info("atm_enabled = %d\n", info->atm_enabled);
#endif
}

static void charger_get_power_off_mode(struct charger_manager *info)
{
#ifdef CONFIG_QGKI
	char *start = NULL;

	info->power_off_charging = false;
	start = strnstr(saved_command_line, "androidboot.mode=charger",
			strlen(saved_command_line));
	if (start)
		info->power_off_charging = true;

	pr_info("is power off charging:%d\n", info->power_off_charging);

#endif
}

static void charger_get_demo_mode(struct charger_manager *info)
{
#ifdef CONFIG_QGKI
	char demo_mode_str[64] = {0};
	char *ptr = NULL, *ptr_e = NULL;
	char keyword[] = "charger_demo_mode=";
	int size = 0, cap = 0;
	int ret = 0;

	info->is_demo_mode = false;
	info->capacity_ctrl = 0;

	ptr = strstr(saved_command_line, keyword);
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");
		if (ptr_e == NULL)
			goto end;

		size = ptr_e - (ptr + strlen(keyword));
		if (size <= 0)
			goto end;

		strncpy(demo_mode_str, ptr + strlen(keyword), size);
		demo_mode_str[size] = '\0';

		ret = kstrtoint(demo_mode_str, 10, &cap);
		if (ret < 0)
			goto end;

		info->is_demo_mode = true;
		if(cap >= 100 || cap == 0) {
			info->is_demo_mode = false;
			info->capacity_ctrl = 100;
		} else if (cap <= 30)
			info->capacity_ctrl = 30;
		else
			info->capacity_ctrl = cap;
	}

end:
	pr_info("demo_mode = %d, capacity = %d\n", info->is_demo_mode, info->capacity_ctrl);

#else
	info->is_demo_mode = false;
	info->capacity_ctrl = 0;
#endif
}

/* internal algorithm common function */
bool is_dual_charger_supported(struct charger_manager *info)
{
	if (info->chg2_dev == NULL)
		return false;
	return true;
}

int charger_enable_vbus_ovp(struct charger_manager *pinfo, bool enable)
{
	int ret = 0;
	u32 sw_ovp = 0;

	if (enable)
		sw_ovp = pinfo->data.max_charger_voltage_setting;
	else
		sw_ovp = 15000000;

	/* Enable/Disable SW OVP status */
	pinfo->data.max_charger_voltage = sw_ovp;

	chr_err("[%s] en:%d ovp:%d\n",
			    __func__, enable, sw_ovp);
	return ret;
}

bool is_typec_adapter(struct charger_manager *info)
{
	int rp;
	int mode;
	enum charger_type chr_type;

	chr_type = get_charger_type(info);
	rp = adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL);
	if (info->pd_type == CNE_PD_CONNECT_TYPEC_ONLY_SNK &&
			rp != 500 &&
			info->chr_type != STANDARD_HOST &&
			info->chr_type != CHARGING_HOST &&
			info->enable_type_c == true)
		return true;

	/* Only TypeC charger */
	rp = adapter_dev_get_property(info->typec_adapter, TYPEC_RP_LEVEL);
	mode = adapter_dev_get_property(info->typec_adapter, TYPE_CC_MODE);
	if (mode == 1 &&
			rp != 500 && chr_type != STANDARD_HOST &&
			chr_type != CHARGING_HOST && info->enable_type_c == true)
		return true;

	return false;
}

int charger_get_vbus(void)
{
	int ret = 0;
	int vchr = 0;

	if (pinfo == NULL)
		return 0;
	ret = charger_dev_get_vbus(pinfo->chg1_dev, &vchr);
	if (ret < 0) {
		chr_err("%s: get vbus failed: %d\n", __func__, ret);
		return ret;
	}

	vchr = vchr / 1000;
	return vchr;
}

/* internal algorithm common function end */


/* sw jeita */
void sw_jeita_state_machine_init(struct charger_manager *info)
{
	struct sw_jeita_data *sw_jeita;

	if (info->enable_sw_jeita == true) {
		sw_jeita = &info->sw_jeita;
		info->battery_temp = battery_get_bat_temperature(info);

		if (info->battery_temp >= info->data.temp_t4_thres)
			sw_jeita->sm = TEMP_ABOVE_T4;
		else if (info->battery_temp > info->data.temp_t3_thres)
			sw_jeita->sm = TEMP_T3_TO_T4;
		else if (info->battery_temp >= info->data.temp_t2_thres)
			sw_jeita->sm = TEMP_T2_TO_T3;
		else if (info->battery_temp >= info->data.temp_t1_thres)
			sw_jeita->sm = TEMP_T1_TO_T2;
		else if (info->battery_temp >= info->data.temp_t0_thres)
			sw_jeita->sm = TEMP_T0_TO_T1;
		else
			sw_jeita->sm = TEMP_BELOW_T0;

		chr_err("[SW_JEITA] tmp:%d sm:%d\n",
			info->battery_temp, sw_jeita->sm);
	}
}

void do_sw_jeita_state_machine(struct charger_manager *info)
{
	struct sw_jeita_data *sw_jeita;

	sw_jeita = &info->sw_jeita;
	sw_jeita->pre_sm = sw_jeita->sm;
	sw_jeita->charging = true;

	/* JEITA battery temp Standard */
	if (info->battery_temp >= info->data.temp_t4_thres) {
		chr_err("[SW_JEITA] Battery Over high Temperature(%d) !!\n",
			info->data.temp_t4_thres);

		sw_jeita->sm = TEMP_ABOVE_T4;
		sw_jeita->charging = false;
	} else if (info->battery_temp > info->data.temp_t3_thres) {
		/* control 45 degree to normal behavior */
		if ((sw_jeita->sm == TEMP_ABOVE_T4)
		    && (info->battery_temp
			>= info->data.temp_t4_thres_minus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t4_thres_minus_x_degree,
				info->data.temp_t4_thres);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t3_thres,
				info->data.temp_t4_thres);

			sw_jeita->sm = TEMP_T3_TO_T4;
		}
	} else if (info->battery_temp >= info->data.temp_t2_thres) {
		if (((sw_jeita->sm == TEMP_T3_TO_T4)
		     && (info->battery_temp
			 >= info->data.temp_t3_thres_minus_x_degree))
		    || ((sw_jeita->sm == TEMP_T1_TO_T2)
			&& (info->battery_temp
			    <= info->data.temp_t2_thres_plus_x_degree))) {
			chr_err("[SW_JEITA] Battery Temperature not recovery to normal temperature charging mode yet!!\n");
		} else {
			chr_err("[SW_JEITA] Battery Normal Temperature between %d and %d !!\n",
				info->data.temp_t2_thres,
				info->data.temp_t3_thres);
			sw_jeita->sm = TEMP_T2_TO_T3;
		}
	} else if (info->battery_temp >= info->data.temp_t1_thres) {
		if ((sw_jeita->sm == TEMP_T0_TO_T1
		     || sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t1_thres_plus_x_degree)) {
			if (sw_jeita->sm == TEMP_T0_TO_T1) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
					info->data.temp_t1_thres_plus_x_degree,
					info->data.temp_t2_thres);
			}
			if (sw_jeita->sm == TEMP_BELOW_T0) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
					info->data.temp_t1_thres,
					info->data.temp_t1_thres_plus_x_degree);
				sw_jeita->charging = false;
			}
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t1_thres,
				info->data.temp_t2_thres);

			sw_jeita->sm = TEMP_T1_TO_T2;
		}
	} else if (info->battery_temp >= info->data.temp_t0_thres) {
		if ((sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t0_thres_plus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t0_thres,
				info->data.temp_t0_thres_plus_x_degree);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t0_thres,
				info->data.temp_t1_thres);

			sw_jeita->sm = TEMP_T0_TO_T1;
		}
	} else {
		chr_err("[SW_JEITA] Battery below low Temperature(%d) !!\n",
			info->data.temp_t0_thres);
		sw_jeita->sm = TEMP_BELOW_T0;
		sw_jeita->charging = false;
	}

	/* set CV after temperature changed */
	/* In normal range, we adjust CV dynamically */
	switch (sw_jeita->sm) {
		case TEMP_ABOVE_T4:
			sw_jeita->cv = info->data.jeita_temp_above_t4_cv;
			sw_jeita->cur = info->data.jeita_temp_above_t4_cur;
			break;

		case TEMP_T3_TO_T4:
			sw_jeita->cv = info->data.jeita_temp_t3_to_t4_cv;
			sw_jeita->cur = info->data.jeita_temp_t3_to_t4_cur;
			break;

		case TEMP_T2_TO_T3:
			sw_jeita->cv = info->data.jeita_temp_t2_to_t3_cv;
			sw_jeita->cur = info->data.jeita_temp_t2_to_t3_cur;
			break;

		case TEMP_T1_TO_T2:
			sw_jeita->cv = info->data.jeita_temp_t1_to_t2_cv;
			sw_jeita->cur = info->data.jeita_temp_t1_to_t2_cur;
			break;

		case TEMP_T0_TO_T1:
			sw_jeita->cv = info->data.jeita_temp_t0_to_t1_cv;
			sw_jeita->cur = info->data.jeita_temp_t0_to_t1_cur;
			break;
		case TEMP_BELOW_T0:
			sw_jeita->cv = info->data.jeita_temp_below_t0_cv;
			sw_jeita->cur = info->data.jeita_temp_below_t0_cur;
			break;
	
		default:
			sw_jeita->cv = 0;
			sw_jeita->cur = 0;
			break;
	}

	chr_err("[SW_JEITA]preState:%d newState:%d tmp:%d cv:%d cur:%d\n",
		sw_jeita->pre_sm, sw_jeita->sm, info->battery_temp,
		sw_jeita->cv, sw_jeita->cur);
}

static ssize_t show_sw_jeita(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->enable_sw_jeita);
	return sprintf(buf, "%d\n", pinfo->enable_sw_jeita);
}

static ssize_t store_sw_jeita(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_sw_jeita = false;
		else {
			pinfo->enable_sw_jeita = true;
			sw_jeita_state_machine_init(pinfo);
		}

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR(sw_jeita, 0644, show_sw_jeita,
		   store_sw_jeita);
/* sw jeita end*/

static ssize_t show_charger_log_level(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	chr_err("%s: %d\n", __func__, chargerlog_level);
	return sprintf(buf, "%d\n", chargerlog_level);
}

static ssize_t store_charger_log_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret = 0;

	chr_err("%s\n", __func__);

	if (buf != NULL && size != 0) {
		chr_err("%s: buf is %s\n", __func__, buf);
		ret = kstrtoul(buf, 10, &val);
		if (ret < 0) {
			chr_err("%s: kstrtoul fail, ret = %d\n", __func__, ret);
			return ret;
		}
		if (val < 0) {
			chr_err("%s: val is inavlid: %ld\n", __func__, val);
			val = 0;
		}
		chargerlog_level = val;
		chr_err("%s: log_level=%d\n", __func__, chargerlog_level);
	}
	return size;
}
static DEVICE_ATTR(charger_log_level, 0644, show_charger_log_level,
		store_charger_log_level);

static ssize_t show_pdc_max_watt_level(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	return sprintf(buf, "%d\n", cne_pdc_get_max_watt(pinfo));
}

static ssize_t store_pdc_max_watt_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		cne_pdc_set_max_watt(pinfo, temp);
		chr_err("[store_pdc_max_watt]:%d\n", temp);
	} else
		chr_err("[store_pdc_max_watt]: format error!\n");

	return size;
}
static DEVICE_ATTR(pdc_max_watt, 0644, show_pdc_max_watt_level,
		store_pdc_max_watt_level);

#if 0
int cne_get_dynamic_cv(struct charger_manager *info, unsigned int *cv)
{
	int ret = 0;
	u32 _cv, _cv_temp;
	unsigned int vbat_threshold[4] = {3400000, 0, 0, 0};
	u32 vbat_bif = 0, vbat_auxadc = 0, vbat = 0;
	u32 retry_cnt = 0;

	if (pmic_is_bif_exist()) {
		do {
			vbat_auxadc = battery_get_bat_voltage() * 1000;
			ret = pmic_get_bif_battery_voltage(&vbat_bif);
			vbat_bif = vbat_bif * 1000;
			if (ret >= 0 && vbat_bif != 0 &&
			    vbat_bif < vbat_auxadc) {
				vbat = vbat_bif;
				chr_err("%s: use BIF vbat = %duV, dV to auxadc = %duV\n",
					__func__, vbat, vbat_auxadc - vbat_bif);
				break;
			}
			retry_cnt++;
		} while (retry_cnt < 5);

		if (retry_cnt == 5) {
			ret = 0;
			vbat = vbat_auxadc;
			chr_err("%s: use AUXADC vbat = %duV, since BIF vbat = %duV\n",
				__func__, vbat_auxadc, vbat_bif);
		}

		/* Adjust CV according to the obtained vbat */
		vbat_threshold[1] = info->data.bif_threshold1;
		vbat_threshold[2] = info->data.bif_threshold2;
		_cv_temp = info->data.bif_cv_under_threshold2;

		if (!info->enable_dynamic_cv && vbat >= vbat_threshold[2]) {
			_cv = info->data.battery_cv;
			goto out;
		}

		if (vbat < vbat_threshold[1])
			_cv = 4608000;
		else if (vbat >= vbat_threshold[1] && vbat < vbat_threshold[2])
			_cv = _cv_temp;
		else {
			_cv = info->data.battery_cv;
			info->enable_dynamic_cv = false;
		}
out:
		*cv = _cv;
		chr_err("%s: CV = %duV, enable_dynamic_cv = %d\n",
			__func__, _cv, info->enable_dynamic_cv);
	} else
		ret = -ENOTSUPP;

	return ret;
}
#endif

void cne_chg_check_cap_in_smt(struct charger_manager *info) {
	enum charger_type chr_type;
	int soc = 0;

	chr_type = get_charger_type(info);
	soc  = battery_get_bat_capacity(info);
	if ((chr_type == CHARGER_UNKNOWN) || (soc < 0)) {
		pr_err("charger is not plug, or batt soc get failed!\n");
		return;
	}

	if (soc < info->cap_low) {
		info->enable_hiz = 0;
		charger_dev_enable_hz(info->chg1_dev, info->enable_hiz);
		charger_dev_enable_hz(info->chg2_dev, info->enable_hiz);
	} else if (soc >= info->cap_high) {
		info->enable_hiz = 1;
		charger_dev_enable_hz(info->chg1_dev, info->enable_hiz);
		charger_dev_enable_hz(info->chg2_dev, info->enable_hiz);
	}

	pr_info("cap_range[%d, %d], cur_cap[%d], hiz mode[%d]\n",
			info->cap_low, info->cap_high, soc, info->enable_hiz);
}

int charger_manager_notifier(struct charger_manager *info, int event)
{
	return srcu_notifier_call_chain(&info->evt_nh, event, NULL);
}

static void battery_info_change_work(struct work_struct *work) {
	struct charger_manager *info = container_of(work,
			struct charger_manager, battery_info_change_work);
	struct power_supply *batt_psy = NULL;
	union power_supply_propval val;
	int ret;
	int tmp = 0;

	batt_psy = power_supply_get_by_name("battery");
	if (!batt_psy) {
		chr_err("Battery psy is null, skip wake up charger check!\n");
		return;
	}

	ret = power_supply_get_property(batt_psy, POWER_SUPPLY_PROP_TEMP, &val);
	if (!ret) {
		tmp = val.intval;
		if (info->battery_temp != tmp
		    && get_charger_type(info) != CHARGER_UNKNOWN) {
			_wake_up_charger(info);
			chr_err("%s: %s tmp:%d %d chr:%d\n",
				__func__, batt_psy->desc->name, tmp,
				info->battery_temp,
				get_charger_type(info));
		}
	}

	/* check the cap */
	if (info->aging_enable)
			cne_chg_check_cap_in_smt(info);
}

int charger_psy_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, psy_nb);
	struct power_supply *psy = v;

	if (work_pending(&info->battery_info_change_work))
		return NOTIFY_OK;

	if (strcmp(psy->desc->name, "battery") == 0)
		schedule_work(&info->battery_info_change_work);

	return NOTIFY_DONE;
}

void cne_charger_int_handler(void)
{
	chr_err("%s\n", __func__);

	if (pinfo == NULL) {
		chr_err("charger is not rdy ,skip1\n");
		return;
	}

	if (pinfo->init_done != true) {
		chr_err("charger is not rdy ,skip2\n");
		return;
	}

	if (get_charger_type(pinfo) == CHARGER_UNKNOWN) {
		mutex_lock(&pinfo->cable_out_lock);
		pinfo->cable_out_cnt++;
		chr_err("cable_out_cnt=%d\n", pinfo->cable_out_cnt);
		mutex_unlock(&pinfo->cable_out_lock);
		charger_manager_notifier(pinfo, CHARGER_NOTIFY_STOP_CHARGING);
	} else
		charger_manager_notifier(pinfo, CHARGER_NOTIFY_START_CHARGING);

	chr_err("wake_up_charger\n");
	_wake_up_charger(pinfo);
}

void cne_charger_is_doing_hvdcp_detect(bool doing)
{
	struct charger_manager *info = pinfo;
	enum charger_type chr_type;

	chr_err("doing = %d\n", doing);

	if (info == NULL) {
		chr_err("charger is not rdy ,skip1\n");
		return;
	}

	if (info->init_done != true) {
		chr_err("charger is not rdy ,skip2\n");
		return;
	}

	chr_type = get_charger_type(info);
	if (chr_type == HVDCP_QC20_CHARGER)
		info->chr_type = chr_type;
	info->is_doing_hvdcp_check = doing;

	chr_err("wake_up_charger\n");
	_wake_up_charger(info);
}

#ifndef CONFIG_QGKI
static enum power_supply_property cne_charger_manager_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
};

static int cne_charger_manager_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct charger_manager *info = power_supply_get_drvdata(psy);
	enum cne_power_supply_property cne_psy = CNE_GET_CPSP_FROM_VAL(val->intval);
	bool enable = false;
	bool doing = false;

	if (!info) {
		pr_notice("charger manager is NULL\n");
		return -EINVAL;
	}

	pr_info("++ cne_psy:%d val:%d\n", cne_psy, CNE_GET_CVAL_FROM_VAL(val->intval));

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		switch (cne_psy) {
		case CNE_POWER_SUPPLY_CHARGER_IN:
			cne_charger_int_handler();
			break;

		case CNE_POWER_SUPPLY_ENABLE_HVDCP_DETECT:
			enable = (bool)CNE_GET_CVAL_FROM_VAL(val->intval);
			charger_manager_enable_hvdcp_det(info->chg1_consumer, enable);
			break;

		case CNE_POWER_SUPPLY_IS_DOING_HVDCP:
			doing = (bool)CNE_GET_CVAL_FROM_VAL(val->intval);
			cne_charger_is_doing_hvdcp_detect(doing);
			break;

		case CNE_POWER_SUPPLY_IGNORE_CHGTYPE_DETECT:
			enable = (bool)CNE_GET_CVAL_FROM_VAL(val->intval);
			charger_manager_ignore_chg_type_det(info->chg1_consumer, enable);
			break;

		case CNE_POWER_SUPPLY_ENABLE_BC12_DETECT:
			enable = (bool)CNE_GET_CVAL_FROM_VAL(val->intval);
			charger_manager_enable_chg_type_det(info->chg1_consumer, enable);
			break;

		default:
			break;
		}
		break;

	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		charger_manager_set_charging_current_limit(info->chg1_consumer,
				MAIN_CHARGER, val->intval);
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		charger_manager_set_input_current_limit(info->chg1_consumer,
				MAIN_CHARGER, val->intval);
		break;

	default:
		break;
	}

	return 0;
}

static int cne_charger_manager_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct charger_manager *info = power_supply_get_drvdata(psy);

	if (!info) {
		pr_notice("charger manager is NULL\n");
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		charger_manager_get_charging_current_limit(info->chg1_consumer,
				MAIN_CHARGER, &(val->intval));
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		charger_manager_get_input_current_limit(info->chg1_consumer,
				MAIN_CHARGER, &(val->intval));
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL:
		charger_manager_get_charging_done(info->chg1_consumer,
				MAIN_CHARGER, &(val->intval));
		break;

	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int register_charger_manager_psy(struct charger_manager *info)
{
	int ret = 0;

	pr_info("++\n");
	info->manager_desc.name = "charger-manager";
	info->manager_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->manager_desc.properties = cne_charger_manager_properties;
	info->manager_desc.num_properties = ARRAY_SIZE(cne_charger_manager_properties);;
	info->manager_desc.set_property = cne_charger_manager_set_property;
	info->manager_desc.get_property = cne_charger_manager_get_property;
	info->manager_cfg.drv_data = info;

	info->manager_psy = devm_power_supply_register(&info->pdev->dev,
		&info->manager_desc, &info->manager_cfg);
	if (IS_ERR(info->manager_psy)) {
		pr_err("Failed to register charger-manager power supply: %ld\n",
				PTR_ERR(info->manager_psy));
		ret = PTR_ERR(info->manager_psy);
		info->manager_psy = NULL;
		return ret;
	}

	return ret;
}
#endif
static int cne_charger_plug_in(struct charger_manager *info,
				enum charger_type chr_type)
{
	info->chr_type = chr_type;
	info->charger_thread_polling = true;

	info->can_charging = true;
	info->enable_dynamic_cv = true;
	info->safety_timeout = false;
	info->vbusov_stat = false;

	chr_err("cne_is_charger_on plug in, type:%d\n", chr_type);
	__pm_stay_awake(plug_wakelock);
	if (info->plug_in != NULL)
		info->plug_in(info);

	// memset(&pinfo->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	// pinfo->sc.disable_in_this_plug = false;
	// wakeup_sc_algo_cmd(&pinfo->sc.data, SC_EVENT_PLUG_IN, 0);
	charger_dev_set_input_current(info->chg1_dev, 500000);
	charger_dev_plug_in(info->chg1_dev);
	charger_dev_plug_in(info->chg2_dev);
	return 0;
}

static int cne_charger_plug_out(struct charger_manager *info)
{
	struct charger_data *pdata1 = &info->chg1_data;
	struct charger_data *pdata2 = &info->chg2_data;

	chr_err("%s\n", __func__);
	__pm_relax(plug_wakelock);
	info->chr_type = CHARGER_UNKNOWN;
	info->charger_thread_polling = false;
	info->pd_reset = false;
	info->is_doing_hvdcp_check = false;
	info->is_dual_charger_enable = false;

	pdata1->disable_charging_count = 0;
	pdata1->input_current_limit_by_aicl = -1;
	pdata2->disable_charging_count = 0;

	if (info->plug_out != NULL)
		info->plug_out(info);

	charger_dev_set_input_current(info->chg1_dev, 100000);
	charger_dev_set_mivr(info->chg1_dev, info->data.min_charger_voltage);
	charger_dev_set_constant_voltage(info->chg1_dev, info->data.battery_cv);
	charger_dev_plug_out(info->chg1_dev);

	charger_dev_set_input_current(info->chg2_dev, 100000);
	charger_dev_set_mivr(info->chg2_dev, info->data.min_charger_voltage);
	charger_dev_set_constant_voltage(info->chg2_dev, info->data.battery_cv);
	charger_dev_plug_out(info->chg2_dev);
	return 0;
}

static bool cne_is_charger_on(struct charger_manager *info)
{
	enum charger_type chr_type;

	chr_type = get_charger_type(info);
	if (chr_type == CHARGER_UNKNOWN) {
		if (info->chr_type != CHARGER_UNKNOWN) {
			cne_charger_plug_out(info);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	} else {
		if (info->chr_type == CHARGER_UNKNOWN)
			cne_charger_plug_in(info, chr_type);
		else
			info->chr_type = chr_type;

		if (info->cable_out_cnt > 0) {
			cne_charger_plug_out(info);
			cne_charger_plug_in(info, chr_type);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt--;
			mutex_unlock(&info->cable_out_lock);
		}
	}

	if (chr_type == CHARGER_UNKNOWN)
		return false;

	return true;
}

static void charger_update_data(struct charger_manager *info)
{
	info->battery_temp = battery_get_bat_temperature(info);
}

static void check_battery_fault(struct charger_manager *info)
{
	union power_supply_propval propval;
	int vbat = 0;
	int count = 5;
	int ret = 0;

	if (!info->power_off_charging) {
		while (count-- > 0) {
			vbat = battery_get_bat_voltage(info);
			if (vbat < 0 || vbat > info->data.emergency_shutdown_vbat) {
				return;
			}

			pr_err("vbat(%d) is blow the low battery threshold(%d) %d times\n",
				_uV_to_mV(vbat), _uV_to_mV(info->data.emergency_shutdown_vbat), count);

			if (count > 0)
				msleep(1000);
		}

		pr_err("battery fault: vbat=%d, shutdown!!!!\n", _uV_to_mV(vbat));

		/* The battery level is critical and the framework will shut down */
		// kernel_power_off();
		propval.intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		ret = battery_set_property(info, POWER_SUPPLY_PROP_CAPACITY_LEVEL, &propval);
		if (ret < 0)
			pr_err("set battery CAPACITY_LEVEL prop fail\n");
		else
			pr_err("set battery CAPACITY_LEVEL prop success\n");

		propval.intval = 0;
		ret = battery_set_property(info, POWER_SUPPLY_PROP_STATUS, &propval);
		if (ret < 0)
			pr_err("set battery STATUS prop fail\n");
		else
			pr_err("set battery STATUS prop success\n");
	}
}

static void check_dynamic_mivr(struct charger_manager *info)
{
	int vbat = 0;

	if (info->enable_dynamic_mivr) {
		if (!cne_pdc_check_charger(info) && !hvdcp_is_ready(info)) {
			vbat = battery_get_bat_voltage(info);
			if (vbat <
				info->data.min_charger_voltage_2 / 1000 - 200) {
				charger_dev_set_mivr(info->chg1_dev,
					info->data.min_charger_voltage_2);
				charger_dev_set_mivr(info->chg2_dev,
					info->data.min_charger_voltage_2);
			} else if (vbat <
				info->data.min_charger_voltage_1 / 1000 - 200) {
				charger_dev_set_mivr(info->chg1_dev,
					info->data.min_charger_voltage_1);
				charger_dev_set_mivr(info->chg2_dev,
					info->data.min_charger_voltage_1);
			} else {
				charger_dev_set_mivr(info->chg1_dev,
					info->data.min_charger_voltage);
				charger_dev_set_mivr(info->chg2_dev,
					info->data.min_charger_voltage);
			}
		}
	}
}

static void cne_chg_get_tchg(struct charger_manager *info)
{
	int ret;
	int tchg_min = -127, tchg_max = -127;
	struct charger_data *pdata;

	pdata = &info->chg1_data;
	ret = charger_dev_get_temperature(info->chg1_dev, &tchg_min, &tchg_max);

	if (ret < 0) {
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
	} else {
		pdata->junction_temp_min = tchg_min;
		pdata->junction_temp_max = tchg_max;
	}

	if (is_slave_charger_exist()) {
		pdata = &info->chg2_data;
		ret = charger_dev_get_temperature(info->chg2_dev,
			&tchg_min, &tchg_max);

		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

}

static void charger_check_status(struct charger_manager *info)
{
	bool charging = true;
	int temperature = 0;
	int soc = 0;
	int real_soc = 0;
	union power_supply_propval propval;
	struct battery_thermal_protection_data *thermal = NULL;

	if (get_charger_type(info) == CHARGER_UNKNOWN)
		return;

	temperature = info->battery_temp;
	thermal = &info->thermal;

	if (info->enable_sw_jeita == true) {
		do_sw_jeita_state_machine(info);
		if (info->sw_jeita.charging == false) {
			charging = false;
			goto stop_charging;
		}
	} else {

		if (thermal->enable_min_charge_temp) {
			if (temperature < thermal->min_charge_temp) {
				chr_err("Battery Under Temperature or NTC fail %d %d\n",
					temperature, thermal->min_charge_temp);
				thermal->sm = BAT_TEMP_LOW;
				charging = false;
				goto stop_charging;
			} else if (thermal->sm == BAT_TEMP_LOW) {
				if (temperature >=
				    thermal->min_charge_temp_plus_x_degree) {
					chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
					thermal->min_charge_temp,
					temperature,
					thermal->min_charge_temp_plus_x_degree);
					thermal->sm = BAT_TEMP_NORMAL;
				} else {
					charging = false;
					goto stop_charging;
				}
			}
		}

		if (temperature >= thermal->max_charge_temp) {
			chr_err("Battery over Temperature or NTC fail %d %d\n",
				temperature, thermal->max_charge_temp);
			thermal->sm = BAT_TEMP_HIGH;
			charging = false;
			goto stop_charging;
		} else if (thermal->sm == BAT_TEMP_HIGH) {
			if (temperature
			    < thermal->max_charge_temp_minus_x_degree) {
				chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
				thermal->max_charge_temp,
				temperature,
				thermal->max_charge_temp_minus_x_degree);
				thermal->sm = BAT_TEMP_NORMAL;
			} else {
				charging = false;
				goto stop_charging;
			}
		}
	}

	cne_chg_get_tchg(info);

#if 0  // todo
	if (!cne_chg_check_vbus(info)) {
		charging = false;
		goto stop_charging;
	}
#endif

	if (info->cmd_discharging)
		charging = false;
	if (info->safety_timeout)
		charging = false;
	if (info->vbusov_stat)
		charging = false;
	if (info->enable_hiz)
		charging = false;
	if (info->is_demo_mode) {
		soc  = battery_get_bat_capacity(info);
		real_soc = battery_get_bat_real_capacity(info);
		if (soc >= info->capacity_ctrl) {
			if (real_soc > 0) {
				if (real_soc > (info->capacity_ctrl * 10 + 5))
					charging = false;
			} else {
				charging = false;
			}
		}
	}

#if 0
	if (info->sc.disable_charger == true)
		charging = false;
#endif

stop_charging:

	chr_err("tmp:%d (jeita:%d sm:%d cv:%d en:%d) sm:%d en:%d c:%d hiz:%d s:%d ov:%d sc:%d demo:(%d %d %d)\n",
		temperature, info->enable_sw_jeita, info->sw_jeita.sm,
		info->sw_jeita.cv, info->sw_jeita.charging, thermal->sm,
		charging, info->cmd_discharging, info->enable_hiz,
		info->safety_timeout, info->vbusov_stat,
		info->can_charging, info->is_demo_mode, info->capacity_ctrl, real_soc);

	if (charging != info->can_charging) {
		_charger_manager_enable_charging(info->chg1_consumer,
						0, charging);
		propval.intval = charging;
		battery_set_property(info, POWER_SUPPLY_PROP_STATUS, &propval);
	}

	info->can_charging = charging;
}

#ifdef CONFIG_PM
static int charger_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	struct timespec64 now;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pinfo->is_suspend = true;
		chr_debug("%s: enter PM_SUSPEND_PREPARE\n", __func__);
		break;
	case PM_POST_SUSPEND:
		pinfo->is_suspend = false;
		chr_debug("%s: enter PM_POST_SUSPEND\n", __func__);
		// get_monotonic_boottime(&now);
		now = ktime_to_timespec64(ktime_get_boottime());

		if (timespec64_compare(&now, &pinfo->endtime) >= 0 &&
			pinfo->endtime.tv_sec != 0 &&
			pinfo->endtime.tv_nsec != 0) {
			chr_err("%s: alarm timeout, wake up charger\n",
				__func__);
			pinfo->endtime.tv_sec = 0;
			pinfo->endtime.tv_nsec = 0;
			_wake_up_charger(pinfo);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block charger_pm_notifier_func = {
	.notifier_call = charger_pm_event,
	.priority = 0,
};
#endif /* CONFIG_PM */

static enum alarmtimer_restart
	cne_charger_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct charger_manager *info =
	container_of(alarm, struct charger_manager, charger_timer);
	unsigned long flags;

	if (info->is_suspend == false) {
		chr_err("%s: not suspend, wake up charger\n", __func__);
		_wake_up_charger(info);
	} else {
		chr_err("%s: alarm timer timeout\n", __func__);
		spin_lock_irqsave(&info->slock, flags);
		if (!info->charger_wakelock->active)
			__pm_stay_awake(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
	}

	return ALARMTIMER_NORESTART;
}

static void cne_charger_start_timer(struct charger_manager *info)
{
	ktime_t k_now, k_add, k_end;
#ifdef CONFIG_QGKI
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&pinfo->charger_timer);
	if (ret < 0) {
		chr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}
#else
	alarm_cancel(&pinfo->charger_timer);
#endif

	k_now = ktime_get_boottime();
	k_add = ktime_set(info->polling_interval, 0);
	k_end = ktime_add(k_now, k_add);
	info->endtime = ktime_to_timespec64(k_end);
	pr_debug("%s: alarm timer start:%d, %ld %ld\n", __func__, info->polling_interval,
		info->endtime.tv_sec, info->endtime.tv_nsec);

	alarm_start(&pinfo->charger_timer, k_end);
}

static void cne_charger_init_timer(struct charger_manager *info)
{
	alarm_init(&info->charger_timer, ALARM_BOOTTIME,
			cne_charger_alarm_timer_func);
	cne_charger_start_timer(info);

#ifdef CONFIG_PM
	if (register_pm_notifier(&charger_pm_notifier_func))
		chr_err("%s: register pm failed\n", __func__);
#endif /* CONFIG_PM */
}

static int charger_routine_thread(void *arg)
{
	struct charger_manager *info = arg;
	unsigned long flags = 0;
	bool is_charger_on = false;
	int bat_current = 0;
	int bat_voltage = 0, bat_temp = 0;

	while (1) {
		wait_event(info->wait_que,
			(info->charger_thread_timeout == true));

		mutex_lock(&info->charger_lock);
		spin_lock_irqsave(&info->slock, flags);
		if (!info->charger_wakelock->active)
			__pm_stay_awake(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);

		info->charger_thread_timeout = false;
		bat_current = battery_get_bat_current(info);
		bat_voltage = battery_get_bat_voltage(info);
		bat_temp = battery_get_bat_temperature(info);
		chr_err("Vbat=%d,Ibat=%d,VChr=%d,T=%d,CT:%d:%d hv:%d pd:%d:%d\n",
			bat_voltage, bat_current, battery_get_vbus(), bat_temp,
			get_charger_type(info), info->chr_type,
			info->enable_hv_charging,
			info->pd_type, info->pd_reset);

		is_charger_on = cne_is_charger_on(info);

		if (info->charger_thread_polling == true)
			cne_charger_start_timer(info);

		charger_update_data(info);
		check_battery_fault(info);
		check_dynamic_mivr(info);
		charger_check_status(info);
		// kpoc_power_off_check(info);

		if (is_disable_charger() == false && 
			is_charger_on && info->can_charging) {
			if (info->do_algorithm)
				info->do_algorithm(info);
		} else {
			chr_debug("disable charging %d %d %d\n",
				is_disable_charger(),
				is_charger_on, info->can_charging);
			if (info->kick_watchdog)
				info->kick_watchdog(info);
		}

		spin_lock_irqsave(&info->slock, flags);
		__pm_relax(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		//chr_debug("%s end , %d\n",
		//	__func__, info->charger_thread_timeout);
		mutex_unlock(&info->charger_lock);
	}

	return 0;
}

static int cne_charger_parse_time_param(struct charger_manager *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	int byte_len, len, idx, time_max, ibat_max;
	int ret = 0;

	info->time_param_len = 0;

	if (of_find_property(np, "time-param", &byte_len)) {
		len = byte_len / sizeof(u32);
		if (len <= 0 || (len % 2 != 0)) {
			pr_err("time-param length(%d) invalid.\n", len);
			return ret;
		}

		info->time_param = devm_kzalloc(dev, byte_len, GFP_KERNEL);
		if (!info->time_param) {
			pr_err("alloc time-param fail.\n");
			return -ENOMEM;
		}

		idx = 0;
		while (idx < len) {
			ret = of_property_read_u32_index(np, "time-param", idx, &time_max);
			if (ret < 0) {
				pr_err("time-param time(%d) invalid.\n", idx);
				break;
			}

			idx++;
			ret = of_property_read_u32_index(np, "time-param", idx, &ibat_max);
			if (ret < 0) {
				pr_err("time-param ibat(idx) invalid.\n", idx);
				break;
			}

			info->time_param[idx / 2].chg_time = time_max;
			info->time_param[idx / 2].ibat_max = ibat_max;
			pr_debug("time=%d ibat_max=%d\n",
				info->time_param[idx / 2].chg_time,
				info->time_param[idx / 2].ibat_max);

			idx++;
		}

		if (!ret)
			info->time_param_len = len / 2;

		pr_debug("time_param_len=%d\n", info->time_param_len);
	}

	return ret;
}

static int cne_charger_parse_temp_compensation(struct charger_manager *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	int byte_len, len, idx, comp_max, ibat_min;
	int ret = 0;

	info->temp_comp_len = 0;

	if (of_find_property(np, "temp-compensation", &byte_len)) {
		len = byte_len / sizeof(u32);
		if (len <= 0 || (len % 2 != 0)) {
			pr_err("temp-compensation length(%d) invalid.\n", len);
			return ret;
		}

		info->temp_comp = devm_kzalloc(dev, byte_len, GFP_KERNEL);
		if (!info->temp_comp) {
			pr_err("alloc temp-compensation fail.\n");
			return -ENOMEM;
		}

		idx = 0;
		while (idx < len) {
			ret = of_property_read_u32_index(np, "temp-compensation", idx, &comp_max);
			if (ret < 0) {
				pr_err("temp-compensation compensation(%d) invalid.\n", idx);
				break;
			}

			idx++;
			ret = of_property_read_u32_index(np, "temp-compensation", idx, &ibat_min);
			if (ret < 0) {
				pr_err("temp-compensation ibat(idx) invalid.\n", idx);
				break;
			}

			info->temp_comp[idx / 2].temp_comp = comp_max;
			info->temp_comp[idx / 2].ibat_min = ibat_min;
			pr_info("comp_max=%d ibat_min=%d\n",
				info->temp_comp[idx / 2].temp_comp,
				info->temp_comp[idx / 2].ibat_min);

			idx++;
		}

		if (!ret)
			info->temp_comp_len = len / 2;

		pr_info("temp_comp_len=%d\n", info->temp_comp_len);
	}

	return ret;
}

static int cne_charger_parse_dt(struct charger_manager *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val;
	int byte_len;
	int ret;

	chr_err("%s: starts\n", __func__);

	if (!np) {
		chr_err("%s: no device node\n", __func__);
		return -EINVAL;
	}

	if (of_property_read_string(np, "algorithm_name",
		&info->algorithm_name) < 0) {
		chr_err("%s: no algorithm_name name\n", __func__);
		info->algorithm_name = "SwitchCharging";
	}

	if (strcmp(info->algorithm_name, "SwitchCharging") == 0) {
		chr_err("found SwitchCharging\n");
#if 0  // todo
		cne_switch_charging_init(info);
#endif
	}
#ifdef CONFIG_CNE_DUAL_CHARGER_SUPPORT
	if (strcmp(info->algorithm_name, "DualSwitchCharging") == 0) {
		pr_debug("found DualSwitchCharging\n");
		cne_dual_switch_charging_init(info);
	}
#endif

	info->disable_charger = of_property_read_bool(np, "disable_charger");
	info->enable_sw_safety_timer =
			of_property_read_bool(np, "enable_sw_safety_timer");
	info->sw_safety_timer_setting = info->enable_sw_safety_timer;
	info->enable_sw_jeita = of_property_read_bool(np, "enable_sw_jeita");
	info->enable_type_c = of_property_read_bool(np, "enable_type_c");
	info->enable_dynamic_mivr =
			of_property_read_bool(np, "enable_dynamic_mivr");
	info->disable_pd_dual = of_property_read_bool(np, "disable_pd_dual");

	info->enable_hv_charging = true;

	/* common */
	if (of_property_read_u32(np, "battery_cv", &val) >= 0)
		info->data.battery_cv = val;
	else {
		chr_err("use default BATTERY_CV:%d\n", BATTERY_CV);
		info->data.battery_cv = BATTERY_CV;
	}

	info->data.enable_software_control_recharge =
		of_property_read_bool(np, "enable_software_control_recharge");
	if (of_property_read_u32(np, "recharge_voltage_threshold", &val) >= 0)
		info->data.recharge_voltage_threshold = val;
	else {
		chr_err("use default RECHARGE_THRESHOLD:%d\n",
			SOFTWARE_CONTROL_RECHARGE_THRESHOLD);
		info->data.recharge_voltage_threshold = SOFTWARE_CONTROL_RECHARGE_THRESHOLD;
	}

	if (of_property_read_u32(np, "recharge_soc_threshold", &val) >= 0)
		info->data.recharge_soc_threshold = val;
	else {
		chr_err("use default RECHARGE_SOC_THRESHOLD:%d\n",
			SOFTWARE_CONTROL_RECHARGE_SOC_THRESHOLD);
		info->data.recharge_soc_threshold = SOFTWARE_CONTROL_RECHARGE_SOC_THRESHOLD;
	}

	if (of_property_read_u32(np, "emergency_shutdown_vbat", &val) >= 0)
		info->data.emergency_shutdown_vbat = val;
	else {
		chr_err("use default EMERGENCY_SHUTDOWN_VBATT:%d\n", EMERGENCY_SHUTDOWN_VBATT);
		info->data.emergency_shutdown_vbat = EMERGENCY_SHUTDOWN_VBATT;
	}

	if (of_property_read_u32(np, "max_charger_voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.max_charger_voltage = V_CHARGER_MAX;
	}
	info->data.max_charger_voltage_setting = info->data.max_charger_voltage;

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MIN:%d\n", V_CHARGER_MIN);
		info->data.min_charger_voltage = V_CHARGER_MIN;
	}

	/* dynamic mivr */
	if (of_property_read_u32(np, "min_charger_voltage_1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else {
		chr_err("use default V_CHARGER_MIN_1:%d\n", V_CHARGER_MIN_1);
		info->data.min_charger_voltage_1 = V_CHARGER_MIN_1;
	}

	if (of_property_read_u32(np, "min_charger_voltage_2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else {
		chr_err("use default V_CHARGER_MIN_2:%d\n", V_CHARGER_MIN_2);
		info->data.min_charger_voltage_2 = V_CHARGER_MIN_2;
	}

	if (of_property_read_u32(np, "max_dmivr_charger_current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else {
		chr_err("use default MAX_DMIVR_CHARGER_CURRENT:%d\n",
			MAX_DMIVR_CHARGER_CURRENT);
		info->data.max_dmivr_charger_current =
					MAX_DMIVR_CHARGER_CURRENT;
	}

	/* charging current */
	if (of_property_read_u32(np, "usb_charger_current_suspend", &val) >= 0)
		info->data.usb_charger_current_suspend = val;
	else {
		chr_err("use default USB_CHARGER_CURRENT_SUSPEND:%d\n",
			USB_CHARGER_CURRENT_SUSPEND);
		info->data.usb_charger_current_suspend =
						USB_CHARGER_CURRENT_SUSPEND;
	}

	if (of_property_read_u32(np, "usb_charger_current_unconfigured", &val)
		>= 0) {
		info->data.usb_charger_current_unconfigured = val;
	} else {
		chr_err("use default USB_CHARGER_CURRENT_UNCONFIGURED:%d\n",
			USB_CHARGER_CURRENT_UNCONFIGURED);
		info->data.usb_charger_current_unconfigured =
					USB_CHARGER_CURRENT_UNCONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current_configured", &val)
		>= 0) {
		info->data.usb_charger_current_configured = val;
	} else {
		chr_err("use default USB_CHARGER_CURRENT_CONFIGURED:%d\n",
			USB_CHARGER_CURRENT_CONFIGURED);
		info->data.usb_charger_current_configured =
					USB_CHARGER_CURRENT_CONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current", &val) >= 0) {
		info->data.usb_charger_current = val;
	} else {
		chr_err("use default USB_CHARGER_CURRENT:%d\n",
			USB_CHARGER_CURRENT);
		info->data.usb_charger_current = USB_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_current", &val) >= 0) {
		info->data.ac_charger_current = val;
	} else {
		chr_err("use default AC_CHARGER_CURRENT:%d\n",
			AC_CHARGER_CURRENT);
		info->data.ac_charger_current = AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "hvdcp_charger_current", &val) >= 0) {
		info->data.hvdcp_charger_current = val;
	} else {
		chr_err("use default HVDCP_CHARGER_CURRENT:%d\n",
			HVDCP_CHARGER_CURRENT);
		info->data.hvdcp_charger_current = HVDCP_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "hvdcp_charger_input_current", &val) >= 0) {
		info->data.hvdcp_charger_input_current = val;
	} else {
		chr_err("use default HVDCP_CHARGER_CURRENT:%d\n",
			HVDCP_CHARGER_INPUT_CURRENT);
		info->data.hvdcp_charger_input_current = HVDCP_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "hvdcp_polling_interval", &val) >= 0) {
		info->data.hvdcp_polling_interval = val;
	} else {
		chr_err("use default HVDCP_POLLING_INTERVAL:%d\n",
			CHARGING_HVDCP_INTERVAL);
		info->data.hvdcp_polling_interval = CHARGING_HVDCP_INTERVAL;
	}

	if (of_property_read_u32(np, "hvdcp_exit_soc", &val) >= 0) {
		info->data.hvdcp_exit_soc = val;
	} else {
		chr_err("use default HVDCP_EXIT_SOC:%d\n",
			HVDCP_EXIT_SOC);
		info->data.hvdcp_exit_soc = HVDCP_EXIT_SOC;
	}

	if (of_property_read_u32(np, "hvdcp_exit_curr_ma", &val) >= 0) {
		info->data.hvdcp_exit_curr_ma = val;
	} else {
		chr_err("use default HVDCP_EXIT_CURR_MA:%d\n",
			HVDCP_EXIT_CURR_MA);
		info->data.hvdcp_exit_curr_ma = HVDCP_EXIT_CURR_MA;
	}

	if (of_property_read_u32(np, "hvdcp_exit_temp_h", &val) >= 0) {
		info->data.hvdcp_exit_temp_h = val;
	} else {
		chr_err("use default HVDCP_EXIT_TEMP_H:%d\n",
			HVDCP_EXIT_TEMP_H);
		info->data.hvdcp_exit_temp_h = HVDCP_EXIT_TEMP_H;
	}

	if (of_property_read_u32(np, "hvdcp_exit_temp_l", &val) >= 0) {
		info->data.hvdcp_exit_temp_l = val;
	} else {
		chr_err("use default HVDCP_EXIT_TEMP_L:%d\n",
			HVDCP_EXIT_TEMP_L);
		info->data.hvdcp_exit_temp_l = HVDCP_EXIT_TEMP_L;
	}

	if (of_property_read_u32(np, "pd_charger_input_current", &val) >= 0)
		info->data.pd_charger_input_current = val;
	else {
		chr_err("use default PD_CHARGER_INPUT_CURRENT:%d\n",
			PD_CHARGER_INPUT_CURRENT);
		info->data.pd_charger_input_current = PD_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "pd_charger_current", &val) >= 0)
		info->data.pd_charger_current = val;
	else {
		chr_err("use default PD_CHARGER_CURRENT:%d\n",
			PD_CHARGER_CURRENT);
		info->data.pd_charger_current = PD_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_input_current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else {
		chr_err("use default AC_CHARGER_INPUT_CURRENT:%d\n",
			AC_CHARGER_INPUT_CURRENT);
		info->data.ac_charger_input_current = AC_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "non_std_ac_charger_current", &val) >= 0)
		info->data.non_std_ac_charger_current = val;
	else {
		chr_err("use default NON_STD_AC_CHARGER_CURRENT:%d\n",
			NON_STD_AC_CHARGER_CURRENT);
		info->data.non_std_ac_charger_current =
					NON_STD_AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "charging_host_charger_current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else {
		chr_err("use default CHARGING_HOST_CHARGER_CURRENT:%d\n",
			CHARGING_HOST_CHARGER_CURRENT);
		info->data.charging_host_charger_current =
					CHARGING_HOST_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_1_0a_charger_current", &val) >= 0)
		info->data.apple_1_0a_charger_current = val;
	else {
		chr_err("use default APPLE_1_0A_CHARGER_CURRENT:%d\n",
			APPLE_1_0A_CHARGER_CURRENT);
		info->data.apple_1_0a_charger_current =
					APPLE_1_0A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_2_1a_charger_current", &val) >= 0)
		info->data.apple_2_1a_charger_current = val;
	else {
		chr_err("use default APPLE_2_1A_CHARGER_CURRENT:%d\n",
			APPLE_2_1A_CHARGER_CURRENT);
		info->data.apple_2_1a_charger_current =
					APPLE_2_1A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ta_ac_charger_current", &val) >= 0)
		info->data.ta_ac_charger_current = val;
	else {
		chr_err("use default TA_AC_CHARGING_CURRENT:%d\n",
			TA_AC_CHARGING_CURRENT);
		info->data.ta_ac_charger_current =
					TA_AC_CHARGING_CURRENT;
	}

	/* sw jeita */
	if (of_property_read_u32(np, "jeita_temp_above_t4_cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_CV:%d\n",
			JEITA_TEMP_ABOVE_T4_CV);
		info->data.jeita_temp_above_t4_cv = JEITA_TEMP_ABOVE_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_CV:%d\n",
			JEITA_TEMP_T3_TO_T4_CV);
		info->data.jeita_temp_t3_to_t4_cv = JEITA_TEMP_T3_TO_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_CV:%d\n",
			JEITA_TEMP_T2_TO_T3_CV);
		info->data.jeita_temp_t2_to_t3_cv = JEITA_TEMP_T2_TO_T3_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_CV:%d\n",
			JEITA_TEMP_T1_TO_T2_CV);
		info->data.jeita_temp_t1_to_t2_cv = JEITA_TEMP_T1_TO_T2_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_CV:%d\n",
			JEITA_TEMP_T0_TO_T1_CV);
		info->data.jeita_temp_t0_to_t1_cv = JEITA_TEMP_T0_TO_T1_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_CV:%d\n",
			JEITA_TEMP_BELOW_T0_CV);
		info->data.jeita_temp_below_t0_cv = JEITA_TEMP_BELOW_T0_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_above_t4_cur", &val) >= 0)
		info->data.jeita_temp_above_t4_cur = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_CURR:%d\n",
			JEITA_TEMP_ABOVE_T4_CURR);
		info->data.jeita_temp_above_t4_cur = JEITA_TEMP_ABOVE_T4_CURR;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cur", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cur = val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_CURR:%d\n",
			JEITA_TEMP_T3_TO_T4_CURR);
		info->data.jeita_temp_t3_to_t4_cur = JEITA_TEMP_T3_TO_T4_CURR;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cur", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cur = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_CURR:%d\n",
			JEITA_TEMP_T2_TO_T3_CURR);
		info->data.jeita_temp_t2_to_t3_cur = JEITA_TEMP_T2_TO_T3_CURR;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cur", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cur = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_CURR:%d\n",
			JEITA_TEMP_T1_TO_T2_CURR);
		info->data.jeita_temp_t1_to_t2_cur = JEITA_TEMP_T1_TO_T2_CURR;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cur", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cur = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_CURR:%d\n",
			JEITA_TEMP_T0_TO_T1_CURR);
		info->data.jeita_temp_t0_to_t1_cur = JEITA_TEMP_T0_TO_T1_CURR;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cur", &val) >= 0)
		info->data.jeita_temp_below_t0_cur = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_CURR:%d\n",
			JEITA_TEMP_BELOW_T0_CURR);
		info->data.jeita_temp_below_t0_cur = JEITA_TEMP_BELOW_T0_CURR;
	}

	if (of_property_read_u32(np, "temp_t4_thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else {
		chr_err("use default TEMP_T4_THRES:%d\n",
			TEMP_T4_THRES);
		info->data.temp_t4_thres = TEMP_T4_THRES;
	}

	if (of_property_read_u32(np, "temp_t4_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T4_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T4_THRES_MINUS_X_DEGREE);
		info->data.temp_t4_thres_minus_x_degree =
					TEMP_T4_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t3_thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else {
		chr_err("use default TEMP_T3_THRES:%d\n",
			TEMP_T3_THRES);
		info->data.temp_t3_thres = TEMP_T3_THRES;
	}

	if (of_property_read_u32(np, "temp_t3_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T3_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T3_THRES_MINUS_X_DEGREE);
		info->data.temp_t3_thres_minus_x_degree =
					TEMP_T3_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t2_thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else {
		chr_err("use default TEMP_T2_THRES:%d\n",
			TEMP_T2_THRES);
		info->data.temp_t2_thres = TEMP_T2_THRES;
	}

	if (of_property_read_u32(np, "temp_t2_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T2_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T2_THRES_PLUS_X_DEGREE);
		info->data.temp_t2_thres_plus_x_degree =
					TEMP_T2_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t1_thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else {
		chr_err("use default TEMP_T1_THRES:%d\n",
			TEMP_T1_THRES);
		info->data.temp_t1_thres = TEMP_T1_THRES;
	}

	if (of_property_read_u32(np, "temp_t1_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T1_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T1_THRES_PLUS_X_DEGREE);
		info->data.temp_t1_thres_plus_x_degree =
					TEMP_T1_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t0_thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else {
		chr_err("use default TEMP_T0_THRES:%d\n",
			TEMP_T0_THRES);
		info->data.temp_t0_thres = TEMP_T0_THRES;
	}

	if (of_property_read_u32(np, "temp_t0_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T0_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T0_THRES_PLUS_X_DEGREE);
		info->data.temp_t0_thres_plus_x_degree =
					TEMP_T0_THRES_PLUS_X_DEGREE;
	}

	/* battery temperature protection */
	info->thermal.sm = BAT_TEMP_NORMAL;
	info->thermal.enable_min_charge_temp =
		of_property_read_bool(np, "enable_min_charge_temp");

	if (of_property_read_u32(np, "min_charge_temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else {
		chr_err("use default MIN_CHARGE_TEMP:%d\n",
			MIN_CHARGE_TEMP);
		info->thermal.min_charge_temp = MIN_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "min_charge_temp_plus_x_degree", &val)
	    >= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else {
		chr_err("use default MIN_CHARGE_TEMP_PLUS_X_DEGREE:%d\n",
			MIN_CHARGE_TEMP_PLUS_X_DEGREE);
		info->thermal.min_charge_temp_plus_x_degree =
					MIN_CHARGE_TEMP_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "max_charge_temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else {
		chr_err("use default MAX_CHARGE_TEMP:%d\n",
			MAX_CHARGE_TEMP);
		info->thermal.max_charge_temp = MAX_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "max_charge_temp_minus_x_degree", &val)
	    >= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else {
		chr_err("use default MAX_CHARGE_TEMP_MINUS_X_DEGREE:%d\n",
			MAX_CHARGE_TEMP_MINUS_X_DEGREE);
		info->thermal.max_charge_temp_minus_x_degree =
					MAX_CHARGE_TEMP_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "dual_polling_ieoc", &val) >= 0)
		info->data.dual_polling_ieoc = val;
	else {
		chr_err("use default dual_polling_ieoc :%d\n", 750000);
		info->data.dual_polling_ieoc = 750000;
	}

	/* PD */
	if (of_property_read_u32(np, "pd_vbus_upper_bound", &val) >= 0) {
		info->data.pd_vbus_upper_bound = val;
	} else {
		chr_err("use default pd_vbus_upper_bound:%d\n",
			PD_VBUS_UPPER_BOUND);
		info->data.pd_vbus_upper_bound = PD_VBUS_UPPER_BOUND;
	}

	if (of_property_read_u32(np, "pd_vbus_low_bound", &val) >= 0) {
		info->data.pd_vbus_low_bound = val;
	} else {
		chr_err("use default pd_vbus_low_bound:%d\n",
			PD_VBUS_LOW_BOUND);
		info->data.pd_vbus_low_bound = PD_VBUS_LOW_BOUND;
	}

	if (of_property_read_u32(np, "pd_ichg_level_threshold", &val) >= 0)
		info->data.pd_ichg_level_threshold = val;
	else {
		chr_err("use default pd_ichg_level_threshold:%d\n",
			PD_ICHG_LEAVE_THRESHOLD);
		info->data.pd_ichg_level_threshold = PD_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "pd_stop_battery_soc", &val) >= 0)
		info->data.pd_stop_battery_soc = val;
	else {
		chr_err("use default pd_stop_battery_soc:%d\n",
			PD_STOP_BATTERY_SOC);
		info->data.pd_stop_battery_soc = PD_STOP_BATTERY_SOC;
	}

	if (of_property_read_u32(np, "vsys_watt", &val) >= 0) {
		info->data.vsys_watt = val;
	} else {
		chr_err("use default vsys_watt:%d\n",
			VSYS_WATT);
		info->data.vsys_watt = VSYS_WATT;
	}

	if (of_property_read_u32(np, "ibus_err", &val) >= 0) {
		info->data.ibus_err = val;
	} else {
		chr_err("use default ibus_err:%d\n",
			IBUS_ERR);
		info->data.ibus_err = IBUS_ERR;
	}

	/* dual charger */
	if (of_property_read_u32(np, "chg1_ta_ac_charger_current", &val) >= 0)
		info->data.chg1_ta_ac_charger_current = val;
	else {
		chr_err("use default TA_AC_MASTER_CHARGING_CURRENT:%d\n",
			TA_AC_MASTER_CHARGING_CURRENT);
		info->data.chg1_ta_ac_charger_current =
						TA_AC_MASTER_CHARGING_CURRENT;
	}

	if (of_property_read_u32(np, "chg2_ta_ac_charger_current", &val) >= 0)
		info->data.chg2_ta_ac_charger_current = val;
	else {
		chr_err("use default TA_AC_SLAVE_CHARGING_CURRENT:%d\n",
			TA_AC_SLAVE_CHARGING_CURRENT);
		info->data.chg2_ta_ac_charger_current =
						TA_AC_SLAVE_CHARGING_CURRENT;
	}

	if (of_property_read_u32(np, "slave_mivr_diff", &val) >= 0)
		info->data.slave_mivr_diff = val;
	else {
		chr_err("use default SLAVE_MIVR_DIFF:%d\n", SLAVE_MIVR_DIFF);
		info->data.slave_mivr_diff = SLAVE_MIVR_DIFF;
	}

	/* slave charger */
	if (of_property_read_u32(np, "chg2_eff", &val) >= 0)
		info->data.chg2_eff = val;
	else {
		chr_err("use default CHG2_EFF:%d\n", CHG2_EFF);
		info->data.chg2_eff = CHG2_EFF;
	}

	info->data.parallel_vbus = of_property_read_bool(np, "parallel_vbus");

	/* cable measurement impedance */
	if (of_property_read_u32(np, "cable_imp_threshold", &val) >= 0)
		info->data.cable_imp_threshold = val;
	else {
		chr_err("use default CABLE_IMP_THRESHOLD:%d\n",
			CABLE_IMP_THRESHOLD);
		info->data.cable_imp_threshold = CABLE_IMP_THRESHOLD;
	}

	if (of_property_read_u32(np, "vbat_cable_imp_threshold", &val) >= 0)
		info->data.vbat_cable_imp_threshold = val;
	else {
		chr_err("use default VBAT_CABLE_IMP_THRESHOLD:%d\n",
			VBAT_CABLE_IMP_THRESHOLD);
		info->data.vbat_cable_imp_threshold = VBAT_CABLE_IMP_THRESHOLD;
	}

	/* BIF */
	if (of_property_read_u32(np, "bif_threshold1", &val) >= 0)
		info->data.bif_threshold1 = val;
	else {
		chr_err("use default BIF_THRESHOLD1:%d\n",
			BIF_THRESHOLD1);
		info->data.bif_threshold1 = BIF_THRESHOLD1;
	}

	if (of_property_read_u32(np, "bif_threshold2", &val) >= 0)
		info->data.bif_threshold2 = val;
	else {
		chr_err("use default BIF_THRESHOLD2:%d\n",
			BIF_THRESHOLD2);
		info->data.bif_threshold2 = BIF_THRESHOLD2;
	}

	if (of_property_read_u32(np, "bif_cv_under_threshold2", &val) >= 0)
		info->data.bif_cv_under_threshold2 = val;
	else {
		chr_err("use default BIF_CV_UNDER_THRESHOLD2:%d\n",
			BIF_CV_UNDER_THRESHOLD2);
		info->data.bif_cv_under_threshold2 = BIF_CV_UNDER_THRESHOLD2;
	}

	info->data.power_path_support =
				of_property_read_bool(np, "power_path_support");
	chr_debug("%s: power_path_support: %d\n",
		__func__, info->data.power_path_support);

	if (of_property_read_u32(np, "max_charging_time", &val) >= 0)
		info->data.max_charging_time = val;
	else {
		chr_err("use default MAX_CHARGING_TIME:%d\n",
			MAX_CHARGING_TIME);
		info->data.max_charging_time = MAX_CHARGING_TIME;
	}

	if (of_property_read_u32(np, "bc12_charger", &val) >= 0)
		info->data.bc12_charger = val;
	else {
		chr_err("use default BC12_CHARGER:%d\n",
			DEFAULT_BC12_CHARGER);
		info->data.bc12_charger = DEFAULT_BC12_CHARGER;
	}

	if (strcmp(info->algorithm_name, "SwitchCharging2") == 0) {
		chr_err("found SwitchCharging2\n");
		cne_switch_charging_init2(info);
	}

	chr_err("algorithm name:%s\n", info->algorithm_name);

	if (of_find_property(np, "thermal-mitigation", &byte_len)) {
		info->thermal_mitigation = devm_kzalloc(dev, byte_len,
				GFP_KERNEL);
		if (info->thermal_mitigation == NULL)
			return -ENOMEM;

		info->thermal_levels = byte_len / sizeof(u32);
		ret = of_property_read_u32_array(np,
			"thermal-mitigation",
			info->thermal_mitigation,
			info->thermal_levels);
		if (ret < 0) {
			pr_err("Couldn't read threm limits rc = %d\n", ret);
			return ret;
		}
	}

	cne_charger_parse_time_param(info, dev);
	cne_charger_parse_temp_compensation(info, dev);

	return 0;
}

static ssize_t show_input_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg1_data.thermal_input_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg1_data.thermal_input_current_limit);
}

static ssize_t store_input_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg1_data.thermal_input_current_limit = reg;
		if (pinfo->data.parallel_vbus)
			pinfo->chg2_data.thermal_input_current_limit = reg;
		pr_debug("[Battery] %s: %x\n",
			__func__, pinfo->chg1_data.thermal_input_current_limit);
	}
	return size;
}
static DEVICE_ATTR(input_current, 0644, show_input_current,
		store_input_current);

static ssize_t show_chg1_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg1_data.thermal_charging_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg1_data.thermal_charging_current_limit);
}

static ssize_t store_chg1_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg1_data.thermal_charging_current_limit = reg;
		pr_debug("[Battery] %s: %x\n", __func__,
			pinfo->chg1_data.thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg1_current, 0644, show_chg1_current, store_chg1_current);

static ssize_t show_chg2_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg2_data.thermal_charging_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg2_data.thermal_charging_current_limit);
}

static ssize_t store_chg2_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg2_data.thermal_charging_current_limit = reg;
		pr_debug("[Battery] %s: %x\n", __func__,
			pinfo->chg2_data.thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg2_current, 0644, show_chg2_current, store_chg2_current);

static ssize_t show_ADC_Charger_Voltage(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int vbus = 5000; //battery_get_vbus();

	if (!atomic_read(&pinfo->enable_kpoc_shdn) || vbus < 0) {
		chr_err("HardReset or get vbus failed, vbus:%d:5000\n", vbus);
		vbus = 5000;
	}

	pr_debug("[%s]: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR(ADC_Charger_Voltage, 0444, show_ADC_Charger_Voltage, NULL);

/* procfs */
static int cne_chg_current_cmd_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->usb_unlimited, pinfo->cmd_discharging);
	return 0;
}

static ssize_t cne_chg_current_cmd_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32] = {0};
	int current_unlimited = 0;
	int cmd_discharging = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &current_unlimited, &cmd_discharging) == 2) {
		info->usb_unlimited = current_unlimited;
		if (cmd_discharging == 1) {
			info->cmd_discharging = true;
			charger_dev_enable(info->chg1_dev, false);
			charger_dev_enable(info->chg2_dev, false);
			charger_manager_notifier(info,
						CHARGER_NOTIFY_STOP_CHARGING);
		} else if (cmd_discharging == 0) {
			info->cmd_discharging = false;
			charger_dev_enable(info->chg1_dev, true);
			charger_manager_notifier(info,
						CHARGER_NOTIFY_START_CHARGING);
		}

		pr_debug("%s current_unlimited=%d, cmd_discharging=%d\n",
			__func__, current_unlimited, cmd_discharging);
		return count;
	}

	chr_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

static int cne_chg_en_power_path_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(pinfo->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static ssize_t cne_chg_en_power_path_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(info->chg1_dev, enable);
		charger_dev_enable_powerpath(info->chg2_dev, enable);
		pr_debug("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	chr_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static int cne_chg_en_safety_timer_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;
	bool safety_timer_en = false;

	charger_dev_is_safety_timer_enabled(pinfo->chg1_dev, &safety_timer_en);
	seq_printf(m, "%d\n", safety_timer_en);

	return 0;
}

static ssize_t cne_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_safety_timer(info->chg1_dev, enable);
		charger_dev_enable_safety_timer(info->chg2_dev, enable);
		pr_debug("%s: enable safety timer = %d\n", __func__, enable);

		/* SW safety timer */
		if (info->sw_safety_timer_setting == true) {
			if (enable)
				info->enable_sw_safety_timer = true;
			else
				info->enable_sw_safety_timer = false;
		}

		return count;
	}

	chr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

static int cne_chg_enable_hiz_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;

	if (!pinfo)
		return -EINVAL;

	seq_printf(m, "%d\n", pinfo->enable_hiz);

	return 0;
}

static ssize_t cne_chg_enable_hiz_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int hiz = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &hiz);
	if (ret == 0) {
		info->enable_hiz = hiz;

		pr_info("enable hiz mode = %d\n", hiz);

		charger_dev_enable_hz(info->chg1_dev, hiz);
		charger_dev_enable_hz(info->chg2_dev, hiz);

		charger_manager_notifier(info,
						CHARGER_NOTIFY_START_CHARGING);

		_wake_up_charger(info);

		return count;
	}

	chr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

static int cne_chg_capacity_ctrl_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;

	if (!pinfo)
		return -EINVAL;

	seq_printf(m, "DemoMode:%d CapCtrl:%d\n", pinfo->is_demo_mode, pinfo->capacity_ctrl);

	return 0;
}

static ssize_t cne_chg_capacity_ctrl_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	int cap = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtoint(desc, 10, &cap);
	if (ret < 0) {
		pr_err("Demo mode parse data error!\n");
		info->is_demo_mode = false;
		info->capacity_ctrl = 0;

		return ret;
	}

	info->is_demo_mode = true;

	if(cap >= 100 || cap == 0) {
		info->is_demo_mode = false;
		info->capacity_ctrl = 100;
	} else if(cap <= 30)
		info->capacity_ctrl = 30;
	else
		info->capacity_ctrl = cap;

	pr_info("demo_mode:%d, capacity:(%d %d)\n", info->is_demo_mode, cap,
			info->capacity_ctrl);

	_wake_up_charger(info);

	return count;
}

static int cne_chg_vbus_show(struct seq_file *m, void *data)
{
	struct charger_manager *info = m->private;

	if (!info)
		return -EINVAL;

	if (!info->chg1_dev)
		seq_printf(m, "%d\n", -9999);
	else
		seq_printf(m, "%d\n", battery_get_vbus());

	return 0;
}

static int cne_chg_charger_state_show(struct seq_file *m, void *data)
{
	struct charger_manager *info = m->private;
	bool state1 = false, state2 = false;

	if (!info)
		return -EINVAL;

	if (info->chg1_dev)
		charger_dev_is_enabled(info->chg1_dev, &state1);

	if (info->chg2_dev)
		charger_dev_is_enabled(info->chg2_dev, &state2);

	state1 = !!state1;
	state2 = !!state2;

	/* bit4 show: chg1 enable
	 * bit0 show: chg2 enable
	 */
	seq_printf(m, "0x%02x\n", (state1 << 4) | state2);

	return 0;
}


static int cne_chg_aging_enable_show(struct seq_file *m, void *data)
{
	struct charger_manager *info = m->private;
	seq_printf(m, "%d\n", info->aging_enable);

	return 0;
}

static ssize_t cne_chg_aging_enable_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		if (info->aging_enable == enable) {
			pr_info("enable aging repeat setting %d, skip!\n", enable);
			return count;
		}
		info->aging_enable = enable;
		pr_err("Aging cap control %s!\n", enable ? "enable" : "disable");
		if (info->aging_enable) {
			cne_chg_check_cap_in_smt(info);
		} else {
			if (info->enable_hiz) {
				info->enable_hiz = 0;
				charger_dev_enable_hz(info->chg1_dev, info->enable_hiz);
				charger_dev_enable_hz(info->chg2_dev, info->enable_hiz);
			}
		}

		return count;
	}

	pr_err("bad argument, echo [enable] > en_safety_timer!\n");
	return count;
}

static int cne_chg_cap_range_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->cap_low, pinfo->cap_high);
	return 0;
}

static ssize_t cne_chg_cap_range_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32] = {0};
	int cap_low = 0;
	int cap_high = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &cap_low, &cap_high) == 2) {
		info->cap_low = cap_low;
		info->cap_high = cap_high;
		if (info->aging_enable)
			cne_chg_check_cap_in_smt(info);
		pr_err("%s cap_low=%d, cap_high=%d\n", __func__, cap_low, cap_high);
		return count;
	}

	chr_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

/* PROC_FOPS_RW(battery_cmd); */
/* PROC_FOPS_RW(discharging_cmd); */
PROC_FOPS_RW(current_cmd);
PROC_FOPS_RW(en_power_path);
PROC_FOPS_RW(en_safety_timer);
PROC_FOPS_RW(enable_hiz);
PROC_FOPS_RW(capacity_ctrl);
PROC_FOPS_RO(vbus);
PROC_FOPS_RO(charger_state);
PROC_FOPS_RW(aging_enable);
PROC_FOPS_RW(cap_range);

/* Create sysfs and procfs attributes */
static int cne_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
	struct proc_dir_entry *battery_dir = NULL;
	struct charger_manager *info = platform_get_drvdata(pdev);
	/* struct charger_device *chg_dev; */

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_jeita);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_log_level);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_pdc_max_watt);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charger_Voltage);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_input_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chg1_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chg2_current);
	if (ret)
		goto _out;

	battery_dir = proc_mkdir("battery_cmd", NULL);
	if (!battery_dir) {
		chr_err("[%s]: mkdir /proc/battery_cmd failed\n", __func__);
		return -ENOMEM;
	}

	proc_create_data("current_cmd", 0640, battery_dir,
			&cne_chg_current_cmd_fops, info);
	proc_create_data("en_power_path", 0640, battery_dir,
			&cne_chg_en_power_path_fops, info);
	proc_create_data("en_safety_timer", 0640, battery_dir,
			&cne_chg_en_safety_timer_fops, info);
	proc_create_data("enable_hiz", 0640, battery_dir,
			&cne_chg_enable_hiz_fops, info);
	proc_create_data("capacity_ctrl", 0640, battery_dir,
			&cne_chg_capacity_ctrl_fops, info);
	proc_create_data("cap_range", 0640, battery_dir,
			&cne_chg_cap_range_fops, info);
	proc_create_data("aging_enable", 0640, battery_dir,
			&cne_chg_aging_enable_fops, info);

_out:
	return ret;
}

void notify_adapter_event(enum adapter_type type, enum adapter_event evt,
	void *val)
{
	chr_err("%s %d %d\n", __func__, type, evt);
	switch (type) {
	case CNE_PD_ADAPTER:
		switch (evt) {
		case CNE_PD_CONNECT_NONE:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify Detach\n");
			pinfo->pd_type = CNE_PD_CONNECT_NONE;
			mutex_unlock(&pinfo->charger_pd_lock);
			/* reset PE40 */
			break;

		case CNE_PD_CONNECT_HARD_RESET:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify HardReset\n");
			pinfo->pd_type = CNE_PD_CONNECT_NONE;
			pinfo->pd_reset = true;
			mutex_unlock(&pinfo->charger_pd_lock);
			_wake_up_charger(pinfo);
			/* reset PE40 */
			break;

		case CNE_PD_CONNECT_PE_READY_SNK:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify fixe voltage ready\n");
			pinfo->pd_type = CNE_PD_CONNECT_PE_READY_SNK;
			mutex_unlock(&pinfo->charger_pd_lock);
			_wake_up_charger(pinfo);
			/* PD is ready */
			break;

		case CNE_PD_CONNECT_PE_READY_SNK_PD30:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify PD30 ready\r\n");
			pinfo->pd_type = CNE_PD_CONNECT_PE_READY_SNK_PD30;
			mutex_unlock(&pinfo->charger_pd_lock);
			_wake_up_charger(pinfo);
			/* PD30 is ready */
			break;

		case CNE_PD_CONNECT_PE_READY_SNK_APDO:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify APDO Ready\n");
			pinfo->pd_type = CNE_PD_CONNECT_PE_READY_SNK_APDO;
			mutex_unlock(&pinfo->charger_pd_lock);
			/* PE40 is ready */
			_wake_up_charger(pinfo);
			break;

		case CNE_PD_CONNECT_TYPEC_ONLY_SNK:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify Type-C Ready\n");
			pinfo->pd_type = CNE_PD_CONNECT_TYPEC_ONLY_SNK;
			mutex_unlock(&pinfo->charger_pd_lock);
			/* type C is ready */
			_wake_up_charger(pinfo);
			break;
		case CNE_TYPEC_WD_STATUS:
			chr_err("wd status = %d\n", *(bool *)val);
			mutex_lock(&pinfo->charger_pd_lock);
			pinfo->water_detected = *(bool *)val;
			mutex_unlock(&pinfo->charger_pd_lock);

			if (pinfo->water_detected == true)
				pinfo->notify_code |= CHG_TYPEC_WD_STATUS;
			else
				pinfo->notify_code &= ~CHG_TYPEC_WD_STATUS;
			// cne_chgstat_notify(pinfo);
			break;
		case CNE_TYPEC_HRESET_STATUS:
			chr_err("hreset status = %d\n", *(bool *)val);
			mutex_lock(&pinfo->charger_pd_lock);
			if (*(bool *)val)
				atomic_set(&pinfo->enable_kpoc_shdn, 1);
			else
				atomic_set(&pinfo->enable_kpoc_shdn, 0);
			mutex_unlock(&pinfo->charger_pd_lock);
			break;
		case CNE_TYPEC_VBUS_STATUS:
			_wake_up_charger(pinfo);
			break;
		};
	}
}
EXPORT_SYMBOL(notify_adapter_event);

static int proc_dump_log_show(struct seq_file *m, void *v)
{
	struct adapter_power_cap cap;
	int i;

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	if (pinfo->pd_type == CNE_PD_CONNECT_PE_READY_SNK_APDO) {
		seq_puts(m, "********** PD APDO cap Dump **********\n");

		adapter_dev_get_cap(pinfo->pd_adapter, CNE_PD_APDO, &cap);
		for (i = 0; i < cap.nr; i++) {
			seq_printf(m,
			"%d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n", i,
			cap.max_mv[i], cap.min_mv[i], cap.ma[i],
			cap.type[i], cap.pwr_limit[i], cap.pdp);
		}
	} else if (pinfo->pd_type == CNE_PD_CONNECT_PE_READY_SNK
		|| pinfo->pd_type == CNE_PD_CONNECT_PE_READY_SNK_PD30) {
		seq_puts(m, "********** PD cap Dump **********\n");

		adapter_dev_get_cap(pinfo->pd_adapter, CNE_PD, &cap);
		for (i = 0; i < cap.nr; i++) {
			seq_printf(m, "%d: mV:%d,%d mA:%d type:%d\n", i,
			cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
		}
	}

	return 0;
}

static ssize_t proc_write(
	struct file *file, const char __user *buffer,
	size_t count, loff_t *f_pos)
{
	return count;
}


static int proc_dump_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_dump_log_show, NULL);
}

static const struct proc_ops charger_dump_log_proc_fops = {
	.proc_open = proc_dump_log_open,
	.proc_read = seq_read,
	.proc_lseek	= seq_lseek,
	.proc_write = proc_write,
};

static int cne_chg_usb_type_show(struct seq_file *m, void *data)
{
	struct charger_manager *info = m->private;
	char usb_tye_name[32]="[unknown]";
	enum charger_type chr_type;

	if (!info)
		return -EINVAL;

	chr_type = get_charger_type(info);
	memset(usb_tye_name, 0, sizeof(usb_tye_name));
	if (chr_type == CHARGER_UNKNOWN)
		memcpy(usb_tye_name, "[Unknown]",
			strlen("[Unknown]") > (sizeof(usb_tye_name) - 1) ?
			(sizeof(usb_tye_name) - 1) : strlen("[Unknown]"));
	else if ((chr_type == STANDARD_HOST) ||
				(chr_type == NONSTANDARD_CHARGER))
		memcpy(usb_tye_name, "[SDP]",
			strlen("[SDP]") > (sizeof(usb_tye_name) - 1) ?
			(sizeof(usb_tye_name) - 1) : strlen("[SDP]"));
	else if (chr_type == CHARGING_HOST)
		memcpy(usb_tye_name, "[CDP]",
			strlen("[CDP]") > (sizeof(usb_tye_name) - 1) ?
			(sizeof(usb_tye_name) - 1) : strlen("[CDP]"));
	else if (chr_type == STANDARD_CHARGER)
		memcpy(usb_tye_name, "[DCP]",
			strlen("[DCP]") > (sizeof(usb_tye_name) - 1) ?
			(sizeof(usb_tye_name) - 1) : strlen("[DCP]"));
	else if (chr_type == PD_CHARGER)
		memcpy(usb_tye_name, "[PD]",
			strlen("[PD]") > (sizeof(usb_tye_name) - 1) ?
			(sizeof(usb_tye_name) - 1) : strlen("[PD]"));
	else
		memcpy(usb_tye_name, "[DCP]",
			strlen("[DCP]") > (sizeof(usb_tye_name) - 1) ?
			(sizeof(usb_tye_name) - 1) : strlen("[DCP]"));

	seq_printf(m, "%s\n", usb_tye_name);

	return 0;
}
PROC_FOPS_RO(usb_type);

void charger_debug_init(struct charger_manager *info)
{
	struct proc_dir_entry *charger_dir;

	charger_dir = proc_mkdir("charger", NULL);
	if (!charger_dir) {
		chr_err("fail to mkdir /proc/charger\n");
		return;
	}

	proc_create("dump_log", 0640,
		charger_dir, &charger_dump_log_proc_fops);
	proc_create_data("vbus", 0440,
		charger_dir, &cne_chg_vbus_fops, info);
	proc_create_data("charger_state", 0440,
		charger_dir, &cne_chg_charger_state_fops, info);
	proc_create_data("usb_type", 0440,
		charger_dir, &cne_chg_usb_type_fops, info);
}

int adapter_class_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, adp_cls_nb);
	struct adapter_device *adp_dev = (struct adapter_device *)v;

	pr_info("++ event:%lu charger-name:%s\n", event, dev_name(&adp_dev->dev));

	if (event == ADAPTER_EVENT_PROP_CHANGED) {
		if (strcmp(dev_name(&adp_dev->dev), "pd_adapter") == 0) {
			pr_info("++ pd_adapter found\n");
			info->pd_adapter = adp_dev;
			_wake_up_charger(info);
		} else if(strcmp(dev_name(&adp_dev->dev), "typec_adapter") == 0) {
			pr_info("++ typec_adapter found\n");
			info->typec_adapter = adp_dev;
			_wake_up_charger(info);
		}
	}

	return 0;
}

static int charger_iio_write_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int val, int val2,
			 long mask)
{
	struct charger_manager *info = iio_priv(indio_dev);
	int channel;

	channel = chan->channel;

	return charger_iio_set_prop(info, channel, val);
}

static int charger_iio_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct charger_manager *info = iio_priv(indio_dev);
	int channel;

	channel = chan->channel;

	return charger_iio_get_prop(info, channel, val);
}

static int charger_of_xlate(struct iio_dev *indio_dev,
				const struct of_phandle_args *iiospec)
{
	struct charger_manager *info = iio_priv(indio_dev);
	int i;
	struct iio_chan_spec *iio_chan = info->iio_chan_ids;

	for (i = 0; i < info->nchannels; i++) {
		if (iio_chan->channel == iiospec->args[0])
			return i;
		iio_chan++;
	}

	return -EINVAL;
}

static const struct iio_info charger_iio_info = {
	.read_raw = charger_iio_read_raw,
	.write_raw = charger_iio_write_raw,
	.of_xlate = charger_of_xlate,
};

static int cne_charger_probe(struct platform_device *pdev)
{
	struct charger_manager *info = NULL;
	struct list_head *pos = NULL;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr = NULL;
	struct iio_dev *indio_dev;
	int ret = 0;

	chr_err("%s: starts\n", __func__);

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);
	pinfo = info;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	info->nchannels = ARRAY_SIZE(cne_iio_chans);
	ret = cne_iio_device_init(info, indio_dev,
			cne_iio_chans, &charger_iio_info);
	if (ret < 0) {
		chr_err("%s: iio probe fail\n", __func__);
		return ret;
	}
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = info->iio_chan_ids;
	indio_dev->num_channels = info->nchannels;

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret < 0) {
		chr_err("%s: iio device register fail\n", __func__);
		return ret;
	} else
		chr_err("%s: iio device register success\n", __func__);

	mutex_init(&info->charger_lock);
	mutex_init(&info->charger_pd_lock);
	mutex_init(&info->cable_out_lock);
	atomic_set(&info->enable_kpoc_shdn, 1);
	spin_lock_init(&info->slock);
	plug_wakelock =	wakeup_source_register(NULL, "plug wakelock");

	/* init thread */
	init_waitqueue_head(&info->wait_que);
	info->polling_interval = CHARGING_INTERVAL;
	info->enable_dynamic_cv = true;

	info->pd_voltage_min = MICRO_5V;
	info->pd_voltage_max = MICRO_5V;
	info->pd_active = false;
	info->pd_usb_suspend_supported = false;
	info->pd_hard_reset = false;

	info->chg1_data.thermal_charging_current_limit = -1;
	info->chg1_data.thermal_input_current_limit = -1;
	info->chg1_data.user_input_current_limit = -1;
	info->chg1_data.user_charging_current_limit = -1;
	info->chg1_data.input_current_limit_by_aicl = -1;
	info->chg2_data.thermal_charging_current_limit = -1;
	info->chg2_data.thermal_input_current_limit = -1;
	info->chg2_data.user_input_current_limit = -1;
	info->chg2_data.user_charging_current_limit = -1;
	info->sw_jeita.error_recovery_flag = true;

	/* Config the default state for aging cap control */
	info->aging_enable = 0;
	info->cap_low = 70;
	info->cap_high = 80;

#ifdef CONFIG_CNE_CHARGER_UNLIMITED
	info->usb_unlimited = true;
	info->enable_sw_safety_timer = false;
	charger_dev_enable_safety_timer(info->chg1_dev, false);
#endif
	INIT_WORK(&info->battery_info_change_work, battery_info_change_work);
	srcu_init_notifier_head(&info->evt_nh);
	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		ptr->cm = info;
		if (ptr->pnb != NULL) {
			srcu_notifier_chain_register(&info->evt_nh, ptr->pnb);
			ptr->pnb = NULL;
		}
	}
	mutex_unlock(&consumer_mutex);

	if (info->chg1_dev != NULL)
		charger_dev_set_drvdata(info->chg1_dev, info);

	info->charger_wakelock = wakeup_source_register(NULL, "charger suspend wakelock");
	if(!info->charger_wakelock) {
		chr_err("%s: request wakelock error.\n", __func__);
		goto err_wakelock;
	}

	cne_charger_init_timer(info);
	cne_charger_parse_dt(info, &pdev->dev);

	info->chg1_consumer =
		charger_manager_get_by_name(&pdev->dev, "charger_port1");
#ifndef CONFIG_QGKI
	register_charger_manager_psy(info);
#endif
	kthread_run(charger_routine_thread, info, "charger_thread");

	// register battery psy adaptor notify
	info->psy_nb.notifier_call = charger_psy_event;
	power_supply_reg_notifier(&info->psy_nb);

	// register typec/pd adaptor notify
	info->adp_cls_nb.notifier_call = adapter_class_event;
	adapter_class_reg_notifier(&info->adp_cls_nb);
	info->pd_adapter = get_adapter_by_name("pd_adapter");
	if (info->pd_adapter)
		chr_err("Found PD adapter!\n");
	else
		chr_err("*** Error : can't find PD adapter ***\n");

	info->typec_adapter = get_adapter_by_name("typec_adapter");
	if (info->typec_adapter)
		chr_err("Found Type-C adapter!\n");
	else
		chr_err("*** Error : can't find Type-C adapter ***\n");

	cne_pdc_init(info);
	charger_ftm_init();
	cne_charger_get_atm_mode(info);
	charger_get_power_off_mode(info);
	charger_get_demo_mode(info);
	sw_jeita_state_machine_init(info);

	ret = cne_charger_setup_files(pdev);
	if (ret)
		chr_err("Error creating sysfs interface\n");
	charger_debug_init(info);

	info->init_done = true;

	/*
	 * When register psy, psy change is called with a delay
	 * The notify callback function may not be called
	 * because system is sleeping.
	 */
	if (info->manager_psy)
		power_supply_changed(info->manager_psy);

	info->batt_psy = power_supply_get_by_name("battery");
	if (info->batt_psy)
		_wake_up_charger(info);

	chr_err("probe success\n");

	return 0;

err_wakelock:
	chr_err("probe fail\n");
	pinfo = NULL;
	kfree(info);

	return ret;
}

static int cne_charger_remove(struct platform_device *dev)
{
	struct charger_manager *info = platform_get_drvdata(dev);

	wakeup_source_unregister(info->charger_wakelock);
	wakeup_source_unregister(plug_wakelock);
	cancel_work_sync(&info->battery_info_change_work);

	return 0;
}

static void cne_charger_shutdown(struct platform_device *dev)
{
	struct charger_manager *info = platform_get_drvdata(dev);
#if 0
	int ret;

	if (cne_pe20_get_is_connect(info) || cne_pe_get_is_connect(info)) {
		if (info->chg2_dev)
			charger_dev_enable(info->chg2_dev, false);
		ret = cne_pe20_reset_ta_vchr(info);
		if (ret == -ENOTSUPP)
			cne_pe_reset_ta_vchr(info);
		pr_debug("%s: reset TA before shutdown\n", __func__);
	}
#endif
	pr_err("cne_charger_shutdown!\n");
	alarm_cancel(&info->charger_timer);
}

static const struct of_device_id cne_charger_of_match[] = {
	{.compatible = "chinoe,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, cne_charger_of_match);

struct platform_device charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver charger_driver = {
	.probe = cne_charger_probe,
	.remove = cne_charger_remove,
	.shutdown = cne_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = cne_charger_of_match,
	},
};

static int __init cne_charger_init(void)
{
	pr_info("++ cne_charger_init\n");
	return platform_driver_register(&charger_driver);
}
late_initcall(cne_charger_init);

static void __exit cne_charger_exit(void)
{
	pr_info("++ cne_charger_exit\n");
	platform_driver_unregister(&charger_driver);
}
module_exit(cne_charger_exit);


MODULE_AUTHOR("chino-e <chino-e@chino-e.com>");
MODULE_DESCRIPTION("CNCE Charger Driver");
MODULE_LICENSE("GPL");
