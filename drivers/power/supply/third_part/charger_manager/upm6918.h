/*
 * upm6918 battery charging driver
 *
 * Copyright (C) 2023 Unisemipower
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _LINUX_UPM6918_I2C_H
#define _LINUX_UPM6918_I2C_H

#include <linux/power_supply.h>


struct upm6918_charge_param {
	int vlim;
	int ilim;
	int ichg;
	int vreg;
};

enum stat_ctrl {
	STAT_CTRL_STAT,
	STAT_CTRL_ICHG,
	STAT_CTRL_INDPM,
	STAT_CTRL_DISABLE,
};

enum vboost {
	BOOSTV_4850 = 4850,
	BOOSTV_5000 = 5000,
	BOOSTV_5150 = 5150,
	BOOSTV_5300 = 5300,
};

enum iboost {
	BOOSTI_500 = 500,
	BOOSTI_1200 = 1200,
};

enum vac_ovp {
	VAC_OVP_5700 = 5700,
	VAC_OVP_6500 = 6500,
	VAC_OVP_10500 = 10500,
	VAC_OVP_14000 = 14000,
};

struct upm6918_platform_data {
	struct upm6918_charge_param usb;
	int iprechg;
	int iterm;
	
	enum stat_ctrl statctrl;
	enum vboost boostv;	// options are 4850,
	enum iboost boosti; // options are 500mA, 1200mA
	enum vac_ovp vac_ovp;
	
};

int upm6918_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id);
int upm6918_charger_remove(struct i2c_client *client);
void upm6918_charger_shutdown(struct i2c_client *client);
#endif
