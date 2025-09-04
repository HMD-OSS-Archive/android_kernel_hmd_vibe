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

#define pr_fmt(fmt)	"[cne-charge][hvdcp] %s: " fmt, __func__

#include <linux/delay.h>
#include "cne_hvdcp.h"
#include "cne_charger_intf.h"
#include "cne_intf.h"
#include "cne_switch_charging.h"
#include "cne_lib.h"

bool hvdcp_is_check(struct charger_manager *info)
{
	bool doing = false;

	if (info && info->is_doing_hvdcp_check)
		doing = true;

	if (info && info->chr_type == PD_CHARGER) {
		info->leave_qc20 = true;
		doing = false;
	}

	pr_info("is_check = %d, doing = %d\n", info->is_doing_hvdcp_check, doing);
	return doing;
}

bool hvdcp_is_ready(struct charger_manager *info)
{
	struct sw_jeita_data *jeita = &info->sw_jeita;
	struct charger_custom_data *pdata = &info->data;
	bool is_ready = false;
	int soc, temp;

	if (!info) {
		pr_err("charger manager not found\n");
		goto out;
	}

	if (info->chr_type != HVDCP_QC20_CHARGER)
		goto out;

	if (info->enable_sw_jeita) {
		if (jeita->sm != TEMP_T2_TO_T3 && jeita->sm != TEMP_T3_TO_T4)
			goto out;
	} else {
		temp = battery_get_bat_temperature(info);
		if (temp > pdata->hvdcp_exit_temp_h ||
			temp < pdata->hvdcp_exit_temp_l)
			goto out;
	}

	soc  = battery_get_bat_capacity(info);
	if (soc > pdata->hvdcp_exit_soc)
		goto out;

	is_ready = true;

out:
	pr_debug("is_ready = %d\n", is_ready);
	return is_ready;
}

static int hvdcp_enable_charger_ic(struct charger_manager *info)
{
	int ret = 0;

	pr_info("++\n");

	if(info->chg1_dev && info->chg2_dev) {
		if (info->is_dual_charger_enable) {
			pr_info("enable dual charger ic\n");
			ret = charger_dev_enable(info->chg1_dev, true);
			if (ret < 0) {
				pr_err("main charger enable fail\n");
				goto out;
			}

			ret = charger_dev_enable(info->chg2_dev, true);
			if (ret < 0) {
				pr_err("slave charger enable fail\n");
				goto out;
			}
		} else {
			pr_info("enable single charger ic\n");
			ret = charger_dev_enable(info->chg1_dev, true);
			ret = charger_dev_enable(info->chg2_dev, false);
			if (ret < 0) {
				pr_err("slave charger disable fail\n");
				goto out;
			}
		}
	} else {
		ret = charger_dev_enable(info->chg1_dev, true);
		if (ret < 0) {
			pr_err("main charger enable fail\n");
			goto out;
		}
		pr_info("enable single charger\n");
	}

out:
	return ret;
}

static int set_hvdcp_dev_set_mivr(struct charger_manager *info, int uV)
{
	int ret = 0;

	if (info->chg1_dev && info->chg2_dev) {
		ret = charger_dev_set_mivr(info->chg1_dev, uV);
		ret = charger_dev_set_mivr(info->chg2_dev, uV + HVDCP_SLAVE_CHARGER_MIVR_DELTA_UV);
	} else {
		ret = charger_dev_set_mivr(info->chg1_dev, uV);
	}

	return ret;
}

static int set_hvdcp_dev_enable(struct charger_manager *info)
{
	int ret = 0;

	pr_info("++\n");

	if(info->chg1_dev && info->chg2_dev) {
		if (info->is_dual_charger_enable) {
			pr_info("switch dual charger\n");
			charger_dev_enable(info->chg1_dev, true);
			charger_dev_enable(info->chg2_dev, true);
		} else {
			pr_info("switch single charger\n");
			charger_dev_enable(info->chg1_dev, true);
			charger_dev_enable(info->chg2_dev, false);
		}
	} else {
		charger_dev_enable(info->chg1_dev, true);
		pr_info("enable single charger\n");
	}

	return ret;
}

static int set_hvdcp_constant_voltage_limit(struct charger_manager *info)
{
	struct sw_jeita_data *jeita = &info->sw_jeita;
	int cv = 0;
	int ret = 0;

	cv = info->data.battery_cv;

	if (info->enable_sw_jeita) {
		if (jeita->cv != 0 && jeita->cv < cv)
			cv = jeita->cur;
	}

	if (info->chg1_dev && info->chg2_dev) {
		charger_dev_set_constant_voltage(info->chg1_dev, cv);
		charger_dev_set_constant_voltage(info->chg2_dev, cv);
	} else {
		charger_dev_set_constant_voltage(info->chg1_dev, cv);
	}

	pr_info("BCV:%d, J:(E:%d %d), CV:%d\n",
		_uV_to_mV(info->data.battery_cv),
		info->enable_sw_jeita, _uV_to_mV(jeita->cv),
		_uV_to_mV(cv));


	return ret;
}

