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

#define pr_fmt(fmt)	"[cne-charge][chg_type_det] %s: " fmt, __func__

#include <generated/autoconf.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/seq_file.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/of.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/regulator/driver.h>
#include <linux/iio/consumer.h>
#include "charger_type.h"
#include "cne_charger_intf.h"
#include "charger_class.h"
#include "adapter_class.h"

#ifdef CONFIG_TCPC_CLASS
#include <tcpm.h>
#include <tcpci_core.h>
#endif
#define USE_EXTCON_USB_CHG

#ifdef CONFIG_QGKI
void __attribute__((weak)) cne_charger_int_handler(void)
{
	pr_notice("%s not defined\n", __func__);
}

void __attribute__((weak)) cne_charger_is_doing_hvdcp_detect(bool doing)
{
	pr_notice("%s not defined\n", __func__);
}
#endif

#define CHG_TYPE_WAKE_LOCK_TIMEOUT 4000

struct chg_type_info {
	struct device *dev;
	struct charger_consumer *chg_consumer;
	struct tcpc_device *tcpc_dev;
	struct notifier_block pd_nb;
	/* Charger Detection */
	struct mutex chgdet_lock;
	bool chgdet_en;
	bool pd_active;
	atomic_t chgdet_cnt;
	wait_queue_head_t waitq;
	struct task_struct *chgdet_task;
	struct workqueue_struct *detect_doing_wq;
	struct work_struct detect_doing_work;
	struct workqueue_struct *chg_in_wq;
	struct work_struct chg_in_work;
	bool ignore_usb;
	bool plugin;
	struct regulator	*dpdm_reg;
	struct mutex		dpdm_lock;
	bool				dpdm_enabled;
	bool				is_doing_hvdcp_detect;
	struct delayed_work	hvdcp_detect_work;
	struct notifier_block tcpc_cls_nb;
	int                 tcpc_init_state;
	uint8_t				wake_lock_typec;
	struct delayed_work	tcpc_init_delay_work;
	struct notifier_block chg_cls_nb;
	struct charger_device *chg1_dev;
	struct charger_device *chg2_dev;
	struct wakeup_source *attach_wake_lock;
	struct wakeup_source *detach_wake_lock;
#ifndef CONFIG_QGKI
	struct power_supply *manager_psy;
	struct notifier_block psy_nb;
#endif
#if defined(USE_EXTCON_USB_CHG)
	struct extcon_dev	*extcon;
	int                 tcpc_mode;
	struct notifier_block adp_cls_nb;
	struct adapter_device *typec_adapter;
	struct work_struct typec_adapter_change_work;
#endif
};

enum iio_psy_property {
	PSY_IIO_PROP_CHARGE_CONTROL_LIMIT_MAX = 0,
	PSY_IIO_PROP_CHARGE_CONTROL_LIMIT,
	PSY_IIO_PD_CURRENT_MAX,
	PSY_IIO_PROP_MAX,
};

static const char * const iio_channel_map[] = {
	"charge_control_limit_max",
	"charge_control_limit",
	"current_max",
};

/* EVB / Phone */
static const char * const cne_chg_type_name[] = {
	"Charger Unknown",
	"Standard USB Host",
	"Charging USB Host",
	"Non-standard Charger",
	"Standard Charger",
	"QC2.0 Charger",
	"Power Delivery Charger",
	"Apple 2.1A Charger",
	"Apple 1.0A Charger",
	"Apple 0.5A Charger",
	"Wireless Charger",
};

static void dump_charger_name(enum charger_type type)
{
	switch (type) {
	case CHARGER_UNKNOWN:
	case STANDARD_HOST:
	case CHARGING_HOST:
	case NONSTANDARD_CHARGER:
	case STANDARD_CHARGER:
	case HVDCP_QC20_CHARGER:
	case PD_CHARGER:
	case APPLE_2_1A_CHARGER:
	case APPLE_1_0A_CHARGER:
	case APPLE_0_5A_CHARGER:
	case WIRELESS_CHARGER:
		pr_info("charger type: %d, %s\n", type,
			cne_chg_type_name[type]);
		break;
	default:
		pr_info("charger type: %d, Not Defined!!!\n",
			type);
		break;
	}
}

/* Power Supply */
struct cne_charger {
	struct device *dev;
	struct power_supply_desc chg_desc;
	struct power_supply_config chg_cfg;
	struct power_supply *chg_psy;
	struct power_supply_desc ac_desc;
	struct power_supply_config ac_cfg;
	struct power_supply *ac_psy;
	struct power_supply_desc usb_desc;
	struct power_supply_config usb_cfg;
	struct power_supply *usb_psy;
	struct iio_channel **iio_channels;
	struct chg_type_info *cti;
	bool chg_online; /* Has charger in or not */
	enum charger_type chg_type;
};

static int smblib_get_prop(struct cne_charger *cne_chg,
			   enum iio_psy_property ipp,
			   union power_supply_propval *val)
{
	struct iio_channel *channel;
	int ret = 0, value = 0;

	if (!cne_chg->iio_channels[ipp]) {
		channel = devm_iio_channel_get(cne_chg->dev,
			iio_channel_map[ipp]);
		if (IS_ERR(channel)) {
			pr_err("get iio channel (ipp = %d) fail\n", ipp);
			ret = PTR_ERR(channel);
			return ret;
		}

		cne_chg->iio_channels[ipp] = channel;
	}

	ret = iio_read_channel_processed(cne_chg->iio_channels[ipp], &value);
	if (ret < 0) {
		pr_err("fail(%d), ipp = %d\n", ret, ipp);
		return ret;
	}

	val->intval = value;
	return 0;
}

static int smblib_set_prop(struct cne_charger *cne_chg,
			   enum iio_psy_property ipp,
			   const union power_supply_propval *val)
{
	struct iio_channel *channel;
	int ret = 0;

	if (!cne_chg->iio_channels[ipp]) {
		channel = devm_iio_channel_get(cne_chg->dev,
			iio_channel_map[ipp]);
		if (IS_ERR(channel)) {
			pr_err("get iio channel (ipp = %d) fail\n", ipp);
			ret = PTR_ERR(channel);
			return ret;
		}

		cne_chg->iio_channels[ipp] = channel;
	}

	ret = iio_write_channel_raw(cne_chg->iio_channels[ipp], val->intval);
	if (ret < 0) {
		pr_err("fail(%d), ipp = %d\n", ret, ipp);
		return ret;
	}

	return 0;
}

static int cne_charger_online(struct cne_charger *cne_chg)
{
	int ret = 0;

	pr_info("chg_online=%d\n", cne_chg->chg_online);

	return ret;
}

static void start_wake_charger(struct chg_type_info *cti)
{
	pr_err("Wake up charger\n");
	mutex_lock(&cti->chgdet_lock);
	atomic_inc(&cti->chgdet_cnt);
	wake_up_interruptible(&cti->waitq);
	mutex_unlock(&cti->chgdet_lock);
}

