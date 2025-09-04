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

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/qti_power_supply.h>
#include "cne_lib.h"
#include "cne_charger_intf.h"

int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}

int _uV_to_mV(int uV)
{
	if (uV == -1)
		return -1;
	else
		return uV / 1000;
}

int cne_iio_device_init(struct charger_manager *info,
	struct iio_dev *indio_dev, const struct cne_iio_prop_channels *cne_chans,
	const struct iio_info *iio_info)
{
	int i;
	struct iio_chan_spec *iio_chan;
	struct platform_device *pdev = info->pdev;
	struct device *dev = &pdev->dev;

	info->iio_chan_ids = devm_kcalloc(dev, info->nchannels,
		sizeof(*info->iio_chan_ids), GFP_KERNEL);
	if (!info->iio_chan_ids) {
		pr_err("calloc iio device fail\n");
		return -ENOMEM;
	}

	for (i = 0; i < info->nchannels; i++) {
		iio_chan = &info->iio_chan_ids[i];

		iio_chan->channel = cne_chans[i].channel_num;
		iio_chan->datasheet_name =
			cne_chans[i].datasheet_name;
		iio_chan->extend_name = cne_chans[i].datasheet_name;
		iio_chan->info_mask_separate =
			cne_chans[i].info_mask;
		iio_chan->type = cne_chans[i].type;
		iio_chan->address = i;
	}

	indio_dev->name = "cne-charger-manager";
	indio_dev->info = iio_info;

	return 0;
}

static int smblib_set_prop_system_temp_level(struct charger_manager *info,
		int level)
{
#ifndef DUAL_85_VERSION
	struct charger_data *pchg_data = &info->chg1_data;

	if (level < 0)
		return -EINVAL;

	if (info->thermal_levels <= 0)
		return -EINVAL;

	if (level > info->thermal_levels)
		return -EINVAL;

	info->system_temp_level = level;
	if (info->system_temp_level == info->thermal_levels)
		pchg_data->thermal_charging_current_limit = -1;
	else
		pchg_data->thermal_charging_current_limit =
			info->thermal_mitigation[info->system_temp_level];
	pr_info("thermal set level:%d\n", level);
	return 0;
#else
	pr_info("D85 Version, disable smblib_set_prop_system_temp_level function.\n");
	return 0;
#endif
}

