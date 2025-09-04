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

#define pr_fmt(fmt)	"[cne-charge][main-charger] %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#endif
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/pinctrl/consumer.h>
#include <linux/i2c.h>
#include "sgm4154x.h"
#include "upm6918.h"

#define CNE_MC_SGM_NAME  "SGM41542"
#define CNE_MC_UPM_NAME  "UPM6918D"
#define CNE_MC_ERR_NAME  "unknown"

char main_charger_info[32]="unknown";
EXPORT_SYMBOL(main_charger_info);
static int cne_driver_index = 0;

static int cne_main_charger_update_device_info(int index)
{
	int ret = 0;

	memset(main_charger_info, 0, sizeof(main_charger_info));
	if(index == 1) {
		memcpy(main_charger_info, CNE_MC_SGM_NAME,
			strlen(CNE_MC_SGM_NAME) > (sizeof(main_charger_info) - 1) ?
			(sizeof(main_charger_info) - 1) : strlen(CNE_MC_SGM_NAME));
	} else if (index == 2) {
		memcpy(main_charger_info, CNE_MC_UPM_NAME,
			strlen(CNE_MC_UPM_NAME) > (sizeof(main_charger_info) - 1) ?
			(sizeof(main_charger_info) - 1) : strlen(CNE_MC_UPM_NAME));
	} else {
		memcpy(main_charger_info, CNE_MC_ERR_NAME,
			strlen(CNE_MC_ERR_NAME) > (sizeof(main_charger_info) - 1) ?
			(sizeof(main_charger_info) - 1) : strlen(CNE_MC_ERR_NAME));
	}

	return ret;
}

static const struct i2c_device_id cne_main_charger_i2c_ids[] = {
	{ "sgm4154x", 0 },
	{ "upm6918d", 0 },
	{},
};

static const struct of_device_id cne_main_charger_of_match[] = {
	{ .compatible = "sgm,sgm4154x", },
	{ .compatible = "upm,upm6918d", },
	{ },
};

static int cne_main_charger_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
    int ret = 0;

	pr_info("++ enter\n");
	match = of_match_node(cne_main_charger_of_match, node);
	if (match == NULL) {
		pr_err("device tree match not found\n");
		return -EINVAL;
	}

	if (memcmp(match->compatible, "sgm,sgm4154x", 12) == 0) {
		ret = sgm4154x_driver_probe(client, id);
		if (!ret)
			cne_driver_index = 1;
	} else if (memcmp(match->compatible, "upm,upm6918d", 12) == 0) {
		ret = upm6918_charger_probe(client, id); 
		if (!ret)
			cne_driver_index = 2;
	} else {
		pr_err("No driver probe!!!\n");
		return -EINVAL;
	}

	if (ret) {
		pr_err("probe %d failed!!!\n", cne_driver_index);
	} else {
		pr_err("probe %d successfull!!!\n", cne_driver_index);
		cne_main_charger_update_device_info(cne_driver_index);
	}

	return ret;
}

static int cne_main_charger_remove(struct i2c_client *client)
{
	if (cne_driver_index == 1)
		sgm4154x_charger_remove(client);
	else if (cne_driver_index == 2)
		upm6918_charger_remove(client);

    return 0;
}

static void cne_main_charger_shutdown(struct i2c_client *client)
{
	if (cne_driver_index == 1)
		sgm4154x_charger_shutdown(client);
	else if (cne_driver_index == 2)
		upm6918_charger_shutdown(client);
}

static struct i2c_driver cne_main_charger_driver = {
	.driver = {
		.name = "cne main charger",
		.owner = THIS_MODULE,
		.of_match_table = cne_main_charger_of_match,		
	},
	.probe = cne_main_charger_probe,
	.remove = cne_main_charger_remove,
	.shutdown = cne_main_charger_shutdown,
	.id_table = cne_main_charger_i2c_ids,
};
module_i2c_driver(cne_main_charger_driver);

MODULE_AUTHOR(" chino-e <chino-e@chino-e.com>");
MODULE_DESCRIPTION("cne main charger driver");
MODULE_LICENSE("GPL v2");