int set_wake_lock(struct chg_type_info *cti, bool wake_lock_typec)
{
	bool ori_lock, new_lock;

	if (cti->wake_lock_typec)
		ori_lock = true;
	else
		ori_lock = false;

	if (wake_lock_typec)
		new_lock = true;
	else
		new_lock = false;

	if (new_lock != ori_lock) {
		if (new_lock) {
			pr_info("attach wake_lock=1\n");
			__pm_wakeup_event(cti->attach_wake_lock, 5000);
		} else {
			pr_info("attach wake_lock=0\n");
			__pm_relax(cti->attach_wake_lock);
		}
		return 1;
	}

	return 0;
}

static int set_wake_lock_typec(struct chg_type_info *cti, bool typec_lock)
{
	uint8_t wake_lock_typec;

	wake_lock_typec = cti->wake_lock_typec;

	if (typec_lock)
		wake_lock_typec++;
	else if (wake_lock_typec > 0)
		wake_lock_typec--;

	if (wake_lock_typec == 0) {
		pr_info("dettach wake_lock=1\n");
		__pm_wakeup_event(cti->detach_wake_lock, 5000);
	}

	set_wake_lock(cti, wake_lock_typec);

	if (wake_lock_typec == 1) {
		pr_info("dettach wake_lock=0\n");
		__pm_relax(cti->detach_wake_lock);
	}

	cti->wake_lock_typec = wake_lock_typec;
	return 0;
}

/*
static void pd_plug_in_out_handler(struct chg_type_info *cti, bool en)
{
	mutex_lock(&cti->chgdet_lock);
	cti->pd_active = en;
	atomic_inc(&cti->chgdet_cnt);
	wake_up_interruptible(&cti->waitq);
	mutex_unlock(&cti->chgdet_lock);
}*/

static void plug_in_out_handler(struct chg_type_info *cti, bool en, bool ignore)
{
	mutex_lock(&cti->chgdet_lock);
	cti->chgdet_en = en;
	cti->ignore_usb = ignore;
	cti->plugin = en;
	atomic_inc(&cti->chgdet_cnt);
	wake_up_interruptible(&cti->waitq);
	mutex_unlock(&cti->chgdet_lock);
}

#if defined(USE_EXTCON_USB_CHG)
/*********************************************************************************
 *
 * Enable USB EXTCON
 *
 ********************************************************************************/
static int cne_charger_get_prop_typec_cc_orientation(struct chg_type_info *cti) {
	int typec_cc = 0;

	if (cti->typec_adapter) {
		typec_cc = adapter_dev_get_property(cti->typec_adapter, TYPE_CC_ORIENTATION);
		pr_info("get init CC dir %d!\n", typec_cc);
	}

	return typec_cc;
}

