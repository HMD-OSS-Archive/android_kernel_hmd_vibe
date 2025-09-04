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

#ifndef __CNE_PD_INTF_H
#define __CNE_PD_INTF_H

#include "adapter_class.h"
#include "cne_charger_intf.h"

/* PD charging */
struct cne_pdc {
	struct tcpc_device *tcpc;
	struct adapter_power_cap cap;
	int pdc_max_watt;
	int pdc_max_watt_setting;

	bool check_impedance;
	int pd_cap_max_watt;
	int pd_cap_max_voltage;
	int pd_idx;
	int pd_reset_idx;
	int pd_boost_idx;
	int pd_buck_idx;
	int vbus_l;
	int vbus_h;

	struct mutex access_lock;
	struct mutex pmic_sync_lock;
	struct wakeup_source suspend_lock;
	int ta_vchr_org;
	bool to_check_chr_type;
	bool to_tune_ta_vchr;
	bool is_cable_out_occur;
	bool is_connect;
	bool is_enabled;
};

bool cne_pdc_check_charger(struct charger_manager *info);
void cne_pdc_plugout_reset(struct charger_manager *info);
void cne_pdc_set_max_watt(struct charger_manager *info, int watt);
int cne_pdc_get_max_watt(struct charger_manager *info);
int cne_pdc_get_setting(struct charger_manager *info, int *vbus,
				int *cur, int *idx);
void cne_pdc_init_table(struct charger_manager *info);
bool cne_pdc_init(struct charger_manager *info);
int cne_pdc_setup(struct charger_manager *info, int idx);
void cne_pdc_plugout(struct charger_manager *info);
void cne_pdc_check_cable_impedance(struct charger_manager *info);
void cne_pdc_reset(struct charger_manager *info);
bool cne_pdc_check_leave(struct charger_manager *info);

#endif /* __CNE_PD_INTF_H */