int charger_iio_set_prop(struct charger_manager *info, int channel, int val)
{
	int rc = 0;

	switch (channel) {
		case PSY_IIO_PD_ACTIVE:
			pr_debug("set prop PD_ACTIVE: %d\n", val);
			info->pd_active = val;
			break;
		case PSY_IIO_PD_USB_SUSPEND_SUPPORTED:
			pr_debug("set prop PD_USB_SUSPEND_SUPPORTED: %d\n", val);
			info->pd_usb_suspend_supported = val;
			break;
		case PSY_IIO_PD_IN_HARD_RESET:
			pr_debug("set prop HARD_RESET: %d\n", val);
			info->pd_hard_reset = val;
			break;
		case PSY_IIO_PD_CURRENT_MAX:
			pr_debug("set prop PD_CURRENT_MAX: %d\n", val);
			info->pd_current_max = val;
			break;
		case PSY_IIO_PD_VOLTAGE_MIN:
			pr_debug("set prop PD_VOLTAGE_MIN: %d\n", val);
			info->pd_voltage_min = val;
			break;
		case PSY_IIO_PD_VOLTAGE_MAX:
			pr_debug("set prop PD_VOLTAGE_MAX: %d\n", val);
			info->pd_voltage_max = val;
			break;
		case PSY_IIO_CHARGE_CONTROL_LIMIT:
			smblib_set_prop_system_temp_level(info, val);
			break;
		default:
			pr_info("set prop not support, prop:%d\n", channel);
			rc = -EINVAL;
			break;
	}

	if (rc < 0) {
		pr_err("Couldn't set prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return IIO_VAL_INT;
}

int charger_iio_get_prop(struct charger_manager *info, int channel, int *val)
{
	struct charger_custom_data *cst_data = &info->data;
	int rc = 0;
	enum charger_type temp;

	switch (channel) {
		case PSY_IIO_PD_ACTIVE:
			*val = info->pd_active;
			pr_debug("get prop PD_ACTIVE:%d\n", *val);
			break;
		case PSY_IIO_CHARGE_CONTROL_LIMIT:
			*val = info->system_temp_level;
			pr_debug("get prop CHARGE_CONTROL_LIMIT:%d\n", *val);
			break;
		case PSY_IIO_CHARGE_CONTROL_LIMIT_MAX:
			*val = info->thermal_levels;
			pr_debug("get prop CHARGE_CONTROL_LIMIT_MAX:%d\n", *val);
			break;
		case PSY_IIO_PD_USB_SUSPEND_SUPPORTED:
			*val = info->pd_usb_suspend_supported;
			pr_debug("get prop USB_SUSPEND_SUPPORTED:%d\n", *val);
			break;
		case PSY_IIO_PD_IN_HARD_RESET:
			*val = info->pd_hard_reset;
			pr_debug("get prop PD_IN_HARD_RESET:%d\n", *val);
			break;
		case PSY_IIO_PD_CURRENT_MAX:
			*val = info->pd_current_max;
			if (cst_data->pd_charger_input_current > 0 &&
					info->pd_current_max > cst_data->pd_charger_input_current)
				*val = cst_data->pd_charger_input_current;

			pr_debug("get prop PD_CURRENT_MAX:%d\n", *val);
			break;
		case PSY_IIO_PD_VOLTAGE_MIN:
			*val = info->pd_voltage_min;
			pr_debug("get prop PD_VOLTAGE_MIN:%d\n", *val);
			break;
		case PSY_IIO_PD_VOLTAGE_MAX:
			*val = info->pd_voltage_max;
			pr_debug("get prop PD_VOLTAGE_MAX:%d\n", *val);
			break;
		case PSY_IIO_USB_REAL_TYPE:
			temp = get_charger_type(info);
			if (temp == CHARGER_UNKNOWN)
				*val = POWER_SUPPLY_TYPE_UNKNOWN;
			else if (temp == STANDARD_HOST)
				*val = POWER_SUPPLY_TYPE_USB;
			else if (temp == CHARGING_HOST)
				*val = POWER_SUPPLY_TYPE_USB_CDP;
			else if (temp == STANDARD_CHARGER)
				*val = POWER_SUPPLY_TYPE_USB_DCP;
			else if (temp ==  HVDCP_QC20_CHARGER)
				*val = QTI_POWER_SUPPLY_TYPE_USB_HVDCP;
			else if (temp ==  PD_CHARGER)
				*val = POWER_SUPPLY_TYPE_USB_PD;
			else if (temp == NONSTANDARD_CHARGER)
				*val = QTI_POWER_SUPPLY_TYPE_USB_FLOAT;
			else
				*val = QTI_POWER_SUPPLY_TYPE_USB_FLOAT;
			pr_debug("get prop USB_REAL_TYPE:%d\n", *val);
			break;
		default:
			pr_info("get prop not support, prop:%d\n", channel);
			rc = -EINVAL;
			break;
	}

	if (rc < 0) {
		pr_info("Couldn't get prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return IIO_VAL_INT;
}

int charger_step_set_current(struct charger_manager *info,
	struct charger_device *chg_dev, u32 uA)
{
	u32 olduA;
	bool cable_in = true;
	int ret = 0;

	ret = charger_dev_get_charging_current(chg_dev, &olduA);
	if (ret < 0) {
		pr_err("get charging current error\n");
		return ret;
	}

	if (chg_dev && chg_dev->props.alias_name)
		pr_info("set %s current:%dmA, old:%dmA\n",
				chg_dev->props.alias_name, _uA_to_mA(uA), _uA_to_mA(olduA));
	else
		pr_info("set current:%dmA, old:%dmA\n",
				_uA_to_mA(uA), _uA_to_mA(olduA));

	if (abs(uA - olduA) >= CHARGER_DEV_ICHG_STEP) {
		do {
			if (uA > olduA) {
				if ((uA - olduA) / CHARGER_CURRENT_ADJUST_STEP > 0)
					olduA += CHARGER_CURRENT_ADJUST_STEP;
				else
					olduA = uA;
			} else {
				if ((olduA - uA) / CHARGER_CURRENT_ADJUST_STEP > 0)
					olduA -= CHARGER_CURRENT_ADJUST_STEP;
				else
					olduA = uA;
			}
			pr_debug("mA:%d oldmA:%d\n", _uA_to_mA(uA), _uA_to_mA(olduA));
			ret = charger_dev_set_charging_current(chg_dev, olduA);
			if (ret < 0) {
				pr_err("set charging current error\n");
				return ret;
			}

			mutex_lock(&info->cable_out_lock);
			cable_in = !info->cable_out_cnt;
			mutex_unlock(&info->cable_out_lock);

			if (cable_in)
				msleep(100);
			else
				pr_info("cable out, exit\n");

		}while (cable_in && (olduA != uA) && (info->chr_type != CHARGER_UNKNOWN));

	}

	return ret;

}

enum charger_type get_charger_type(struct charger_manager *info)
{
	union power_supply_propval pval = {0, };
	int rc = 0;

	if (!info) {
		pr_err("charger manager is null\n");
		return -EINVAL;
	}

	if (!info->charger_psy) {
		info->charger_psy = power_supply_get_by_name("charger");
		if (!info->charger_psy) {
			pr_err("Couldn't find charger psy\n");
			return -EINVAL;
		}
	}

	rc = power_supply_get_property(info->charger_psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &pval);
	if (rc < 0) {
		pr_err("Couldn't get charge type\n");
		return -EINVAL;
	}

	return pval.intval;
}