static const unsigned int cne_charger_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static int cne_charger_extcon_init(struct chg_type_info *cti)
{
	int ret = 0;

	/*
	 * associate extcon with the dev as it could have a DT
	 * node which will be useful for extcon_get_edev_by_phandle()
	 */
	cti->extcon = devm_extcon_dev_allocate(cti->dev, cne_charger_extcon_cable);
	if (IS_ERR(cti->extcon)) {
		ret = PTR_ERR(cti->extcon);
		pr_err("%s: extcon dev alloc fail(%d)\n", __func__, ret);
		goto out;
	}

	ret = devm_extcon_dev_register(cti->dev, cti->extcon);
	if (ret) {
		pr_err("%s: extcon dev reg fail(%d)\n", __func__, ret);
		goto out;
	}

	/* Support reporting polarity and speed via properties */
	extcon_set_property_capability(cti->extcon, EXTCON_USB,
				       EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(cti->extcon, EXTCON_USB,
				       EXTCON_PROP_USB_SS);
	extcon_set_property_capability(cti->extcon, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(cti->extcon, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_SS);
out:
	return ret;
}

static inline void cne_charger_stop_usb_host(struct chg_type_info *cti)
{
	extcon_set_state_sync(cti->extcon, EXTCON_USB_HOST, false);
}

static inline void cne_charger_start_usb_host(struct chg_type_info *cti)
{
	union extcon_property_value val = {.intval = 0};

	val.intval = cne_charger_get_prop_typec_cc_orientation(cti);
	extcon_set_property(cti->extcon, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_TYPEC_POLARITY, val);

	val.intval = 1;
	extcon_set_property(cti->extcon, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_SS, val);

	extcon_set_state_sync(cti->extcon, EXTCON_USB_HOST, true);
}

static inline void cne_charger_stop_usb_peripheral(struct chg_type_info *cti)
{
	extcon_set_state_sync(cti->extcon, EXTCON_USB, false);
}

static inline void cne_charger_start_usb_peripheral(struct chg_type_info *cti)
{
	union extcon_property_value val = {.intval = 0};

	val.intval = cne_charger_get_prop_typec_cc_orientation(cti);
	extcon_set_property(cti->extcon, EXTCON_USB,
			    EXTCON_PROP_USB_TYPEC_POLARITY, val);

	val.intval = 1;
	extcon_set_property(cti->extcon, EXTCON_USB, EXTCON_PROP_USB_SS, val);

	extcon_set_state_sync(cti->extcon, EXTCON_USB, true);
}

static void cne_charger_config(struct chg_type_info *cti, int config)
{
	struct charger_device *chg_dev = NULL;
	int vbus = 0;

	if (!cti) {
		pr_err("chg info is NULL!\n");
		return;
	}

	// Get charger device
	chg_dev = get_charger_by_name("primary_chg");
	if (chg_dev)
		pr_err("Found primary charger [%s]\n", chg_dev->props.alias_name);
	else
		pr_err("*** Error : can't find primary charger ***\n");

	// Check CC mode: SNK-1,SRC-2,UNATTACHED-0
	if ((config == 1) && (cti->tcpc_mode == 0)) {
		pr_err("The charger plug in!\n");
		mutex_lock(&cti->chgdet_lock);
		cti->tcpc_mode = config;
		set_wake_lock_typec(cti, true);
		mutex_unlock(&cti->chgdet_lock);
		plug_in_out_handler(cti, true, false);
	} else if ((config == 0) && (cti->tcpc_mode == 1)) {
		pr_err("The charger plug out!\n");
		mutex_lock(&cti->chgdet_lock);
		cti->tcpc_mode = config;
		set_wake_lock_typec(cti, false);
		mutex_unlock(&cti->chgdet_lock);
		plug_in_out_handler(cti, false, false);
		//stop usb device mode
		cne_charger_stop_usb_peripheral(cti);
	} else if ((config == 2) && (cti->tcpc_mode == 0)) {
		pr_err("The OTG device plug in!\n");
		mutex_lock(&cti->chgdet_lock);
		cti->tcpc_mode = config;
		set_wake_lock_typec(cti, true);
		mutex_unlock(&cti->chgdet_lock);
		// Enable OTG device
		if (chg_dev) {
			charger_dev_enable_otg(chg_dev, true);
			charger_dev_set_boost_current_limit(chg_dev, 1500000);
			charger_dev_kick_wdt(chg_dev);
			cne_charger_start_usb_host(cti);
		}
	} else if ((config == 0) && (cti->tcpc_mode == 2)) {
		pr_err("The OTG device plug out!\n");
		mutex_lock(&cti->chgdet_lock);
		cti->tcpc_mode = config;
		set_wake_lock_typec(cti, false);
		mutex_unlock(&cti->chgdet_lock);
		// Disable OTG device
		if (chg_dev) {
			cne_charger_stop_usb_host(cti);
			charger_dev_enable_otg(chg_dev, false);
		}
	} else if ((config == 2) && (cti->tcpc_mode == 1)) {
		pr_err("Source_to_Sink\n");
		mutex_lock(&cti->chgdet_lock);
		cti->tcpc_mode = config;
		set_wake_lock_typec(cti, true);
		mutex_unlock(&cti->chgdet_lock);
		plug_in_out_handler(cti, true, true);
	} else if ((config == 1) && (cti->tcpc_mode == 2)) {
		pr_err("Sink_to_Source\n");
		mutex_lock(&cti->chgdet_lock);
		cti->tcpc_mode = config;
		set_wake_lock_typec(cti, false);
		mutex_unlock(&cti->chgdet_lock);
		plug_in_out_handler(cti, false, true);
	} else {
		pr_err("check vbus for norp!\n");
		charger_dev_get_vbus(chg_dev, &vbus);
		if ((vbus >= 4400) && (cti->tcpc_mode == 0)) {
			pr_err("only vbus attach!\n");
			mutex_lock(&cti->chgdet_lock);
			cti->tcpc_mode = 1;
			set_wake_lock_typec(cti, true);
			mutex_unlock(&cti->chgdet_lock);
			plug_in_out_handler(cti, true, false);
		}
	}
}

/*********************************************************************************
 *
 * Enable ADAPTER CLASS
 *
 ********************************************************************************/
static void typec_adapter_change_work(struct work_struct *work) {
	struct chg_type_info *cti = container_of(work,
				struct chg_type_info, typec_adapter_change_work);
	int typec_mode = 0;

	typec_mode = adapter_dev_get_property(cti->typec_adapter, TYPE_CC_MODE);
	cne_charger_config(cti, typec_mode);
}

int cne_chg_det_adapter_class_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct chg_type_info *cti =
			container_of(nb, struct chg_type_info, adp_cls_nb);
	struct adapter_device *adp_dev = (struct adapter_device *)v;

	pr_err("++ event:%lu charger-name:%s\n", event, dev_name(&adp_dev->dev));
	if (event == ADAPTER_EVENT_PROP_CHANGED) {
		if (strcmp(dev_name(&adp_dev->dev), "typec_adapter") == 0) {
			pr_err("++ typec_adapter found\n");
			cti->typec_adapter = adp_dev;
			schedule_work(&cti->typec_adapter_change_work);
		}
	}

	return 0;
}
#endif

/* Power Supply Functions */
#ifndef CONFIG_QGKI
static int notify_charger_manager(struct chg_type_info *cti,
		enum cne_power_supply_property prop, int val)
{
	union power_supply_propval propval;
	int ret = 0;

	if (!cti) {
		pr_notice("no chg_type_info!\n");
		return -EINVAL;
	}

	if (!cti->manager_psy) {
		cti->manager_psy = power_supply_get_by_name("charger-manager");
		if (!cti->manager_psy) {
			pr_err("charger-manager psy get fail\n");
			return -ENODEV;
		}
	}

	propval.intval = CNE_CPSP_TO_VAL(prop, val);
	ret = power_supply_set_property(cti->manager_psy,
				POWER_SUPPLY_PROP_STATUS, &propval);
	if (ret < 0)
		pr_err("start charger-manager hvdcp failed, ret = %d\n", ret);

	pr_info("prop:%d(0x%x) notify charger-manager success\n", prop, val);

	return ret;
}

static int set_charger_manager_config(struct chg_type_info *cti,
		enum cne_power_supply_property prop, int val)
{
	union power_supply_propval propval;
	int ret = 0;

	if (!cti) {
		pr_notice("no chg_type_info!\n");
		return -EINVAL;
	}

	if (!cti->manager_psy) {
		cti->manager_psy = power_supply_get_by_name("charger-manager");
		if (!cti->manager_psy) {
			pr_err("charger-manager psy get fail\n");
			return -ENODEV;
		}
	}

	if (prop == CNE_POWER_SUPPLY_SETTING_CHARGING_CURRENT) {
		propval.intval = val;
		ret = power_supply_set_property(cti->manager_psy,
				POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &propval);
	} else if (prop == CNE_POWER_SUPPLY_SETTING_INPUT_CURRENT) {
		propval.intval = val;
		ret = power_supply_set_property(cti->manager_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &propval);
	}

	return ret;
}

static int get_charger_manager_config(struct chg_type_info *cti,
		enum cne_power_supply_property prop, int* val)
{
	union power_supply_propval propval;
	int ret = 0;

	if (!cti) {
		pr_notice("no chg_type_info!\n");
		return -EINVAL;
	}

	if (!cti->manager_psy) {
		cti->manager_psy = power_supply_get_by_name("charger-manager");
		if (!cti->manager_psy) {
			pr_err("charger-manager psy get fail\n");
			return -ENODEV;
		}
	}

	if (prop == CNE_POWER_SUPPLY_SETTING_CHARGING_CURRENT) {
		ret = power_supply_get_property(cti->manager_psy,
				POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &propval);
	} else if (prop == CNE_POWER_SUPPLY_SETTING_INPUT_CURRENT) {
		ret = power_supply_get_property(cti->manager_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &propval);
	}
	*val = propval.intval;

	return ret;
}
#endif

static int cne_charger_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct cne_charger *cne_chg = power_supply_get_drvdata(psy);

	if (!cne_chg) {
		pr_notice("no charger data\n");
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		/* Force to 1 in all charger type */
		if (cne_chg->chg_type != CHARGER_UNKNOWN)
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = cne_chg->chg_type;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		smblib_get_prop(cne_chg,
			PSY_IIO_PROP_CHARGE_CONTROL_LIMIT, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		smblib_get_prop(cne_chg,
			PSY_IIO_PROP_CHARGE_CONTROL_LIMIT_MAX, val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cne_charger_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct cne_charger *cne_chg = power_supply_get_drvdata(psy);
	struct chg_type_info *cti = NULL;
	enum charger_type type;

	if (!cne_chg) {
		pr_notice("no charger data\n");
		return -EINVAL;
	}

	cti = cne_chg->cti;
	if (!cti) {
		pr_notice("no chg_type_info!\n");
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		cne_chg->chg_online = val->intval;
		cne_charger_online(cne_chg);
		return 0;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		type = val->intval;
		if (cne_chg->chg_type != type) {
			cne_chg->chg_type = type;
#if 0
#ifndef DUAL_85_VERSION
			if (cne_chg->chg_type == STANDARD_CHARGER) {
				if (cti->tcpc_init_state) {
					pr_info("+++++ hvdcp detect start +++++\n");
					cti->is_doing_hvdcp_detect = true;
					queue_work(cti->chg_in_wq, &cti->detect_doing_work);
					schedule_delayed_work(&cti->hvdcp_detect_work, 0);
				}
			}
#endif
#endif
#if defined(USE_EXTCON_USB_CHG)
			if ((cne_chg->chg_type == STANDARD_HOST) ||
					(cne_chg->chg_type == CHARGING_HOST)) {
				pr_info("+++++ Enable USB device mode +++++\n");
				cne_charger_start_usb_peripheral(cti);
			}
#endif
			dump_charger_name(cne_chg->chg_type);
			queue_work(cti->chg_in_wq, &cti->chg_in_work);
		}
		break;

	case POWER_SUPPLY_PROP_TYPE:
		pr_info("+++++ QUICK_CHARGE_TYPE +++++\n");
		if(val->intval == HVDCP_QC20_CHARGER)
			cne_chg->chg_type = val->intval;
		cti->is_doing_hvdcp_detect = false;
		queue_work(cti->chg_in_wq, &cti->detect_doing_work);

		dump_charger_name(cne_chg->chg_type);
		queue_work(cti->chg_in_wq, &cti->chg_in_work);
		break;

	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		smblib_set_prop(cne_chg,
			PSY_IIO_PROP_CHARGE_CONTROL_LIMIT, val);
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		pr_info("tcpc init state:%d\n", val->intval);
		if (!cti->tcpc_init_state)
			cti->tcpc_init_state = val->intval;
#ifndef DUAL_85_VERSION
		if (cti->tcpc_init_state && cne_chg->chg_type == STANDARD_CHARGER)
			schedule_delayed_work(&cti->tcpc_init_delay_work, msecs_to_jiffies(10000));
#endif
		break;

#if defined(USE_EXTCON_USB_CHG)
	case POWER_SUPPLY_PROP_USB_TYPE:
		pr_info("tcpc mode/vbus state:%d\n", val->intval);
		mutex_lock(&cti->chgdet_lock);
		if (val->intval == cti->tcpc_mode) {
			mutex_unlock(&cti->chgdet_lock);
			pr_info("tcpc mode/vbus has been set!\n");
			return 0;
		}
		mutex_unlock(&cti->chgdet_lock);
		cne_charger_config(cti, val->intval);
	break;
#endif

	default:
		return -EINVAL;
	}

	if (cne_chg->ac_psy)
		power_supply_changed(cne_chg->ac_psy);
	if (cne_chg->usb_psy)
		power_supply_changed(cne_chg->usb_psy);

	return 0;
}

static int cne_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct cne_charger *cne_chg = power_supply_get_drvdata(psy);
	int ret = 0;

	if (!cne_chg) {
		pr_notice("no charger data\n");
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		/* Force to 1 in all charger type */
		if (cne_chg->chg_type != CHARGER_UNKNOWN)
			val->intval = 1;
		/* Reset to 0 if charger type is USB */
		if ((cne_chg->chg_type == STANDARD_HOST) ||
			(cne_chg->chg_type == CHARGING_HOST))
			val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (cne_chg->chg_type ==  HVDCP_QC20_CHARGER)
			val->intval = 2000000;
		else if (cne_chg->chg_type == STANDARD_CHARGER)
			val->intval = 1500000;
		else if(cne_chg->chg_type == CHARGING_HOST)
			val->intval = 1500000;
		else if(cne_chg->chg_type == NONSTANDARD_CHARGER)
			val->intval = 1000000;
		else if(cne_chg->chg_type ==  PD_CHARGER) {
			ret = smblib_get_prop(cne_chg,
				PSY_IIO_PD_CURRENT_MAX, val);
			if (!ret && val->intval < 1000000)
				val->intval = 1000000;
		} else
			val->intval = 500000;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if ((cne_chg->chg_type ==  HVDCP_QC20_CHARGER) ||
			(cne_chg->chg_type ==  PD_CHARGER))
			val->intval = 9000000; 
		else
			val->intval = 5000000;
		break;

	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		if (!cne_chg->cti) {
			pr_err("cti is NULL, did not get the charging limit current!\n");
			return -EINVAL;
		}

#ifdef CONFIG_QGKI
		if (cne_chg->cti->chg_consumer)
			charger_manager_get_charging_current_limit(cne_chg->cti->chg_consumer,
					MAIN_CHARGER, &(val->intval));
		else
			pr_err("get charger-manager charging current failed, chg_consumer is null\n");
#else
		ret = get_charger_manager_config(cne_chg->cti,
					CNE_POWER_SUPPLY_SETTING_CHARGING_CURRENT, &(val->intval));
		if (ret < 0)
			pr_err("get charger-manager(get charging current)failed, ret = %d\n",
					ret);
#endif
		break;

#if 0
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!cne_chg->cti) {
			pr_err("cti is NULL, did not allow get the input current!\n");
			return -EINVAL;
		}

#ifdef CONFIG_QGKI
		if (cne_chg->cti->chg_consumer)
			charger_manager_get_input_current_limit(cne_chg->cti->chg_consumer,
					MAIN_CHARGER, &(val->intval));
		else
			pr_err("get charger-manager input current failed, chg_consumer is null\n");
#else
		ret = get_charger_manager_config(cne_chg->cti,
					CNE_POWER_SUPPLY_SETTING_INPUT_CURRENT, &(val->intval));
		if (ret < 0)
			pr_err("get charger-manager(set input current)failed, ret = %d\n",
					ret);
#endif
#endif
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int cne_ac_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct cne_charger *cne_chg = power_supply_get_drvdata(psy);
	struct chg_type_info *cti = NULL;
	int ret = 0;

	if (!cne_chg) {
		pr_notice("no charger data\n");
		return -EINVAL;
	}

	cti = cne_chg->cti;
	if (!cti) {
		pr_notice("no chg_type_info!\n");
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
#ifdef CONFIG_QGKI
		if (cti->chg_consumer)
			charger_manager_set_charging_current_limit(cti->chg_consumer,
					MAIN_CHARGER, val->intval);
		else
			pr_err("config user charging current failed, chg_consumer is null\n");
#else
		ret = set_charger_manager_config(cti,
					CNE_POWER_SUPPLY_SETTING_CHARGING_CURRENT, val->intval);
		if (ret < 0)
			pr_err("config charger-manager(set charging current)failed, ret = %d\n",
					ret);
#endif
		break;

#if 0
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
#ifdef CONFIG_QGKI
		if (cti->chg_consumer)
			charger_manager_set_input_current_limit(cti->chg_consumer,
					MAIN_CHARGER, val->intval);
		else
			pr_err("config user input current failed, chg_consumer is null\n");
#else
		ret = set_charger_manager_config(cti,
					CNE_POWER_SUPPLY_SETTING_INPUT_CURRENT, val->intval);
		if (ret < 0)
			pr_err("config charger-manager(set input current)failed, ret = %d\n",
					ret);
#endif
#endif
		break;

	default:
		return -EINVAL;
	}

	if (cne_chg->ac_psy)
		power_supply_changed(cne_chg->ac_psy);
	if (cne_chg->usb_psy)
		power_supply_changed(cne_chg->usb_psy);

	return 0;
}

static int cne_ac_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
#if 0
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
#endif
		return 1;
	default:
		break;
	}

	return 0;
}

