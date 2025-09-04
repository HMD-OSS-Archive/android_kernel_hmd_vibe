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

#define pr_fmt(fmt)	"[cne-charge][switch_charging] %s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include "cne_charger_intf.h"
#include "cne_switch_charging.h"
#include "cne_intf.h"
#include "cne_hvdcp.h"
#include "cne_lib.h"

static void _disable_all_charging(struct charger_manager *info)
{
	charger_dev_enable(info->chg1_dev, false);
	charger_dev_enable(info->chg2_dev, false);

	if (pdc_is_ready())
		pdc_stop();
}

static void swchg_select_charging_current_limit(struct charger_manager *info)
{
	struct charger_data *pdata = NULL;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	u32 ichg1_min = 0, aicr1_min = 0;
	int cur;
	int ret = 0;

	pdata = &info->chg1_data;
	mutex_lock(&swchgalg->ichg_aicr_access_mutex);

	/* AICL */
	if (!cne_is_TA_support_pd_pps(info) && !cne_pdc_check_charger(info)) {
		charger_dev_run_aicl(info->chg1_dev,
				&pdata->input_current_limit_by_aicl);
		if (info->enable_dynamic_mivr) {
			if (pdata->input_current_limit_by_aicl >
				info->data.max_dmivr_charger_current)
				pdata->input_current_limit_by_aicl =
					info->data.max_dmivr_charger_current;
		}
	}

	if (pdata->force_charging_current > 0) {

		pdata->charging_current_limit = pdata->force_charging_current;
		if (pdata->force_charging_current <= 450000) {
			pdata->input_current_limit = 500000;
		} else {
			pdata->input_current_limit =
					info->data.ac_charger_input_current;
			pdata->charging_current_limit =
					info->data.ac_charger_current;
		}
		goto done;
	}

	if (info->usb_unlimited) {
		pdata->input_current_limit = 2000000;

		pdata->charging_current_limit =
					info->data.ac_charger_current;
		goto done;
	}

	if (info->water_detected) {
		pdata->input_current_limit = info->data.usb_charger_current;
		pdata->charging_current_limit = info->data.usb_charger_current;
		goto done;
	}

	if (info->atm_enabled == true && (info->chr_type == STANDARD_HOST ||
	    info->chr_type == CHARGING_HOST)) {
		pdata->input_current_limit = 100000; /* 100mA */
		goto done;
	}

	if (is_typec_adapter(info)) {
		if (adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL)
			== 3000) {
			pdata->input_current_limit = 3000000;
			pdata->charging_current_limit = 3000000;
		} else if (adapter_dev_get_property(info->pd_adapter,
			TYPEC_RP_LEVEL) == 1500) {
			pdata->input_current_limit = 1500000;
			pdata->charging_current_limit = 2000000;
		} else if (adapter_dev_get_property(info->typec_adapter,
			TYPEC_RP_LEVEL) == 3000) {
			pdata->input_current_limit = 2000000;
			pdata->charging_current_limit = 2500000;
		} else if (adapter_dev_get_property(info->typec_adapter,
			TYPEC_RP_LEVEL) == 1500) {
			pdata->input_current_limit = 1500000;
			pdata->charging_current_limit = 2000000;
		} else {
			chr_err("type-C: inquire rp error\n");
			pdata->input_current_limit = 500000;
			pdata->charging_current_limit = 500000;
		}

		chr_err("type-C:%d current:%d\n",
			info->pd_type,
			adapter_dev_get_property(info->pd_adapter,
				TYPEC_RP_LEVEL));
	} else if (info->chr_type == STANDARD_HOST) {
			pdata->input_current_limit =
					info->data.usb_charger_current;
			/* it can be larger */
			pdata->charging_current_limit =
					info->data.usb_charger_current;
	} else if (info->chr_type == NONSTANDARD_CHARGER) {
		pdata->input_current_limit =
				info->data.non_std_ac_charger_current;
		pdata->charging_current_limit =
				info->data.non_std_ac_charger_current;
	} else if (info->chr_type == STANDARD_CHARGER ||
			info->chr_type == HVDCP_QC20_CHARGER) {
		pdata->input_current_limit =
				info->data.ac_charger_input_current;
		pdata->charging_current_limit =
				info->data.ac_charger_current;
	} else if (info->chr_type == PD_CHARGER) {
		if (pdc_is_ready()) {
			pdata->input_current_limit =
				info->data.ac_charger_input_current;
			pdata->charging_current_limit =
				info->data.ac_charger_current;
		} else {
			pdata->input_current_limit =
				info->data.usb_charger_current;
			pdata->charging_current_limit =
				info->data.usb_charger_current;
		}
	} else if (info->chr_type == CHARGING_HOST) {
		pdata->input_current_limit =
				info->data.charging_host_charger_current;
		pdata->charging_current_limit =
				info->data.charging_host_charger_current;
	} else if (info->chr_type == APPLE_1_0A_CHARGER) {
		pdata->input_current_limit =
				info->data.apple_1_0a_charger_current;
		pdata->charging_current_limit =
				info->data.apple_1_0a_charger_current;
	} else if (info->chr_type == APPLE_2_1A_CHARGER) {
		pdata->input_current_limit =
				info->data.apple_2_1a_charger_current;
		pdata->charging_current_limit =
				info->data.apple_2_1a_charger_current;
	}

	if (info->enable_sw_jeita) {
		cur = info->sw_jeita.cur;
		if (cur > 0 && cur <= pdata->charging_current_limit)
			pdata->charging_current_limit = cur;
	}

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
	}

	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
	}

	if (pdata->input_current_limit_by_aicl != -1 &&
	    !cne_is_TA_support_pd_pps(info)) {
		if (pdata->input_current_limit_by_aicl <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->input_current_limit_by_aicl;
	}

	if (pdata->user_charging_current_limit != -1) {
		if (pdata->user_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->user_charging_current_limit;
	}

	if (pdata->user_input_current_limit != -1) {
		if (pdata->user_input_current_limit <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->user_input_current_limit;
	}

done:
	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -ENOTSUPP && pdata->charging_current_limit < ichg1_min)
		pdata->charging_current_limit = 0;

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -ENOTSUPP && pdata->input_current_limit < aicr1_min)
		pdata->input_current_limit = 0;

	chr_err("force:%d thermal:%d,%d user:%d,%d jeita:%d,%d setting:%d %d type:%d usb_unlimited:%d usbsm:%d aicl:%d atm:%d\n",
		_uA_to_mA(pdata->force_charging_current),
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->user_input_current_limit),
		_uA_to_mA(pdata->user_charging_current_limit),
		info->enable_sw_jeita,
		_uA_to_mA(info->sw_jeita.cur),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		info->chr_type, info->usb_unlimited,
		info->usb_state,
		pdata->input_current_limit_by_aicl, info->atm_enabled);

	charger_dev_set_input_current(info->chg1_dev,
					pdata->input_current_limit);
	charger_dev_set_charging_current(info->chg1_dev,
					pdata->charging_current_limit);

	/* If AICR < 300mA, stop PE+/PE+20 */
