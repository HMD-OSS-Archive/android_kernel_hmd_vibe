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

#ifndef __CNE_CHARGER_H__
#define __CNE_CHARGER_H__

#include <linux/ktime.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/hrtimer.h>
#include <linux/spinlock.h>
#include "charger_type.h"
#include "charger_class.h"

/* charger_manager notify charger_consumer */
enum {
	CHARGER_NOTIFY_EOC,
	CHARGER_NOTIFY_START_CHARGING,
	CHARGER_NOTIFY_STOP_CHARGING,
	CHARGER_NOTIFY_ERROR,
	CHARGER_NOTIFY_NORMAL,
};

enum {
	MAIN_CHARGER = 0,
	SLAVE_CHARGER = 1,
	TOTAL_CHARGER = 2,
};

struct charger_consumer {
	struct device *dev;
	void *cm;
	struct notifier_block *pnb;
	struct list_head list;
	bool hv_charging_disabled;
};

/* ============================================= */
/* The following are charger consumer interfaces */
/* ============================================= */

/* @supply_name: name of charging port
 * use charger_port1, charger_port2, ...
 * for most cases, use charging_port1
 */
struct charger_consumer *charger_manager_get_by_name(
	struct device *dev,
	const char *supply_name);
int charger_manager_set_input_current_limit(
	struct charger_consumer *consumer,
	int idx,
	int input_current_uA);
int charger_manager_set_charging_current_limit(
	struct charger_consumer *consumer,
	int idx,
	int charging_current_uA);
int charger_manager_get_input_current_limit(
	struct charger_consumer *consumer,
	int idx,
	int* input_current);
int charger_manager_get_charging_current_limit(
	struct charger_consumer *consumer,
	int idx,
	int* charging_current);
int register_charger_manager_notifier(
	struct charger_consumer *consumer,
	struct notifier_block *nb);
int charger_manager_get_charger_temperature(
	struct charger_consumer *consumer,
	int idx,
	int *tchg_min,
	int *tchg_max);
int unregister_charger_manager_notifier(
	struct charger_consumer *consumer,
	struct notifier_block *nb);
int charger_manager_enable_high_voltage_charging(
	struct charger_consumer *consumer,
	bool en);
int charger_manager_enable_power_path(
	struct charger_consumer *consumer,
	int idx,
	bool en);
int charger_manager_enable_charging(
	struct charger_consumer *consumer,
	int idx,
	bool en);
int charger_manager_ignore_chg_type_det(
	struct charger_consumer *consumer,
	bool en);
int charger_manager_enable_chg_type_det(
	struct charger_consumer *consumer,
	bool en);
int charger_manager_enable_hvdcp_det(
	struct charger_consumer *consumer,
	bool en);
int cne_chr_is_charger_exist(unsigned char *exist);
bool is_power_path_supported(void);
int charger_get_vbus(void);
bool cne_charger_plugin(void);

#endif /* __CNE_CHARGER_H__ */