static int cne_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct cne_charger *cne_chg = power_supply_get_drvdata(psy);

	if (!cne_chg) {
		pr_notice("no charger data\n");
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if ((cne_chg->chg_type == STANDARD_HOST) ||
			(cne_chg->chg_type == CHARGING_HOST))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 500000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 5000000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property cne_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property cne_ac_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
#if 0
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
#endif
};

static enum power_supply_property cne_usb_properties[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static void tcpc_init_delay_work_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_type_info *cti = container_of(dwork,
			struct chg_type_info, tcpc_init_delay_work);
	struct power_supply *psy = power_supply_get_by_name("charger");
	union power_supply_propval propval;
	bool attach = false;
	int ret = 0;

	if (!cti) {
		pr_err("no chg_type_info data\n");
		return;
	}

	if (!psy) {
		pr_err("get power supply failed\n");
		return;
	}

	mutex_lock(&cti->chgdet_lock);
	attach = cti->chgdet_en;
	mutex_unlock(&cti->chgdet_lock);

	ret = power_supply_get_property(psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);

#if 0
	if (attach && propval.intval == STANDARD_CHARGER) {
		pr_info("+++++ hvdcp detect start +++++\n");
		cti->is_doing_hvdcp_detect = true;
		queue_work(cti->chg_in_wq, &cti->detect_doing_work);
		schedule_delayed_work(&cti->hvdcp_detect_work, 0);
	}
#endif

	return;
}