#if 0
	if (pdata->input_current_limit < 300000) {
		if (cne_pe20_get_is_enable(info)) {
			cne_pe20_set_is_enable(info, false);
			if (cne_pe20_get_is_connect(info))
				cne_pe20_reset_ta_vchr(info);
		}

		if (cne_pe_get_is_enable(info)) {
			cne_pe_set_is_enable(info, false);
			if (cne_pe_get_is_connect(info))
				cne_pe_reset_ta_vchr(info);
		}
	}
#endif
	/*
	 * If thermal current limit is larger than charging IC's minimum
	 * current setting, enable the charger immediately
	 */
	if (pdata->input_current_limit > aicr1_min &&
	    pdata->charging_current_limit > ichg1_min && info->can_charging)
		charger_dev_enable(info->chg1_dev, true);
	mutex_unlock(&swchgalg->ichg_aicr_access_mutex);
}

static int swchg_select_cv(struct charger_manager *info)
{
	u32 cv = info->data.battery_cv;
	u32 jt_cv = info->sw_jeita.cv;

	if (info->enable_sw_jeita && jt_cv > 0 && jt_cv < cv)
			cv = jt_cv;

	pr_debug("battery_cv:%d, jeita_cv:%d, cv:%d\n",
		info->sw_jeita.cv, info->sw_jeita.cv, cv);

	charger_dev_set_constant_voltage(info->chg1_dev, cv);

	return cv;
}

static void swchg_turn_on_charging(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	bool charging_enable = true;

	if (swchgalg->state == CHR_ERROR) {
		charging_enable = false;
		chr_err("[charger]Charger Error, turn OFF charging !\n");
	} else {
		swchg_select_charging_current_limit(info);
		if (info->chg1_data.input_current_limit == 0
		    || info->chg1_data.charging_current_limit == 0) {
			charging_enable = false;
			chr_err("[charger]charging current is set 0mA, turn off charging !\n");
		} else {
			swchg_select_cv(info);
		}
	}

	charger_dev_enable(info->chg1_dev, charging_enable);
	charger_dev_enable(info->chg2_dev, false);
}

