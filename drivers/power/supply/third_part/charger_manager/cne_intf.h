/*
 * Copyright (C) 2017 Chino-e Inc.
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

#ifndef __CNE_INTF_H
#define __CNE_INTF_H

#include <linux/power_supply.h>
#include <linux/time.h>
#include "cne_charger_intf.h"
#include "cne_pdc.h"

#define BATTERY_DEFAULT_TEMPERATURE 250
#define IINDPM_DEFAULT_CURRENT      500000 /* 500mA */
#define IINDPM_DEFAULT_CURRENT      500000 /* 500mA */
#define ADAPTER20W_9V_OUTPUT_CURR_LIMIT 2220000 /* 2220 mA */
#define ADAPTER20W_9V_ALLOW_OC_LIMIT    100000  /* 100 mA */

enum adapter_ret {
	ADAPTER_OK = 0,
	ADAPTER_NOT_SUPPORT,
	ADAPTER_TIMEOUT,
	ADAPTER_REJECT,
	ADAPTER_ERROR,
	ADAPTER_ADJUST,
};

enum charger_use_state {
	CHARGER_STATE_ERROR = -1,
	CHARGER_STATE_NORMAL = 0,
	CHARGER_STATE_QUICK = 1,
};

int charger_is_chip_enabled(bool *en);
int charger_device_init(void);
int charger_enable_chip(bool en);
int charger_is_enabled(bool *en);
int charger_enable_chip(bool en);
int charger_get_mivr_state(bool *in_loop);
int charger_get_mivr(u32 *uV);
int charger_set_mivr(u32 uV);
int charger_get_input_current(u32 *uA);
int charger_set_input_current(u32 uA);
int charger_set_charging_current(u32 uA);
int charger_get_ibus(u32 *ibus);
int charger_get_ibat(u32 *ibat);
int charger_get_vbat(u32 *uV);
int charger_set_constant_voltage(u32 uV);
enum charger_use_state charger_use_charger_ic_check(bool enable,
		bool en_hv, u32 i_limit);
int charger_enable_termination(bool en);
int charger_enable_powerpath(bool en);
int charger_enable_charging(bool en);
int charger_dump_registers(void);

int adapter_set_cap(int mV, int mA);
int adapter_set_cap_start(int mV, int mA);
int adapter_set_cap_end(int mV, int mA);
int adapter_get_output(int *mV, int *mA);
int adapter_get_status(struct ta_status *sta);
int adapter_is_support_pd_pps(void);

int adapter_get_cap(struct pd_cap *cap);
int adapter_is_support_pd(void);

int set_charger_manager(struct charger_manager *info);
int enable_vbus_ovp(bool en);
int wake_up_charger(void);

int battery_get_vbus(void);
int battery_get_soc(void);
int battery_get_bat_current(struct charger_manager *info);
int battery_get_bat_voltage(struct charger_manager *info);
int battery_get_bat_temperature(struct charger_manager *info);
int battery_get_bat_capacity(struct charger_manager *info);
int battery_get_bat_real_capacity(struct charger_manager *info);
int battery_set_property(struct charger_manager *info,
				       enum power_supply_property prop,
				       const union power_supply_propval *val);

bool cne_is_TA_support_pd_pps(struct charger_manager *pinfo);

static inline void get_monotonic_boottime(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_boottime());
}

#endif /* __CNE_INTF_H */