static void hvdcp_detect_work_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_type_info *cti = container_of(dwork,
			struct chg_type_info, hvdcp_detect_work);
	int ret = 0;

	if (!cti) {
		ret = -ENODEV;
		pr_err("no chg_type_info data(%d)\n", ret);
		return;
	}

#ifdef CONFIG_QGKI
	if (cti->chg_consumer)
		charger_manager_enable_hvdcp_det(cti->chg_consumer,	true);
	else
		pr_err("chg_consumer is null\n");
#else
	ret = notify_charger_manager(cti, CNE_POWER_SUPPLY_ENABLE_HVDCP_DETECT, 1);
	if (ret < 0)
		pr_err("notify charger-manager(hvdcp detect)failed, ret = %d\n", ret);

#endif

	return;
}

static void detect_doing_work_handler(struct work_struct *work)
{
	struct chg_type_info *cti = container_of(work,
			struct chg_type_info, detect_doing_work);
	bool doing = false;
	int ret = 0;

	if (!cti) {
		ret = -ENODEV;
		pr_err("no chg_type_info data(%d)\n", ret);
		return;
	}

	doing = cti->is_doing_hvdcp_detect;
#ifdef CONFIG_QGKI
	cne_charger_is_doing_hvdcp_detect(doing);
#else
	ret = notify_charger_manager(cti, CNE_POWER_SUPPLY_IS_DOING_HVDCP, (int)doing);
	if (ret < 0)
		pr_err("notify charger-manager(detect doing) failed, ret = %d\n", ret);

#endif
}


static void charger_in_work_handler(struct work_struct *work)
{
	struct chg_type_info *cti = container_of(work,
			struct chg_type_info, chg_in_work);
	int ret = 0;

	if (!cti) {
		ret = -ENODEV;
		pr_err("no chg_type_info data(%d)\n", ret);
		return;
	}

#ifdef CONFIG_QGKI
	cne_charger_int_handler();
#else
	ret = notify_charger_manager(cti, CNE_POWER_SUPPLY_CHARGER_IN, 1);
	if (ret < 0)
		pr_err("notify charger-manager(charger_in) failed, ret = %d\n", ret);
#endif
}

static int charger_request_dpdm(struct chg_type_info *cti, bool enable)
{
	int rc = 0;

	// if (cti->pr_swap_in_progress)
	//	return 0;

	/* fetch the DPDM regulator */
	if (!cti->dpdm_reg && of_get_property(cti->dev->of_node,
				"dpdm-supply", NULL)) {
		cti->dpdm_reg = devm_regulator_get(cti->dev, "dpdm");
		if (IS_ERR(cti->dpdm_reg)) {
			rc = PTR_ERR(cti->dpdm_reg);
			pr_err("Couldn't get dpdm regulator rc=%d\n",
					rc);
			cti->dpdm_reg = NULL;
			return rc;
		}
	}

	mutex_lock(&cti->dpdm_lock);
	if (enable) {
		if (cti->dpdm_reg && !cti->dpdm_enabled) {
			pr_err("enabling DPDM regulator\n");
			rc = regulator_enable(cti->dpdm_reg);
			if (rc < 0)
				pr_err("Couldn't enable dpdm regulator rc=%d\n",
					rc);
			else
				cti->dpdm_enabled = true;
		}
	} else {
		if (cti->dpdm_reg && cti->dpdm_enabled) {
			pr_err("disabling DPDM regulator\n");
			rc = regulator_disable(cti->dpdm_reg);
			if (rc < 0)
				pr_err("Couldn't disable dpdm regulator rc=%d\n",
					rc);
			else
				cti->dpdm_enabled = false;
		}
	}
	mutex_unlock(&cti->dpdm_lock);

	return rc;
}

#ifdef CONFIG_TCPC_CLASS
static int cne_set_chg_type_changed(enum charger_type type)
{
	struct power_supply *psy = power_supply_get_by_name("charger");
	union power_supply_propval propval;
	int ret = 0;

	if (!psy) {
		pr_info("get power supply failed\n");
		return -EINVAL;
	}

	ret = power_supply_get_property(psy,
					POWER_SUPPLY_PROP_CHARGE_TYPE,
					&propval);
	if (ret < 0) {
		pr_err("get psy type failed, ret = %d\n", ret);
		return ret;
	}

	pr_info("charger type = (%d -> %d)\n", propval.intval, type);

	if (propval.intval != type) {
		propval.intval = type;
		ret = power_supply_set_property(psy,
						POWER_SUPPLY_PROP_CHARGE_TYPE,
						&propval);
		if (ret < 0)
			pr_err("set psy type failed, ret = %d\n", ret);
		else
			pr_info("chg_type = %d\n", type);
	}

	return ret;
}