static int cne_switch_charging_plug_in(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	swchgalg->state = CHR_CC;
	info->polling_interval = CHARGING_INTERVAL;
	swchgalg->disable_charging = false;
	get_monotonic_boottime(&swchgalg->charging_begin_time);
	pdc_plug_in();

	return 0;
}

static int cne_switch_charging_plug_out(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	swchgalg->total_charging_time = 0;

	cne_pdc_plugout(info);
	pdc_plug_out();

	info->leave_pdc = false;
	info->leave_qc20 = false;
	info->hvdcp_err_count = 0;

	return 0;
}

static int cne_switch_charging_do_charging(struct charger_manager *info,
						bool en)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	chr_err("%s: en:%d %s\n", __func__, en, info->algorithm_name);
	if (en) {
		swchgalg->disable_charging = false;
		swchgalg->state = CHR_CC;
		get_monotonic_boottime(&swchgalg->charging_begin_time);
		charger_manager_notifier(info, CHARGER_NOTIFY_NORMAL);
	} else {
		/* disable charging might change state, so call it first */
		_disable_all_charging(info);
		swchgalg->disable_charging = true;
		swchgalg->state = CHR_ERROR;
		charger_manager_notifier(info, CHARGER_NOTIFY_ERROR);
	}

	return 0;
}

static int cne_switch_chr_hvdcp_init(struct charger_manager *info)
{
	return cne_switch_hvdcp_init(info);
}

static int cne_switch_chr_pdc_init(struct charger_manager *info)
{
	int ret;

	ret = pdc_init();

	if (ret == 0)
		set_charger_manager(info);

	info->leave_pdc = false;

	return 0;
}

static int select_pdc_charging_current_limit(struct charger_manager *info,
		struct pdc_data *pdc_data)
{
	struct charger_custom_data *cst_data = &info->data;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct charger_data *pdata;
	struct sw_jeita_data *jeita = &info->sw_jeita;
	int in_curr = 0, chg_curr = 0, time_curr = -1;
	struct timespec64 time_now;
	int chg_time, i;
	int ret = 0;

	pdata = &info->chg1_data;

	/* 1. PD device limit */
	in_curr = cst_data->pd_charger_input_current;
	chg_curr = cst_data->pd_charger_current;

	/* 2. PD adapter limit */
	if (info->pd_current_max > 0 && info->pd_current_max < in_curr)
		in_curr = info->pd_current_max;

	/* 3. thermal limit */
	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit < in_curr)
			in_curr = pdata->thermal_input_current_limit;
	}

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit < chg_curr)
			chg_curr = pdata->thermal_charging_current_limit;
	}

	/* 4. battery temp limit */
	if (info->enable_sw_jeita) {
		if(jeita->cur > 0 && jeita->cur < chg_curr)
			chg_curr = jeita->cur;
	}

	/* 5. force current limit */
	if (pdata->force_charging_current > 0 &&
			pdata->force_charging_current < chg_curr) {
		chg_curr = pdata->force_charging_current;
	}

	/* 6. time param limit */
	if (info->time_param_len > 0) {
		get_monotonic_boottime(&time_now);
		chg_time = time_now.tv_sec - swchgalg->charging_begin_time.tv_sec;
		for (i = info->time_param_len; i > 0; i--) {
			if(chg_time > info->time_param[i - 1].chg_time) {
				time_curr = info->time_param[i - 1].ibat_max;
				if (time_curr < chg_curr)
					chg_curr = time_curr;

				break;
			}
		}
	}

	/* 7. user limit */
	if (pdata->user_input_current_limit != -1) {
		if (pdata->user_input_current_limit < in_curr)
			in_curr = pdata->user_input_current_limit;
	}

	if (pdata->user_charging_current_limit != -1) {
		if (pdata->user_charging_current_limit < chg_curr)
			chg_curr = pdata->user_charging_current_limit;
	}

	pdc_data->input_current_limit = in_curr;
	pdc_data->charging_current_limit = chg_curr;

	chr_err("pd:%d,%d force:%d thermal:%d,%d user:%d,%d adapter:%d duration:%d type:%d charging:%d %d\n",
		_uA_to_mA(cst_data->pd_charger_input_current),
		_uA_to_mA(cst_data->pd_charger_current),
		_uA_to_mA(pdata->force_charging_current),
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->user_input_current_limit),
		_uA_to_mA(pdata->user_charging_current_limit),
		_uA_to_mA(info->pd_current_max),
		_uA_to_mA(time_curr),
		info->chr_type,
		_uA_to_mA(pdc_data->input_current_limit),
		_uA_to_mA(pdc_data->charging_current_limit));

	return ret;
}

