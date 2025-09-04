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

#define pr_fmt(fmt)	"[cne-charge][slave-charger] %s: " fmt, __func__

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
#include "bq25601.h"
#include "upm6910.h"

#define CNE_SL_CX_NAME  "CX25601"
#define CNE_SL_SGM_NAME  "SGM41513"
#define CNE_SL_HL_NAME  "HL7019"
#define CNE_SL_UPM_NAME  "UPM6910D"
#define CNE_SL_ERR_NAME  "unknown"

char slave_charger_info[32]="unknown";
EXPORT_SYMBOL(slave_charger_info);
static int cne_slave_driver_index = 0;

static int cne_slave_charger_update_device_info(int index)
{
	int ret = 0;

	memset(slave_charger_info, 0, sizeof(slave_charger_info));
	if(index == 1) {
		memcpy(slave_charger_info, CNE_SL_CX_NAME,
			strlen(CNE_SL_CX_NAME) > (sizeof(slave_charger_info) - 1) ?
			(sizeof(slave_charger_info) - 1) : strlen(CNE_SL_CX_NAME));
	} else if (index == 2) {
		memcpy(slave_charger_info, CNE_SL_SGM_NAME,
			strlen(CNE_SL_SGM_NAME) > (sizeof(slave_charger_info) - 1) ?
			(sizeof(slave_charger_info) - 1) : strlen(CNE_SL_SGM_NAME));
	} else if (index == 3) {
		memcpy(slave_charger_info, CNE_SL_HL_NAME,
			strlen(CNE_SL_HL_NAME) > (sizeof(slave_charger_info) - 1) ?
			(sizeof(slave_charger_info) - 1) : strlen(CNE_SL_HL_NAME));
	} else if (index == 4) {
		memcpy(slave_charger_info, CNE_SL_UPM_NAME,
			strlen(CNE_SL_UPM_NAME) > (sizeof(slave_charger_info) - 1) ?
			(sizeof(slave_charger_info) - 1) : strlen(CNE_SL_UPM_NAME));
	} else {
		memcpy(slave_charger_info, CNE_SL_ERR_NAME,
			strlen(CNE_SL_ERR_NAME) > (sizeof(slave_charger_info) - 1) ?
			(sizeof(slave_charger_info) - 1) : strlen(CNE_SL_ERR_NAME));
	}

	return ret;
}

static const struct i2c_device_id cne_slave_charger_i2c_ids[] = {
	{ "bq25601", 0 },
	{ "sgm41513", 0 },
	{ "hl7019d", 0 },
	{ "upm6910d", 0 },
	{},
};

static const struct of_device_id cne_slave_charger_of_match[] = {
	{ .compatible = "bq25601", },
	{ .compatible = "sgm41513", },
	{ .compatible = "hl7019d", },
	{ .compatible = "upm,upm6910d", },
	{ },
};

static int cne_slave_charger_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
    int ret = 0;

	pr_info("++ enter\n");
	match = of_match_node(cne_slave_charger_of_match, node);
	if (match == NULL) {
		pr_err("device tree match not found\n");
		return -EINVAL;
	}

	if (memcmp(match->compatible, "bq25601", 7) == 0) {
		ret = bq25601_driver_probe(client, id);
		if (!ret)
			cne_slave_driver_index = 1;
	} else if (memcmp(match->compatible, "sgm41513", 8) == 0) {
		ret = bq25601_driver_probe(client, id);
		if (!ret)
			cne_slave_driver_index = 2;
	} else if (memcmp(match->compatible, "hl7019d", 7) == 0) {
		ret = bq25601_driver_probe(client, id);
		if (!ret)
			cne_slave_driver_index = 3;
	} else if (memcmp(match->compatible, "upm,upm6910d", 12) == 0) {
		ret = upm6910_charger_probe(client, id);
		if (!ret)
			cne_slave_driver_index = 4;
	} else {
		pr_err("No driver probe!!!\n");
		return -EINVAL;	
	}

	if (ret) {
		pr_err("probe %d failed!!!\n", cne_slave_driver_index);
	} else {
		pr_err("probe %d successfull!!!\n", cne_slave_driver_index);
		cne_slave_charger_update_device_info(cne_slave_driver_index);
	}

	return ret;
}

static int cne_slave_charger_remove(struct i2c_client *client)
{
	if ((cne_slave_driver_index > 0) && (cne_slave_driver_index < 4))
		bq25601_charger_remove(client);
	else if (cne_slave_driver_index == 2)
		upm6910_charger_remove(client);

    return 0;
}

static void cne_slave_charger_shutdown(struct i2c_client *client)
{
	if ((cne_slave_driver_index > 0) && (cne_slave_driver_index < 4))
		bq25601_charger_shutdown(client);
	else if (cne_slave_driver_index == 2)
		upm6910_charger_shutdown(client);
}

static struct i2c_driver cne_slave_charger_driver = {
	.driver = {
		.name = "cne slave charger",
		.owner = THIS_MODULE,
		.of_match_table = cne_slave_charger_of_match,		
	},
	.probe = cne_slave_charger_probe,
	.remove = cne_slave_charger_remove,
	.shutdown = cne_slave_charger_shutdown,
	.id_table = cne_slave_charger_i2c_ids,
};

module_i2c_driver(cne_slave_charger_driver);

MODULE_AUTHOR(" chino-e <chino-e@chino-e.com>");
MODULE_DESCRIPTION("cne slave charger driver");
MODULE_LICENSE("GPL v2");