static int select_hvdcp_charging_current_limit(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct charger_custom_data *pdata = &info->data;
	struct charger_data *pchg_data = &info->chg1_data;
	struct sw_jeita_data *jeita = &info->sw_jeita;
	int in_curr = 0, chg_curr = 0, time_curr = -1;
	struct timespec64 time_now;
	int chg_time, i;
	int ret = 0;

	in_curr = HVDCP_INPUT_CURRENT_MAX_UA;
	chg_curr = HVDCP_CHARGING_CURRENT_MAX_UA;

	/* 1. device limit */
	if (pdata->hvdcp_charger_input_current > 0 &&
			pdata->hvdcp_charger_input_current < in_curr)
		in_curr = pdata->hvdcp_charger_input_current;

	if (pdata->hvdcp_charger_current > 0 &&
			pdata->hvdcp_charger_current < chg_curr)
		chg_curr = pdata->hvdcp_charger_current;

	/* 2. thermal limit */
	if (pchg_data->thermal_input_current_limit > 0 &&
			pchg_data->thermal_input_current_limit < in_curr)
		in_curr = pchg_data->thermal_input_current_limit;

	if (pchg_data->thermal_charging_current_limit > 0 &&
			pchg_data->thermal_charging_current_limit < chg_curr)
		chg_curr = pchg_data->thermal_charging_current_limit;

	/* 3. battery temp limit */
	if (info->enable_sw_jeita) {
		if(jeita->cur > 0 && jeita->cur < chg_curr)
			chg_curr = jeita->cur;
	}

	/* 4. force charging current */
	if (pchg_data->force_charging_current > 0 &&
			pchg_data->force_charging_current < chg_curr) {
		chg_curr = pchg_data->force_charging_current;
	}

	/* 5. input current limit by aicl */
	if (pchg_data->input_current_limit_by_aicl > 0 &&
			pchg_data->input_current_limit_by_aicl < in_curr) {
		in_curr = pchg_data->charging_current_limit;
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
	if (pchg_data->user_input_current_limit > 0 &&
			pchg_data->user_input_current_limit < in_curr)
		in_curr = pchg_data->user_input_current_limit;

	if (pchg_data->user_charging_current_limit > 0 &&
			pchg_data->user_charging_current_limit < chg_curr)
		chg_curr = pchg_data->user_charging_current_limit;

	info->hvdcp_input_currnet = in_curr;
	info->hvdcp_charging_current = chg_curr;

	pr_info("HVDCP: D:(%d %d) T:(%d %d) U:(%d %d) J:(E:%d %d) F:(%d) A:(%d) TL:(%d) C(%d %d)\n",
		_uA_to_mA(pdata->hvdcp_charger_input_current),
		_uA_to_mA(pdata->hvdcp_charger_current),
		_uA_to_mA(pchg_data->thermal_input_current_limit),
		_uA_to_mA(pchg_data->thermal_charging_current_limit),
		_uA_to_mA(pchg_data->user_input_current_limit),
		_uA_to_mA(pchg_data->user_charging_current_limit),
		info->enable_sw_jeita, _uA_to_mA(jeita->cur),
		_uA_to_mA(pchg_data->force_charging_current),
		_uA_to_mA(pchg_data->input_current_limit_by_aicl),
		_uA_to_mA(time_curr),
		_uA_to_mA(info->hvdcp_input_currnet),
		_uA_to_mA(info->hvdcp_charging_current));

	return ret;
}

static int set_hvdcp_charging_current_limit(struct charger_manager *info)
{
	int ret = 0;
	int curr = 0;
	int i_uA, chg_uA;
	int master_in_curr, slave_in_curr;

	i_uA = info->hvdcp_input_currnet;
	chg_uA = info->hvdcp_charging_current;

	if (info->chg1_dev && info->chg2_dev) {
		if (info->is_dual_charger_enable) {
			master_in_curr = ((i_uA / 2) /
				CHARGER_DEV_IINDPM_STEP) * CHARGER_DEV_IINDPM_STEP;
			slave_in_curr = i_uA - master_in_curr;
			curr = chg_uA / 2;

			pr_info("use dual ic to charge, current:(%d %d) -> I(%d %d) C(%d)\n",
				_uA_to_mA(i_uA), _uA_to_mA(chg_uA),
				_uA_to_mA(master_in_curr), _uA_to_mA(slave_in_curr),
				_uA_to_mA(curr));

			charger_dev_set_input_current(info->chg1_dev, master_in_curr);
			charger_dev_set_input_current(info->chg2_dev, slave_in_curr);

			ret = charger_step_set_current(info, info->chg1_dev, curr);
			ret += charger_step_set_current(info, info->chg2_dev, curr);
			if (ret < 0) {
				pr_err("set chg1_dev or chg2_dev charging current fail\n");
				return ret;
			}
		} else {
			pr_info("use single ic to charge, current:(%d %d)mA\n",
				_uA_to_mA(i_uA), _uA_to_mA(chg_uA));

			ret = charger_dev_set_input_current(info->chg1_dev, i_uA);
			ret += charger_dev_set_charging_current(info->chg1_dev, chg_uA);
			if (ret < 0) {
				pr_err("set chg1_dev charging current fail\n");
				return ret;
			}
		}
	} else {
		charger_dev_set_input_current(info->chg1_dev, i_uA);
		ret = charger_dev_set_charging_current(info->chg1_dev, chg_uA);
		if (ret < 0) {
			pr_err("set chg1_dev charging current fail\n");
			return ret;
		}
	}

	return ret;
}

int cne_hvdcp_detect_setting(struct charger_manager *info)
{
	struct charger_custom_data *pdata = &info->data;
	int ret = 0;

	charger_dev_set_charging_current(info->chg1_dev, 
			HVDCP_CHARGING_CURRENT_DEFAULT_UA);
	charger_dev_set_charging_current(info->chg2_dev, 0);
	charger_dev_set_constant_voltage(info->chg1_dev,
			pdata->battery_cv);
	charger_dev_set_constant_voltage(info->chg2_dev,
			pdata->battery_cv);

	charger_dev_enable(info->chg1_dev, true);
	charger_dev_enable(info->chg2_dev, false);

	return ret;
}

int hvdcp_device_init(struct charger_manager *info)
{
	int ret = 0;

	if(info->chg2_dev) {
		ret = charger_dev_init(info->chg2_dev);
		charger_dev_set_input_current(info->chg2_dev, 0);
		charger_dev_set_charging_current(info->chg2_dev, 0);
	}

	return ret;
}

int hvdcp_get_vbus_interval(struct charger_manager *info)
{
	int vol;
	int ret;

	ret = charger_dev_get_vbus(info->chg1_dev, &vol);
	if (ret < 0) {
		pr_err("get vbus error.\n");
		return ret;
	}

	if (vol > ADAPTER_5V_INTERVAL_LOW && vol < ADAPTER_5V_INTERVAL_HIGH)
		return ADAPTER_5V;
	else if (vol > ADAPTER_9V_INTERVAL_LOW && vol < ADAPTER_9V_INTERVAL_HIGH)
		return ADAPTER_9V;

	return ADAPTER_5V;
}

int hvdcp_set_vbus_voltage(struct charger_manager *info, u32 V)
{
	int i = 0;
	int adp_v = 0;
	int ret = 0;

	adp_v = hvdcp_get_vbus_interval(info);
	if (adp_v < 0) {
		pr_err("get adaptor voltage fail\n");
		return adp_v;
	}

	if (adp_v == V) {
		pr_info("adapter is %dmV, exit\n", adp_v);
		return 0;
	}

	for (i = 0; i < 3; i++) {
		if (get_charger_type(info) != HVDCP_QC20_CHARGER) {
			pr_info("charge type(%d) error, exit.\n", info->chr_type);
			return -1;
		}

		ret = charger_dev_enable_hvdcp_voltage(info->chg1_dev,
			V * ADAPTER_V_TO_UV);
		if (ret < 0) {
			pr_err("set adaptor to %dV fail\n", V);
			return ret;
		}

		msleep(500);
		adp_v = hvdcp_get_vbus_interval(info);
		if (adp_v < 0) {
			pr_err("get adaptor voltage fail\n");
			return adp_v;
		}

		if(adp_v == V)
			break;
	}

	if (adp_v != V) {
		pr_err("Vadaptor(%d) is not equel Vexpected(%d)\n", adp_v, V);
		ret = -1;
	}

	pr_info("switch vbus to %dV.\n", V);

	return ret;
}

int is_in_hvdcp_state_check(struct charger_manager *info)
{
	struct sw_jeita_data *jeita = &info->sw_jeita;
	struct charger_custom_data *pdata = &info->data;
	int temp, soc, curr_ua;

	if (info->chr_type != HVDCP_QC20_CHARGER) {
		pr_err("chr_type:%d error, exit hvdcp.\n", info->chr_type);
		goto lvdcp;
	}

	if (info->hvdcp_err_count > HVDCP_ERR_MAX_COUNT) {
		pr_err("hvdcp err count over max count\n");
		info->leave_qc20 = true;
		goto lvdcp;
	}

	soc  = battery_get_bat_capacity(info);
	pr_info("soc:%d, en:%d, sm:(%d %d), cur:(%d %d)\n", soc,
		info->can_charging, info->enable_sw_jeita, jeita->sm,
		_uA_to_mA(info->hvdcp_charging_current),
		pdata->hvdcp_exit_curr_ma);

	/* charging disable */
	if (!info->can_charging)
		goto lvdcp;

	/* soc limit */
	if (soc > pdata->hvdcp_exit_soc) {
		info->leave_qc20 = true;
		goto lvdcp;
	}

	/* jeita limit */
	if (info->enable_sw_jeita) {
		if (jeita->sm != TEMP_T2_TO_T3 && jeita->sm != TEMP_T3_TO_T4)
			goto lvdcp;
	} else {
		temp = battery_get_bat_temperature(info);
		if (temp > pdata->hvdcp_exit_temp_h ||
			temp < pdata->hvdcp_exit_temp_l)
			goto lvdcp;
	}

	/* current limit */
	curr_ua = pdata->hvdcp_exit_curr_ma * 1000;
	if (info->hvdcp_charging_current < curr_ua)
		goto lvdcp;

	info->is_dual_charger_enable = true;
	return true;

lvdcp:
	info->is_dual_charger_enable = false;
	return false;
}

static int hvdcp_switch_to_single_ic(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	int ret = 0;

	ret = hvdcp_enable_charger_ic(info);
	if (ret < 0) {
		pr_err("switch to single ic fail\n");
		return ret;
	}

	ret = hvdcp_set_vbus_voltage(info, ADAPTER_5V);
	if (ret < 0) {
		pr_err("set vbus to specific voltage fail\n");
		return ret;
	}

	pr_info("switch to single ic success\n");

	swchgalg->state = CHR_CC;

	return 0;
}

int cne_switch_hvdcp_init(struct charger_manager *info)
{
	int ret = 0;

	ret = charger_dev_enable(info->chg2_dev, false);
	info->leave_qc20 = false;
	info->hvdcp_err_count = 0;

	return ret;
}

bool cne_switch_to_hvdcp_check(struct charger_manager *info)
{
	bool need_hvdcp = false;

	select_hvdcp_charging_current_limit(info);

	need_hvdcp = is_in_hvdcp_state_check(info);

	if (!need_hvdcp)
		hvdcp_set_vbus_voltage(info, ADAPTER_5V);

	return need_hvdcp;
}

int cne_switch_hvdcp_run(struct charger_manager *info)
{
	struct charger_custom_data *pdata = &info->data;
	int adp_v;
	int ret = 0;
	int v_dpm;

	if (get_charger_type(info) == CHARGER_UNKNOWN) {
		pr_err("unplug the adapter, exit.\n");
		charger_dev_enable_hvdcp_voltage(info->chg1_dev, ADAPTER_DPDM_HIZ);
		return 0;
	}

	select_hvdcp_charging_current_limit(info);

	if (!is_in_hvdcp_state_check(info)) {
		ret = hvdcp_switch_to_single_ic(info);
		if (ret < 0) {
			info->hvdcp_err_count++;
			return 0;
		}

		v_dpm = info->data.min_charger_voltage;
		pr_info("set mivr to %dmV\n", _uV_to_mV(v_dpm));
		set_hvdcp_dev_set_mivr(info, v_dpm);

		return 1;
	}

	adp_v = hvdcp_get_vbus_interval(info);
	if (adp_v < 0) {
		info->hvdcp_err_count++;
		return 0;
	}

	if (adp_v != ADAPTER_9V) {
		ret = hvdcp_set_vbus_voltage(info, ADAPTER_9V);
		if (ret < 0) {
			info->hvdcp_err_count++;
			pr_err("adaptor voltage set fail, exit\n");
			return 0;
		}

		v_dpm = (ADAPTER_9V * ADAPTER_V_TO_UV) - HVDCP_VBUS_IR_DROP_THRESHOLD;
		pr_info("set mivr to %dmV\n", _uV_to_mV(v_dpm));
		set_hvdcp_dev_set_mivr(info, v_dpm);
	}

	set_hvdcp_dev_enable(info);
	set_hvdcp_constant_voltage_limit(info);
	set_hvdcp_charging_current_limit(info);

	info->polling_interval = pdata->hvdcp_polling_interval;
	info->hvdcp_err_count = 0;

	return ret;
}




