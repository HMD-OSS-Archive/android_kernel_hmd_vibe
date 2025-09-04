/*
 * Copyright (C) 2015 Chino-e Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __CNE_CHARGER_TYPE_H__
#define __CNE_CHARGER_TYPE_H__

enum charger_type {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,		/* USB : 450mA */
	CHARGING_HOST,
	NONSTANDARD_CHARGER,	/* AC : 450mA~1A */
	STANDARD_CHARGER,	/* AC : ~1A */
	HVDCP_QC20_CHARGER,	/* HVDCP : ~2A */
	PD_CHARGER,		/* PD: ~2A */
	APPLE_2_1A_CHARGER, /* 2.1A apple charger */
	APPLE_1_0A_CHARGER, /* 1A apple charger */
	APPLE_0_5A_CHARGER, /* 0.5A apple charger */
	WIRELESS_CHARGER,
};

enum cne_power_supply_property {
	CNE_POWER_SUPPLY_CHARGER_IN = 1,
	CNE_POWER_SUPPLY_ENABLE_BC12_DETECT,
	CNE_POWER_SUPPLY_ENABLE_HVDCP_DETECT,
	CNE_POWER_SUPPLY_IS_DOING_HVDCP,
	CNE_POWER_SUPPLY_IGNORE_CHGTYPE_DETECT,
	CNE_POWER_SUPPLY_SETTING_CHARGING_CURRENT,
	CNE_POWER_SUPPLY_SETTING_INPUT_CURRENT,
};

#define CNE_CPSP_TO_VAL(prop, val)		(((0xf & prop) << 4) | (val & 0xf))
#define CNE_GET_CPSP_FROM_VAL(val)		((0xf0 & val) >> 4)
#define CNE_GET_CVAL_FROM_VAL(val)		(val & 0xf)

void cne_charger_int_handler(void);
void cne_charger_is_doing_hvdcp_detect(bool doing);

bool is_usb_rdy(void);
bool cne_usb_is_device(void);
int is_otg_en(void);

#endif /* __CNE_CHARGER_TYPE_H__ */