static int cne_switch_chr_pdc_run(struct charger_manager *info)
{
	struct charger_custom_data *pdata = &info->data;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct pdc_data *data = NULL;
	int ret = 0;

	data = pdc_get_data();

	select_pdc_charging_current_limit(info, data);

	data->pd_vbus_low_bound = pdata->pd_vbus_low_bound;
	data->pd_vbus_upper_bound = pdata->pd_vbus_upper_bound;
	data->leave_soc_th = pdata->pd_stop_battery_soc;

	/* select CV */
	data->battery_cv = pdata->battery_cv;
	if (info->enable_sw_jeita) {
		if (info->sw_jeita.cv != 0)
			data->battery_cv = info->sw_jeita.cv;
	}

	/* charging enable */
	data->can_charging = info->can_charging;

	if (info->enable_hv_charging == false)
		goto stop;

	ret = pdc_run();

	if (ret == 2) {
		chr_err("leave pdc\n");
		info->leave_pdc = true;
		swchgalg->state = CHR_CC;
	}

	return 0;

stop:
	pdc_stop();
	swchgalg->state = CHR_CC;

	return 0;
}


/* return false if total charging time exceeds max_charging_time */
static bool cne_switch_check_charging_time(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct timespec64 time_now;

	if (info->enable_sw_safety_timer) {
		get_monotonic_boottime(&time_now);
		chr_debug("%s: begin: %ld, now: %ld\n", __func__,
			swchgalg->charging_begin_time.tv_sec, time_now.tv_sec);

		if (swchgalg->total_charging_time >=
		    info->data.max_charging_time) {
			chr_err("%s: SW safety timeout: %d sec > %d sec\n",
				__func__, swchgalg->total_charging_time,
				info->data.max_charging_time);
			charger_dev_notify(info->chg1_dev,
					CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT);
			return false;
		}
	}

	return true;
}

static int cne_switch_chr_cc(struct charger_manager *info)
{
	bool chg_done = false;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct timespec64 charging_time;
	ktime_t k_now, k_begin, k_diff;
	int tmp = battery_get_bat_temperature(info);

#if 0
	/* check bif */
	if (IS_ENABLED(CONFIG_CNE_BIF_SUPPORT)) {
		if (pmic_is_bif_exist() != 1) {
			chr_err("CONFIG_CNE_BIF_SUPPORT but no bif , stop charging\n");
			swchgalg->state = CHR_ERROR;
			charger_manager_notifier(info, CHARGER_NOTIFY_ERROR);
		}
	}
#endif

	k_now = ktime_get_boottime();
	k_begin = timespec64_to_ktime(swchgalg->charging_begin_time);
	k_diff = ktime_sub(k_now, k_begin);

	charging_time = ktime_to_timespec64(k_diff);

	swchgalg->total_charging_time = charging_time.tv_sec;

	chr_err("hv:%d thermal:%d,%d tmp:%d\n",
		info->enable_hv_charging,
		info->chg1_data.thermal_charging_current_limit,
		info->chg1_data.thermal_input_current_limit,
		tmp);

#if 0
	if (pdc_is_ready() &&
		!info->leave_pdc) {
		if (info->enable_hv_charging == true) {
			chr_err("enter PDC!\n");
			swchgalg->state = CHR_PDC;
			pdc_device_init();
			return 1;
		}
	}

	if (hvdcp_is_check(info)) {
		cne_hvdcp_detect_setting(info);
		return 0;
	}

	if (hvdcp_is_ready(info) &&
		!info->leave_qc20) {
		if (info->enable_hv_charging && cne_switch_to_hvdcp_check(info)) {
			chr_err("enter HVDCP!\n");
			swchgalg->state = CHR_HVDCP;
			hvdcp_device_init(info);
			return 1;
		}
	}
#endif

	swchg_turn_on_charging(info);

	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	if (chg_done) {
		swchgalg->state = CHR_BATFULL;
		charger_dev_do_event(info->chg1_dev, EVENT_EOC, 0);
		chr_err("battery full!\n");
	}

	/* If it is not disabled by throttling,
	 * enable PE+/PE+20, if it is disabled
	 */
	if (info->chg1_data.thermal_input_current_limit != -1 &&
		info->chg1_data.thermal_input_current_limit < 300)
		return 0;

	return 0;
}