static int pd_tcp_notifier_call(struct notifier_block *pnb,
				unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct chg_type_info *cti = container_of(pnb,
               struct chg_type_info, pd_nb);

	switch (event) {
	case TCP_NOTIFY_SINK_VBUS:
		pr_info("SINK_VBUS: type(0x%02X)\n", noti->vbus_state.type);
		if (noti->vbus_state.type & TCP_VBUS_CTRL_PD_DETECT)
			pd_plug_in_out_handler(cti, true);
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		pr_debug("TYPEC_STATE: old:%d -> new:%d\n",
				noti->typec_state.old_state,
				noti->typec_state.new_state);
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
		    (noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC)) {
			pr_info("USB Plug in, pol = %d\n",
					noti->typec_state.polarity);
			plug_in_out_handler(cti, true, false);
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC)
			&& noti->typec_state.new_state == TYPEC_UNATTACHED) {
			pr_info("USB Plug out\n");
			mutex_lock(&cti->chgdet_lock);
			cti->pd_active = false;
			mutex_unlock(&cti->chgdet_lock);
			plug_in_out_handler(cti, false, false);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_SRC &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SNK) {
			pr_info("Source_to_Sink\n");
			plug_in_out_handler(cti, true, true);
		}  else if (noti->typec_state.old_state == TYPEC_ATTACHED_SNK &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			pr_info("Sink_to_Source\n");
			mutex_lock(&cti->chgdet_lock);
			cti->pd_active = false;
			mutex_unlock(&cti->chgdet_lock);
			plug_in_out_handler(cti, false, true);
		}
		break;
	}
	return NOTIFY_OK;
}

static int cne_tcpc_class_event(struct notifier_block *nb, unsigned long event, void *v)
{

	struct chg_type_info *cti =
			container_of(nb, struct chg_type_info, tcpc_cls_nb);
	struct tcpc_device *tcpc_devi = (struct tcpc_device *)v;
	char *name = tcpc_devi->desc.name;
	int ret = 0;

	pr_info("++ event:%lu tcpc-name:%s\n", event, name);

	if (event == TCPC_CLASS_EVENT_PROP_CHANGED) {
		if (!strcmp(name, "type_c_port0") && !cti->tcpc_dev) {
			pr_info("++ type_c_port0 found\n");
			cti->tcpc_dev = tcpc_devi;
			cti->pd_nb.notifier_call = pd_tcp_notifier_call;
			ret = register_tcp_dev_notifier(cti->tcpc_dev,
				&cti->pd_nb, TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS);
			if (ret < 0)
				pr_info("register tcpc notifer fail\n");
		}
	}

	return ret;
}
#endif

static int chgdet_task_threadfn(void *data)
{
	struct chg_type_info *cti = data;
	bool attach = false;
	bool is_pd = false;
	int ret = 0;

	pr_info("++ enter\n");
	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(cti->waitq,
					     atomic_read(&cti->chgdet_cnt) > 0);
		if (ret < 0) {
			pr_info("wait event been interrupted(%d)\n", ret);
			continue;
		}

		pm_stay_awake(cti->dev);
		mutex_lock(&cti->chgdet_lock);
		atomic_set(&cti->chgdet_cnt, 0);
		attach = cti->chgdet_en;
		is_pd = cti->pd_active;
		mutex_unlock(&cti->chgdet_lock);
		if (!cti->chg1_dev)
			cti->chg1_dev = get_charger_by_name("primary_chg");

#ifdef CONFIG_QGKI
		if (!cti->chg_consumer)
			cti->chg_consumer = charger_manager_get_by_name(cti->dev,
							"charger_port1");
		if (!cti->chg1_dev || !cti->chg_consumer) {
			pr_info("chg dev or consumer is null, skip charger type detect!\n");
			pm_relax(cti->dev);
			continue;
		}

		if (is_pd) {
			charger_manager_ignore_chg_type_det(
				cti->chg_consumer, true);
			#ifdef CONFIG_TCPC_CLASS
			cne_set_chg_type_changed(PD_CHARGER);
			#endif
		} else if (cti->chg_consumer) {
			if (!attach)
				cancel_delayed_work_sync(&cti->tcpc_init_delay_work);

			charger_request_dpdm(cti, attach);
			charger_manager_ignore_chg_type_det(
				cti->chg_consumer, false);
			charger_manager_enable_chg_type_det(cti->chg_consumer,
							attach);
		}
#else
		if (!cti->manager_psy)
			cti->manager_psy = power_supply_get_by_name("charger-manager");

		if (!cti->chg1_dev || !cti->manager_psy) {
			pr_info("chg dev or manager_psy is null, skip charger type detect!\n");
			pm_relax(cti->dev);
			continue;
		}

		if (is_pd) {
			notify_charger_manager(cti,
				CNE_POWER_SUPPLY_IGNORE_CHGTYPE_DETECT, (int)true);
			#ifdef CONFIG_TCPC_CLASS
			cne_set_chg_type_changed(PD_CHARGER);
			#endif
		} else {
			if (!attach)
				cancel_delayed_work_sync(&cti->tcpc_init_delay_work);

			charger_request_dpdm(cti, attach);
			notify_charger_manager(cti,
				CNE_POWER_SUPPLY_IGNORE_CHGTYPE_DETECT, (int)false);
			notify_charger_manager(cti,
				CNE_POWER_SUPPLY_ENABLE_BC12_DETECT, (int)attach);
		}
#endif
		pm_relax(cti->dev);
	}
	pr_info("-- exit\n");
	return 0;
}

static int cne_chg_det_charger_class_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct chg_type_info *cti =
			container_of(nb, struct chg_type_info, chg_cls_nb);
	struct charger_device *chg_dev = (struct charger_device *)v;

	pr_info("++ event:%lu charger-name:%s\n", event, dev_name(&chg_dev->dev));

	if (event == CHGCLS_EVENT_PROP_CHANGED) {
		if (strcmp(dev_name(&chg_dev->dev), "primary_chg") == 0) {
			pr_info("++ primary_chg found\n");
			cti->chg1_dev = chg_dev;
		}

		if (strcmp(dev_name(&chg_dev->dev), "slave_chg") == 0) {
			pr_info("++ slave_chg found\n");
			cti->chg2_dev = chg_dev;
		}

		start_wake_charger(cti);
	}

	return 0;
}

#ifndef CONFIG_QGKI
int cne_chg_det_psy_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct chg_type_info *cti =
			container_of(nb, struct chg_type_info, psy_nb);
	struct power_supply *psy = v;

	if (cti) {
		if (strcmp(psy->desc->name, "charger-manager") == 0) {
			pr_err("%s: init cm psy, and wake charger!\n", __func__);
			cti->manager_psy = psy;
			start_wake_charger(cti);
		}
	}

	return NOTIFY_DONE;
}
#endif

static int cne_charger_detect_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct chg_type_info *cti = NULL;
	struct cne_charger *cne_chg = NULL;
#if defined(USE_EXTCON_USB_CHG)
	int typec_mode = 0;
