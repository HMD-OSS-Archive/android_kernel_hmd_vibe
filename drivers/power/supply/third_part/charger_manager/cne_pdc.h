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

#ifndef __CNE_PDC_H
#define __CNE_PDC_H

#define ADAPTER_CAP_MAX_NR 10
#define PD_CAP_HV_THRESHOLD 7500

/* pe40 copy start */
struct pps_status {
	int output_mv;	/* 0xffff means no support */
	int output_ma;	/* 0xff means no support */
	uint8_t real_time_flags;
};

struct ta_status {
	int temperature;
	bool ocp;
	bool otp;
	bool ovp;
};
/* pe40 copy end */

struct pd_cap {
	uint8_t selected_cap_idx;
	uint8_t nr;
	uint8_t pdp;
	uint8_t pwr_limit[ADAPTER_CAP_MAX_NR];
	int max_mv[ADAPTER_CAP_MAX_NR];
	int min_mv[ADAPTER_CAP_MAX_NR];
	int ma[ADAPTER_CAP_MAX_NR];
	int maxwatt[ADAPTER_CAP_MAX_NR];
	int minwatt[ADAPTER_CAP_MAX_NR];
	uint8_t type[ADAPTER_CAP_MAX_NR];
	int info[ADAPTER_CAP_MAX_NR];
};

struct pdc_data {
	int input_current_limit;
	int charging_current_limit;
	int battery_cv;
	int min_charger_voltage;
	int pd_vbus_low_bound;
	int pd_vbus_upper_bound;
	int ibus_err;
	int vsys_watt;
	bool can_charging;
	int leave_soc_th;
};

struct pdc {
	struct pd_cap cap;
	struct pdc_data data;

	int pdc_max_watt;
	int pdc_max_watt_setting;

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

	int pdc_input_current_limit_setting;	/* TA */
	int charging_state;
};

int pdc_init(void);
int pdc_device_init(void);
bool pdc_is_enable(void);
bool pdc_is_connect(void);
bool pdc_is_ready(void);
int pdc_reset(void);
int pdc_stop(void);
int pdc_run(void);
int pdc_plug_in(void);
int pdc_plug_out(void);
int pdc_set_data(struct pdc_data data);
struct pdc_data *pdc_get_data(void);

#endif /* __CNE_PDC_H */