static int cne_switch_chr_err(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	if (info->enable_sw_jeita) {
		if ((info->sw_jeita.sm == TEMP_BELOW_T0) ||
			(info->sw_jeita.sm == TEMP_ABOVE_T4))
			info->sw_jeita.error_recovery_flag = false;

		if ((info->sw_jeita.error_recovery_flag == false) &&
			(info->sw_jeita.sm != TEMP_BELOW_T0) &&
			(info->sw_jeita.sm != TEMP_ABOVE_T4)) {
			info->sw_jeita.error_recovery_flag = true;
			swchgalg->state = CHR_CC;
			get_monotonic_boottime(&swchgalg->charging_begin_time);
		}
	}

	swchgalg->total_charging_time = 0;

	_disable_all_charging(info);
	return 0;
}

static int cne_switch_chr_full(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	bool chg_done = false, pre_done = false;
	int cv, vbat, soc, rechg_vol, rechg_soc = 100;
	int i, ret = 0;

	swchgalg->total_charging_time = 0;

	/* turn off LED */

	/*
	 * If CV is set to lower value by JEITA,
	 * Reset CV to normal value if temperture is in normal zone
	 */
	cv = swchg_select_cv(info);
	info->polling_interval = CHARGING_FULL_INTERVAL;
	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	if (info->data.enable_software_control_recharge) {
		pre_done = chg_done;
		rechg_soc = info->data.recharge_soc_threshold;
		rechg_vol = cv - info->data.recharge_voltage_threshold;
		vbat = battery_get_bat_voltage(info);
		soc = battery_get_bat_capacity(info);
		if (vbat > 0 && soc >= rechg_soc && vbat < rechg_vol ) {
			chg_done = false;
			ret = charger_dev_enable_hz(info->chg1_dev, true);
			if (ret < 0)
				pr_err("enable hiz fail\n");

			for (i = 0; i < 5; i++) {
				ret = charger_dev_enable_hz(info->chg1_dev, false);
				if (ret >= 0)
					break;
				else {
					pr_err("disable hiz fail,count = %d\n", i);
					msleep(10);
				}
			}

			if (i == 5)
				pr_err("start recharging fail\n");
			else
				pr_info("start recharging success\n");
		}

		pr_info("soc=%d, vbat=%d, rechg:(%d %d), chg_done=(%d %d)\n", soc,
			_uV_to_mV(vbat), _uV_to_mV(rechg_vol), rechg_soc, pre_done, chg_done);
	}

	if (!chg_done) {
		swchgalg->state = CHR_CC;
		charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
		info->enable_dynamic_cv = true;
		get_monotonic_boottime(&swchgalg->charging_begin_time);
		chr_err("battery recharging!\n");
		info->polling_interval = CHARGING_INTERVAL;
	}

	return 0;
}

static int cne_switch_charging_current(struct charger_manager *info)
{
	swchg_select_charging_current_limit(info);
	return 0;
}

static int cne_switch_kick_watchdog(struct charger_manager *info)
{
	pr_info("kick watchdog\n");

	charger_dev_kick_wdt(info->chg1_dev);
	charger_dev_kick_wdt(info->chg2_dev);

	return 0;
}

static int cne_switch_charging_run(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	int ret = 0;

	//chr_err("%s [%d %d], timer=%d\n", __func__, swchgalg->state,
	//	info->pd_type,
	//	swchgalg->total_charging_time);

	charger_dev_kick_wdt(info->chg1_dev);
	charger_dev_kick_wdt(info->chg2_dev);

#if 0
	if (cne_pdc_check_charger(info) == false &&
	    cne_is_TA_support_pd_pps(info) == false) {

		cne_pe20_check_charger(info);
		if (cne_pe20_get_is_connect(info) == false)
			cne_pe_check_charger(info);
	}
#endif

	do {
		switch (swchgalg->state) {
			chr_err("%s_2 [%d] %d\n", __func__, swchgalg->state,
				info->pd_type);
		case CHR_CC:
			ret = cne_switch_chr_cc(info);
			break;

		case CHR_HVDCP:
			ret = cne_switch_hvdcp_run(info);
			break;

		case CHR_PDC:
			ret = cne_switch_chr_pdc_run(info);
			break;

		case CHR_BATFULL:
			ret = cne_switch_chr_full(info);
			break;

		case CHR_ERROR:
			ret = cne_switch_chr_err(info);
			break;
		}
	} while (ret != 0);
	cne_switch_check_charging_time(info);

	charger_dev_dump_registers(info->chg1_dev);
	charger_dev_dump_registers(info->chg2_dev);
	return 0;
}