#endif

	pr_info("++ enter\n");

	cne_chg = devm_kzalloc(&pdev->dev, sizeof(*cne_chg), GFP_KERNEL);
	if (!cne_chg) {
		pr_err("allocate device memory fail\n");
		return -ENOMEM;
	}

	cne_chg->dev = &pdev->dev;
	cne_chg->chg_online = false;
	cne_chg->chg_type = CHARGER_UNKNOWN;

	/* set driver data before resources request it */
	platform_set_drvdata(pdev, cne_chg);

	cne_chg->iio_channels = devm_kcalloc(cne_chg->dev, PSY_IIO_PROP_MAX,
			sizeof(cne_chg->iio_channels[0]), GFP_KERNEL);
	if (!cne_chg->iio_channels) {
		pr_err("allocate iio memory fail\n");
		return -ENOMEM;
	}

	/* init cti device */
	cti = devm_kzalloc(&pdev->dev, sizeof(*cti), GFP_KERNEL);
	if (!cti) {
		ret = -ENOMEM;
		goto cleanup;
	}
	cti->dev = &pdev->dev;
	/* Init Charger Detection */
	cti->tcpc_init_state = 0;
	cti->wake_lock_typec = 0;
#if defined(USE_EXTCON_USB_CHG)
	cti->tcpc_mode = 0;
	cti->pd_active = 0;
	INIT_WORK(&cti->typec_adapter_change_work, typec_adapter_change_work);

	/* extcon init */
	ret = cne_charger_extcon_init(cti);
	if (ret) {
		pr_err("%s init extcon fail(%d)\n", __func__, ret);
		goto cleanup;
	}
#endif

	mutex_init(&cti->chgdet_lock);
	atomic_set(&cti->chgdet_cnt, 0);
	INIT_DELAYED_WORK(&cti->hvdcp_detect_work, hvdcp_detect_work_handler);
	INIT_DELAYED_WORK(&cti->tcpc_init_delay_work, tcpc_init_delay_work_handler);
	init_waitqueue_head(&cti->waitq);
	cti->detect_doing_wq = create_singlethread_workqueue("detect_doing");
	INIT_WORK(&cti->detect_doing_work, detect_doing_work_handler);
	cti->chg_in_wq = create_singlethread_workqueue("charger_in");
	INIT_WORK(&cti->chg_in_work, charger_in_work_handler);
	cti->attach_wake_lock = wakeup_source_register(cti->dev, "typec_attach_wake_lock");
	cti->detach_wake_lock = wakeup_source_register(cti->dev, "typec_detach_wake_lock");
	cne_chg->cti = cti;

	/* register power supply */
	cne_chg->chg_desc.name = "charger";
	cne_chg->chg_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	cne_chg->chg_desc.properties = cne_charger_properties;
	cne_chg->chg_desc.num_properties = ARRAY_SIZE(cne_charger_properties);
	cne_chg->chg_desc.set_property = cne_charger_set_property;
	cne_chg->chg_desc.get_property = cne_charger_get_property;
	cne_chg->chg_cfg.drv_data = cne_chg;

	cne_chg->ac_desc.name = "usb";
	cne_chg->ac_desc.type = POWER_SUPPLY_TYPE_USB_PD;
	cne_chg->ac_desc.properties = cne_ac_properties;
	cne_chg->ac_desc.num_properties = ARRAY_SIZE(cne_ac_properties);
	cne_chg->ac_desc.get_property = cne_ac_get_property;
	cne_chg->ac_desc.set_property = cne_ac_set_property;
	cne_chg->ac_desc.property_is_writeable = cne_ac_prop_is_writeable;
	cne_chg->ac_cfg.drv_data = cne_chg;

	cne_chg->usb_desc.name = "pc_port";
	cne_chg->usb_desc.type = POWER_SUPPLY_TYPE_USB;
	cne_chg->usb_desc.properties = cne_usb_properties;
	cne_chg->usb_desc.num_properties = ARRAY_SIZE(cne_usb_properties);
	cne_chg->usb_desc.get_property = cne_usb_get_property;
	cne_chg->usb_cfg.drv_data = cne_chg;

	cne_chg->chg_psy = devm_power_supply_register(&pdev->dev,
		&cne_chg->chg_desc, &cne_chg->chg_cfg);
	if (IS_ERR(cne_chg->chg_psy)) {
		dev_notice(&pdev->dev, "Failed to register power supply: %ld\n",
			PTR_ERR(cne_chg->chg_psy));
		ret = PTR_ERR(cne_chg->chg_psy);
		goto cleanup;
	}

	cne_chg->ac_psy = devm_power_supply_register(&pdev->dev, &cne_chg->ac_desc,
		&cne_chg->ac_cfg);
	if (IS_ERR(cne_chg->ac_psy)) {
		dev_notice(&pdev->dev, "Failed to register power supply: %ld\n",
			PTR_ERR(cne_chg->ac_psy));
		ret = PTR_ERR(cne_chg->ac_psy);
		goto cleanup;
	}

	cne_chg->usb_psy = devm_power_supply_register(&pdev->dev, &cne_chg->usb_desc,
		&cne_chg->usb_cfg);
	if (IS_ERR(cne_chg->usb_psy)) {
		dev_notice(&pdev->dev, "Failed to register power supply: %ld\n",
			PTR_ERR(cne_chg->usb_psy));
		ret = PTR_ERR(cne_chg->usb_psy);
		goto cleanup;
	}

	// charger detect thread must be placed before chg_dev,cm,adaptor
	cti->chgdet_task = kthread_run(
				chgdet_task_threadfn, cti, "chgdet_thread");
	ret = PTR_ERR_OR_ZERO(cti->chgdet_task);
	if (ret < 0) {
		pr_info("create chg det work fail\n");
		goto err_get_tcpc_dev;
	}

#ifdef CONFIG_QGKI
	// get charger-manager consumer
	cti->chg_consumer = charger_manager_get_by_name(cti->dev,
							"charger_port1");
	if (!cti->chg_consumer) {
		pr_info("get charger consumer device failed\n");
		ret = -EINVAL;
		goto err_get_tcpc_dev;
	}
#else
	// register charger-manager psy notify
	cti->psy_nb.notifier_call = cne_chg_det_psy_event;
	power_supply_reg_notifier(&cti->psy_nb);

	cti->manager_psy = power_supply_get_by_name("charger-manager");
	if (!cti->manager_psy)
		pr_err("charger-manager psy get fail\n");
#endif

	// register charger class notify
	cti->chg_cls_nb.notifier_call = cne_chg_det_charger_class_event;
	charger_class_reg_notifier(&cti->chg_cls_nb);
	cti->chg1_dev = get_charger_by_name("primary_chg");
	if (cti->chg1_dev)
		pr_err("Found primary charger!\n");
	else
		pr_err("*** Error : can't find primary charger ***\n");

	cti->chg2_dev = get_charger_by_name("slave_chg");
	if (cti->chg2_dev)
		pr_err("Found slave charger!\n");
	else
		pr_err("*** Error : can't find slave charger ***\n");

