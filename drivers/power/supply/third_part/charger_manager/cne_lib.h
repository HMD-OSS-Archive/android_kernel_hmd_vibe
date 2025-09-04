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
#ifndef __CNE_LIB_H
#define __CNE_LIB_H

#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/qti_power_supply.h>
#include <linux/power_supply.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>
#include "cne_charger_intf.h"

#define MICRO_5V	5000000
#define MICRO_9V	9000000
#define MICRO_12V	12000000

struct cne_iio_prop_channels {
	const char *datasheet_name;
	int channel_num;
	enum iio_chan_type type;
	long info_mask;
};

#define PARAM(chan) PSY_IIO_##chan

#define CNE_CHAN(_dname, _chan, _type, _mask)		\
	{								\
		.datasheet_name = _dname,				\
		.channel_num = _chan,				\
		.type = _type,						\
		.info_mask = _mask,					\
	},								\

#define CNE_CHAN_VOLT(_dname, chan)					\
	[PARAM(chan)] = CNE_CHAN(_dname, PARAM(chan),		\
			IIO_VOLTAGE, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define CNE_CHAN_CUR(_dname, chan)					\
	[PARAM(chan)] = CNE_CHAN(_dname, PARAM(chan),		\
			IIO_CURRENT, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define CNE_CHAN_INDEX(_dname, chan)					\
	[PARAM(chan)] = CNE_CHAN(_dname, PARAM(chan),		\
			IIO_INDEX, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define CNE_CHAN_ACTIVITY(_dname, chan)				\
	[PARAM(chan)] = CNE_CHAN(_dname, PARAM(chan),		\
			IIO_ACTIVITY, BIT(IIO_CHAN_INFO_PROCESSED))	\

static const struct cne_iio_prop_channels cne_iio_chans[] = {
	CNE_CHAN_INDEX("pd_active", PD_ACTIVE)
	CNE_CHAN_INDEX("pd_usb_suspend_supported",
			PD_USB_SUSPEND_SUPPORTED)
	CNE_CHAN_ACTIVITY("pd_in_hard_reset", PD_IN_HARD_RESET)
	CNE_CHAN_CUR("pd_current_max", PD_CURRENT_MAX)
	CNE_CHAN_VOLT("pd_voltage_min", PD_VOLTAGE_MIN)
	CNE_CHAN_VOLT("pd_voltage_max", PD_VOLTAGE_MAX)
	CNE_CHAN_INDEX("real_type", USB_REAL_TYPE)
	CNE_CHAN_INDEX("charge_control_limit", CHARGE_CONTROL_LIMIT)
	CNE_CHAN_INDEX("charge_control_limit_max", CHARGE_CONTROL_LIMIT_MAX)
};

int _uA_to_mA(int uA);

int _uV_to_mV(int uV);

enum charger_type get_charger_type(struct charger_manager *info);

int cne_iio_device_init(struct charger_manager *info,
	struct iio_dev *indio_dev, const struct cne_iio_prop_channels *cp_chans,
	const struct iio_info *cp_iio_info);

int charger_iio_set_prop(struct charger_manager *info, int channel, int val);

int charger_iio_get_prop(struct charger_manager *info, int channel, int *val);

int charger_step_set_current(struct charger_manager *info,
	struct charger_device *chg_dev, u32 uA);

#endif /*  __CNE_LIB_H */