static int charger_dev_event(struct notifier_block *nb,
	unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, chg1_nb);
	struct chgdev_notify *data = v;

	chr_info("%s %ld", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		charger_manager_notifier(info, CHARGER_NOTIFY_EOC);
		pr_info("%s: end of charge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		charger_manager_notifier(info, CHARGER_NOTIFY_START_CHARGING);
		pr_info("%s: recharge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT:
		info->safety_timeout = true;
		chr_err("%s: safety timer timeout\n", __func__);

		/* If sw safety timer timeout, do not wake up charger thread */
		if (info->enable_sw_safety_timer)
			return NOTIFY_DONE;
		break;
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		info->vbusov_stat = data->vbusov_stat;
		chr_err("%s: vbus ovp = %d\n", __func__, info->vbusov_stat);
		break;
	default:
		return NOTIFY_DONE;
	}

	if (info->chg1_dev->is_polling_mode == false)
		_wake_up_charger(info);

	return NOTIFY_DONE;
}

static int chg1_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, chg1_nb);

	// todo
	chr_info("%s [%s] event:%ld", __func__, info->algorithm_name , event);

	return -ENOTSUPP; // cne_pe50_notifier_call(info, CNE_PE50_NOTISRC_CHG, event, data);
}

static int chg2_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct charger_manager *info =
	 		container_of(nb, struct charger_manager, chg2_nb);

	// todo
	chr_info("%s [%s] event:%ld", __func__, info->algorithm_name , event);

	return -ENOTSUPP;
}

static int swchg_charger_class_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, chg_cls_nb);
	struct charger_device *chg_dev = (struct charger_device *)v;
	// bool found = false;

	pr_info("++ event:%lu charger-name:%s\n", event, dev_name(&chg_dev->dev));

	if (event == CHGCLS_EVENT_PROP_CHANGED) {
		if (strcmp(dev_name(&chg_dev->dev), "primary_chg") == 0) {
			pr_info("++ primary_chg found\n");
			// found = true;
			info->chg1_dev = chg_dev;
			info->chg1_nb.notifier_call = chg1_dev_event;
			register_charger_device_notifier(info->chg1_dev,
						 &info->chg1_nb);
		}

		if (strcmp(dev_name(&chg_dev->dev), "slave_chg") == 0) {
			pr_info("++ slave_chg found\n");
			info->chg2_dev = chg_dev;
		}

		// if (found)
		//	_wake_up_charger(info);
	}

	return 0;
}

int cne_switch_charging_init2(struct charger_manager *info)
{
	struct switch_charging_alg_data *swch_alg;

	swch_alg = devm_kzalloc(&info->pdev->dev,
				sizeof(*swch_alg), GFP_KERNEL);
	if (!swch_alg)
		return -ENOMEM;

	info->chg_cls_nb.notifier_call = swchg_charger_class_event;
	charger_class_reg_notifier(&info->chg_cls_nb);

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev) {
		chr_err("Found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
		info->chg1_nb.notifier_call = chg1_dev_event;
		register_charger_device_notifier(info->chg1_dev,
						 &info->chg1_nb);
	} else
		chr_err("*** Error : can't find primary charger ***\n");

	info->chg2_dev = get_charger_by_name("slave_chg");
	if (info->chg2_dev) {
		chr_err("Found slave charger [%s]\n",
			info->chg2_dev->props.alias_name);
		info->chg2_nb.notifier_call = chg2_dev_event;
		register_charger_device_notifier(info->chg2_dev,
						 &info->chg2_nb);
	} else
		chr_err("*** Error : can't find slave charger ***\n");

	mutex_init(&swch_alg->ichg_aicr_access_mutex);

	info->algorithm_data = swch_alg;
	info->do_algorithm = cne_switch_charging_run;
	info->plug_in = cne_switch_charging_plug_in;
	info->plug_out = cne_switch_charging_plug_out;
	info->do_charging = cne_switch_charging_do_charging;
	info->do_event = charger_dev_event;
	info->change_current_setting = cne_switch_charging_current;
	info->kick_watchdog = cne_switch_kick_watchdog;

	cne_switch_chr_pdc_init(info);
	cne_switch_chr_hvdcp_init(info);

	return 0;
}