#ifdef CONFIG_TCPC_CLASS
	// register pd adaptor notify
	cti->tcpc_cls_nb.notifier_call = cne_tcpc_class_event;
	tcpc_class_reg_notifier(&cti->tcpc_cls_nb);

	cti->tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (cti->tcpc_dev != NULL) {
		pr_info("tcpc device is ready\n");
		cti->pd_nb.notifier_call = pd_tcp_notifier_call;
		ret = register_tcp_dev_notifier(cti->tcpc_dev,
			&cti->pd_nb, TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS);
		if (ret < 0) {
			pr_notice("register tcpc notifier fail(%d)\n", ret);
			goto cleanup;
		}
	}
#endif

#if defined(USE_EXTCON_USB_CHG)
	// register type-c adaptor notify
	cti->adp_cls_nb.notifier_call = cne_chg_det_adapter_class_event;
	adapter_class_reg_notifier(&cti->adp_cls_nb);

	cti->typec_adapter = get_adapter_by_name("typec_adapter");
	if (cti->typec_adapter)
		pr_err("Found typec adapter [%s]\n", cti->typec_adapter->props.alias_name);
	else
		pr_err("*** Error : can't find typec adapter ***\n");

	// init check for typec/vbus
	pr_info("init check for CC and vbus!\n");
	if (cti->typec_adapter) {
		typec_mode = adapter_dev_get_property(cti->typec_adapter, TYPE_CC_MODE);
		cne_charger_config(cti, typec_mode);
	} else {
		// check vbus
		cne_charger_config(cti, -1);
	}

	power_supply_changed(cne_chg->chg_psy);
	power_supply_changed(cne_chg->ac_psy);
	power_supply_changed(cne_chg->usb_psy);
#endif

	device_init_wakeup(&pdev->dev, true);

	pr_info("probe success\n");
	return 0;

err_get_tcpc_dev:

	devm_kfree(&pdev->dev, cti);

cleanup:
	platform_set_drvdata(pdev, NULL);
	pr_err("probe fail\n");

	return ret;
}

static int cne_charger_detect_remove(struct platform_device *pdev)
{
	struct cne_charger *cne_charger = platform_get_drvdata(pdev);
	struct chg_type_info *cti = cne_charger->cti;

#ifdef CONFIG_TCPC_CLASS
	unregister_tcp_dev_notifier(cti->tcpc_dev,
		&cti->pd_nb, TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS);
#endif
	wakeup_source_unregister(cti->detach_wake_lock);
	wakeup_source_unregister(cti->attach_wake_lock);
	platform_set_drvdata(pdev, NULL);
	cancel_work_sync(&cti->typec_adapter_change_work);

	pr_info("enter\n");
	if (cti->chgdet_task) {
		kthread_stop(cti->chgdet_task);
		atomic_inc(&cti->chgdet_cnt);
		wake_up_interruptible(&cti->waitq);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cne_charger_detect_suspend(struct device *dev)
{
	/* struct cne_charger *cne_charger = dev_get_drvdata(dev); */
	return 0;
}

static int cne_charger_detect_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cne_charger *cne_charger = platform_get_drvdata(pdev);

	if (!cne_charger) {
		pr_info("get cne_charger failed\n");
		return -ENODEV;
	}

	if (cne_charger->chg_psy)
		power_supply_changed(cne_charger->chg_psy);
	if (cne_charger->ac_psy)
		power_supply_changed(cne_charger->ac_psy);
	if (cne_charger->usb_psy)
		power_supply_changed(cne_charger->usb_psy);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cne_charger_pm_ops, cne_charger_detect_suspend,
	cne_charger_detect_resume);

static const struct of_device_id cne_charger_match[] = {
	{ .compatible = "chinoe,charger-detect", },
	{ },
};
static struct platform_driver cne_charger_driver = {
	.probe = cne_charger_detect_probe,
	.remove = cne_charger_detect_remove,
	.driver = {
		.name = "charger detect",
		.owner = THIS_MODULE,
		.pm = &cne_charger_pm_ops,
		.of_match_table = cne_charger_match,
	},
};

/* Legacy api to prevent build error */
bool upmu_is_chr_det(void)
{
	struct cne_charger *cne_chg = NULL;
	struct power_supply *psy = power_supply_get_by_name("charger");

	if (!psy) {
		pr_info("get power supply failed\n");
		return -EINVAL;
	}
	cne_chg = power_supply_get_drvdata(psy);
	return cne_chg->chg_online;
}

/* Legacy api to prevent build error */
bool pmic_chrdet_status(void)
{
	if (upmu_is_chr_det())
		return true;

	pr_notice("No charger\n");
	return false;
}

bool cne_charger_plugin(void)
{
	struct cne_charger *cne_chg = NULL;
	struct power_supply *psy = power_supply_get_by_name("charger");
	struct chg_type_info *cti = NULL;

	if (!psy) {
		pr_info("get power supply failed\n");
		return -EINVAL;
	}
	cne_chg = power_supply_get_drvdata(psy);
	cti = cne_chg->cti;
	pr_info("plugin:%d\n", cti->plugin);

	return cti->plugin;
}

static s32 __init cne_charger_det_init(void)
{
	pr_info("++ cne_charger_det_init\n");
	return platform_driver_register(&cne_charger_driver);
}

static void __exit cne_charger_det_exit(void)
{
	pr_info("++ cne_charger_det_exit\n");
	platform_driver_unregister(&cne_charger_driver);
}

late_initcall_sync(cne_charger_det_init);
module_exit(cne_charger_det_exit);

#ifdef CONFIG_QGKI
#ifdef CONFIG_TCPC_CLASS
static int __init cne_charger_det_notifier_call_init(void)
{
	int ret = 0;
	struct power_supply *psy = power_supply_get_by_name("charger");
	struct cne_charger *cne_chg = NULL;
	struct chg_type_info *cti = NULL;

	if (!psy) {
		pr_notice("get power supply fail\n");
		return -ENODEV;
	}
	cne_chg = power_supply_get_drvdata(psy);
	cti = cne_chg->cti;

	cti->tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	if (cti->tcpc_dev == NULL) {
		pr_notice("get tcpc dev fail\n");
		ret = -ENODEV;
		goto out;
	}
	cti->pd_nb.notifier_call = pd_tcp_notifier_call;
	ret = register_tcp_dev_notifier(cti->tcpc_dev,
		&cti->pd_nb, TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS);
	if (ret < 0) {
		pr_notice("register tcpc notifier fail(%d)\n",
			  ret);
		goto out;
	}
	pr_info("done\n");
out:
	power_supply_put(psy);
	return ret;
}
late_initcall(cne_charger_det_notifier_call_init);
#endif
#endif

MODULE_DESCRIPTION("chino-e charger detect driver");
MODULE_AUTHOR("chino-e");
MODULE_LICENSE("GPL v2");

