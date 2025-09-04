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

#define pr_fmt(fmt)	"[cne-charge][intf] %s: " fmt, __func__

#include "cne_intf.h"
#include "cne_lib.h"

static struct charger_manager *pinfo = NULL;

struct charger_manager *get_charger_manager(void)
{
	return pinfo;
}

int charger_is_chip_enabled(bool *en)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return charger_dev_is_chip_enabled(info->chg1_dev, en);
}

int charger_device_init(void)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;

	if (!info)
		return -1;

	if(info->chg1_dev && info->chg2_dev) {
		charger_dev_set_input_current(info->chg1_dev, IINDPM_DEFAULT_CURRENT);

		ret = charger_dev_init(info->chg2_dev);
		charger_dev_set_input_current(info->chg2_dev, 0);
		charger_dev_set_charging_current(info->chg2_dev, 0);
	} else if (info->chg2_dev) {
		ret = charger_dev_init(info->chg2_dev);
		charger_dev_set_input_current(info->chg2_dev, 0);
		charger_dev_set_charging_current(info->chg2_dev, 0);
	}

	return ret;
}

int charger_enable_chip(bool en)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return charger_dev_enable_chip(info->chg1_dev, en);
}

int charger_is_enabled(bool *en)
{
	struct charger_manager *info = get_charger_manager();
	bool en1, en2;
	int ret = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		ret = charger_dev_is_enabled(info->chg1_dev, &en1);
		if (ret < 0) {
			pr_info("get dev1 enable state fail\n");
			return ret;
		}
		ret = charger_dev_is_enabled(info->chg2_dev, &en2);
		if (ret < 0) {
			pr_info("get dev2 enable state fail\n");
			return ret;
		}

		*en = (en1 || en2);
	} else {
		ret = charger_dev_is_enabled(info->chg1_dev, en);
	}

	return ret;
}

int charger_get_mivr_state(bool *in_loop)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return charger_dev_get_mivr_state(info->chg1_dev, in_loop);
}

int charger_get_mivr(u32 *uV)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return charger_dev_get_mivr(info->chg1_dev, uV);
}

int charger_set_mivr(u32 uV)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		ret = charger_dev_set_mivr(info->chg1_dev, uV);
		ret = charger_dev_set_mivr(info->chg2_dev, uV);
	} else {
		ret = charger_dev_set_mivr(info->chg1_dev, uV);
	}

	return ret;
}

int charger_get_input_current(u32 *uA)
{
	struct charger_manager *info = get_charger_manager();
	u32 tmp;
	int ret = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		if (info->is_dual_charger_enable) {
			ret = charger_dev_get_input_current(info->chg1_dev, uA);
			if (ret < 0) {
				pr_err("get dev1 input current fail\n");
				return ret;
			}

			ret = charger_dev_get_input_current(info->chg2_dev, &tmp);
			if (ret < 0) {
				pr_err("get dev2 input current fail\n");
				return ret;
			}

			*uA += tmp;
		} else
			ret = charger_dev_get_input_current(info->chg1_dev, uA);
	} else
		ret = charger_dev_get_input_current(info->chg1_dev, uA);

	return ret;
}

int charger_set_input_current(u32 uA)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;
	int input_master = 0, input_slave = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		if (info->is_dual_charger_enable) {
			input_master = ((uA / 2) /
				CHARGER_DEV_IINDPM_STEP) * CHARGER_DEV_IINDPM_STEP;
			input_slave = uA - input_master;

			pr_info("use dual ic, input current: %d(%d %d)mA\n", _uA_to_mA(uA),
					_uA_to_mA(input_master), _uA_to_mA(input_slave));

			ret = charger_dev_set_input_current(info->chg2_dev, input_slave);
			ret += charger_dev_get_input_current(info->chg2_dev, &input_slave);
			if (ret < 0) {
				pr_err("set or get chg2_dev input current fail\n");
				return ret;
			}

			input_master = uA - input_slave;
			ret = charger_dev_set_input_current(info->chg1_dev, input_master);
			if (ret < 0) {
				pr_err("set chg1_dev input current fail\n");
				return ret;
			}
			pr_info("use dual ic, final set input current is: %d(%d %d)mA\n", _uA_to_mA(uA),
					_uA_to_mA(input_master), _uA_to_mA(input_slave));
		} else {
			pr_info("use single ic, input current:%d mA\n", _uA_to_mA(uA));

			ret = charger_dev_set_input_current(info->chg1_dev, uA);
			if (ret < 0) {
				pr_err("set chg1_dev input current fail\n");
				return ret;
			}
		}
	} else {
		ret = charger_dev_set_input_current(info->chg1_dev, uA);
		if (ret < 0) {
			pr_err("set chg1_dev input current fail\n");
			return ret;
		}
	}

	return ret;
}

int charger_set_charging_current(u32 uA)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;
	int curr = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		if (info->is_dual_charger_enable) {
			curr = uA / 2;
			pr_info("use dual ic to charge, current:(%d %d)mA\n",
				_uA_to_mA(uA), _uA_to_mA(curr));
			ret = charger_step_set_current(info, info->chg1_dev, curr);
			ret += charger_step_set_current(info, info->chg2_dev, curr);
			if (ret < 0) {
				pr_err("set chg1_dev or chg2_dev charging current fail\n");
				return ret;
			}
		} else {
			pr_info("use single ic to charge, current:%d mA\n", _uA_to_mA(uA));
			ret = charger_dev_set_charging_current(info->chg1_dev, uA);
			if (ret < 0) {
				pr_err("set chg1_dev charging current fail\n");
				return ret;
			}
		}

	} else {
		ret = charger_dev_set_charging_current(info->chg1_dev, uA);
		if (ret < 0) {
			pr_err("set chg1_dev charging current fail\n");
			return ret;
		}
	}
	return ret;
}

enum charger_use_state charger_use_charger_ic_check(bool enable, bool en_hv, u32 i_limit)
{
	struct charger_manager *info = get_charger_manager();
	struct sw_jeita_data *jeita = &info->sw_jeita;
	bool use_quick = true;
	int ret = 0;
	int soc;

	if (!info)
		return CHARGER_STATE_ERROR;

	if (info->chg1_dev && info->chg2_dev) {
		/* charging disable, normal charge */
		if (!enable)
			use_quick = false;

		/* adapter not support high voltage */
		if (!en_hv)
			use_quick = false;

		/* jeita limit */
		if (info->enable_sw_jeita) {
			if (jeita->sm != TEMP_T2_TO_T3 && jeita->sm != TEMP_T3_TO_T4)
				use_quick = false;
		}

		/* battery charging limit */
		if (i_limit <= info->data.pd_ichg_level_threshold)
			use_quick = false;

		/* soc limit */
		soc  = battery_get_bat_capacity(info);
		if (soc > info->data.pd_stop_battery_soc)
			use_quick = false;

		pr_info("enable:%d sm:(%d %d) current:(%d %d) soc:%d use_quick:%d\n",
			enable, info->enable_sw_jeita, jeita->sm,
			i_limit, info->data.pd_ichg_level_threshold,
			soc, use_quick);

		if (!use_quick) {
			pr_info("use single charge ic\n");

			info->is_dual_charger_enable = false;

			ret = charger_dev_enable(info->chg2_dev, false);
			if (ret < 0 && ret != -ENOTSUPP)
				return CHARGER_STATE_ERROR;

			ret = charger_dev_set_charging_current(info->chg2_dev, 0);
			if (ret < 0 && ret != -ENOTSUPP)
				return CHARGER_STATE_ERROR;

			return CHARGER_STATE_NORMAL;

		} else {
			pr_info("use dual charge ic\n");
			info->is_dual_charger_enable = true;
			return CHARGER_STATE_QUICK;
		}
	}

	return CHARGER_STATE_NORMAL;
}

int charger_get_ibus(u32 *ibus)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return charger_dev_get_ibus(info->chg1_dev, ibus);
}

int charger_get_vbat(u32 *uV)
{
	struct charger_manager *info = get_charger_manager();
	int vbat;

	if (!info)
		return -1;

	vbat = battery_get_bat_voltage(info);
	if (vbat < 0) {
		pr_err("get vbat fail\n");
		return -EINVAL;
	}

	*uV = vbat;

	return 0;
}

int charger_set_constant_voltage(u32 uV)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		ret = charger_dev_set_constant_voltage(info->chg1_dev, uV);
		if (ret < 0) {
			pr_err("set chg1_dev cv fail\n");
			return ret;
		}

		ret = charger_dev_set_constant_voltage(info->chg2_dev, uV);
		if (ret < 0) {
			pr_err("set chg2_dev cv fail\n");
			return ret;
		}
	} else {
		ret = charger_dev_set_constant_voltage(info->chg1_dev, uV);
		if (ret < 0) {
			pr_err("set chg1_dev cv fail\n");
			return ret;
		}
	}

	return ret;
}

int charger_enable_termination(bool en)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return charger_dev_enable_termination(info->chg1_dev, en);
}

int charger_enable_powerpath(bool en)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		ret = charger_dev_enable_powerpath(info->chg1_dev, en);
		ret += charger_dev_enable_powerpath(info->chg2_dev, en);
		if (ret < 0)
			pr_err("enable dual powerpath fail\n");
	} else {
		ret = charger_dev_enable_powerpath(info->chg1_dev, en);
		if (ret < 0)
			pr_err("enable single powerpath fail\n");
	}

	return ret;
}

int charger_enable_charging(bool en)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;

	if (!info)
		return -1;

	pr_info("en:%d\n", en);

	if (info->chg1_dev && info->chg2_dev) {
		if (info->is_dual_charger_enable) {
			ret = charger_dev_enable(info->chg1_dev, en);
			ret += charger_dev_enable(info->chg2_dev, en);
			if (ret < 0)
				pr_err("enable dual charge fail\n");
		} else {
			ret = charger_dev_enable(info->chg1_dev, en);
			ret += charger_dev_enable(info->chg2_dev, false);
			if (ret < 0)
				pr_err("enable single charge fail\n");
		}
	} else {
		ret = charger_dev_enable_powerpath(info->chg1_dev, en);
		if (ret < 0)
			pr_err("enable charge fail\n");
	}

	return ret;
}

int charger_dump_registers(void)
{
	struct charger_manager *info = get_charger_manager();
	int ret = 0;

	if (!info)
		return -1;

	if (info->chg1_dev && info->chg2_dev) {
		ret = charger_dev_dump_registers(info->chg1_dev);
		ret += charger_dev_dump_registers(info->chg2_dev);
		if (ret < 0)
			pr_err("dual ic dump registers fail\n");
	} else {
		ret = charger_dev_dump_registers(info->chg1_dev);
		if (ret < 0)
			pr_err("single ic dump registers fail\n");
	}
	return ret;
}

int adapter_set_1st_cap(int mV, int mA)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return adapter_dev_set_cap(info->pd_adapter,
		CNE_PD_APDO_START, mV, mA);
}

int adapter_set_cap(int mV, int mA)
{
	struct charger_manager *info = get_charger_manager();
	int ret;

	if (!info)
		return -1;

	ret = adapter_dev_set_cap(info->pd_adapter, CNE_PD_APDO, mV, mA);
	if (ret == CNE_ADAPTER_REJECT)
		return ADAPTER_REJECT;
	else if (ret != 0)
		return ADAPTER_ERROR;

	return ADAPTER_OK;
}

int adapter_set_cap_start(int mV, int mA)
{
	struct charger_manager *info = get_charger_manager();
	int ret;

	if (!info)
		return -1;

	ret = adapter_dev_set_cap(info->pd_adapter, CNE_PD_APDO_START, mV, mA);
	if (ret == CNE_ADAPTER_REJECT)
		return ADAPTER_REJECT;
	else if (ret != 0)
		return ADAPTER_ERROR;

	return ADAPTER_OK;
}

int adapter_set_cap_end(int mV, int mA)
{
	struct charger_manager *info = get_charger_manager();
	int ret;

	if (!info)
		return -1;

	ret = adapter_dev_set_cap(info->pd_adapter, CNE_PD_APDO_END, mV, mA);
	if (ret == CNE_ADAPTER_REJECT)
		return ADAPTER_REJECT;
	else if (ret != 0)
		return ADAPTER_ERROR;

	return ADAPTER_OK;
}

int adapter_get_output(int *mV, int *mA)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return adapter_dev_get_output(info->pd_adapter, mV, mA);
}

int adapter_get_status(struct ta_status *sta)
{
	struct charger_manager *info = get_charger_manager();
	struct adapter_status tasta = {0};
	int ret;

	if (!info)
		return -1;

	ret = adapter_dev_get_status(info->pd_adapter, &tasta);

	sta->temperature = tasta.temperature;
	sta->ocp = tasta.ocp;
	sta->otp = tasta.otp;
	sta->ovp = tasta.ovp;

	return ret;
}

int adapter_is_support_pd_pps(void)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	if (info->pd_type == CNE_PD_CONNECT_PE_READY_SNK_APDO)
		return true;

	return false;
}

int adapter_get_cap(struct pd_cap *cap)
{
	struct charger_manager *info = get_charger_manager();
	struct adapter_power_cap tacap = {0};
	int i;

	if (!info)
		return -1;

	adapter_dev_get_cap(info->pd_adapter, CNE_PD, &tacap);

	cap->selected_cap_idx = tacap.selected_cap_idx;
	cap->nr = tacap.nr;

	for (i = 0; i < tacap.nr; i++) {
		cap->max_mv[i] = tacap.max_mv[i];
		cap->min_mv[i] = tacap.min_mv[i];
		cap->ma[i] = tacap.ma[i];
		cap->maxwatt[i] = tacap.maxwatt[i];
		cap->minwatt[i] = tacap.minwatt[i];
		cap->type[i] = tacap.type[i];
	}

	return 0;
}

int adapter_is_support_pd(void)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	pr_info("pd_type:%d\n", info->pd_type);
	if (info->pd_type == CNE_PD_CONNECT_PE_READY_SNK ||
		info->pd_type == CNE_PD_CONNECT_PE_READY_SNK_PD30)
		return true;

	return false;
}

int set_charger_manager(struct charger_manager *info)
{
	if (pinfo == NULL)
		pinfo = info;

	return 0;
}

int enable_vbus_ovp(bool en)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	charger_enable_vbus_ovp(info, en);

	return 0;
}

int wake_up_charger(void)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	_wake_up_charger(info);

	return 0;
}

int battery_get_vbus(void)
{
	struct charger_manager *info = get_charger_manager();
	u32 vbus;
	int ret;

	if (!info)
		return -1;

	ret = charger_dev_get_vbus(info->chg1_dev, &vbus);
	if (ret < 0) {
		pr_err("get vbus failed, ret=%d\n", ret);
		return ret;
	}

	return vbus;
}

int battery_get_soc(void)
{
	struct charger_manager *info = get_charger_manager();

	if (!info)
		return -1;

	return battery_get_bat_capacity(info);
}

int battery_get_property(struct charger_manager *info,
	enum power_supply_property psp, int *out)
{
	int rc = 0;
	union power_supply_propval pval = {0, };

	if (!info->batt_psy) {
		info->batt_psy = power_supply_get_by_name("battery");
		if (!info->batt_psy) {
			chr_err("Couldn't find battery psy\n");
 			return -EINVAL;
		}
	}

	rc = power_supply_get_property(info->batt_psy,
			psp, &pval);
	if (rc < 0) {
		chr_err("Couldn't get battery prop:%d\n", psp);
		return -EINVAL;
	}

	*out = pval.intval;

	return rc;
}

int battery_set_property(struct charger_manager *info,
				enum power_supply_property prop,
				const union power_supply_propval *val)
{
	int rc = 0;

	if (!info->batt_psy) {
		info->batt_psy = power_supply_get_by_name("battery");
		if (!info->batt_psy) {
			chr_err("Couldn't find battery psy\n");
			return -EINVAL;
		}
	}

	rc = power_supply_set_property(info->batt_psy,
			prop, val);
	if (rc < 0)
		chr_err("Couldn't set battery prop:%d\n", prop);

	return rc;
}

int battery_get_bat_current(struct charger_manager *info)
{
	int ibat = 0;
	int ret = 0;

	ret = battery_get_property(info,
			POWER_SUPPLY_PROP_CURRENT_NOW, &ibat);
	if (ret < 0)
		return ret;
	else
		return ibat;
}

int battery_get_bat_voltage(struct charger_manager *info)
{
	int vbat = 0;
	int ret = 0;

	ret = battery_get_property(info,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &vbat);
	if (ret < 0)
		return ret;
	else
		return vbat;
}

int battery_get_bat_capacity(struct charger_manager *info)
{
	int cap = 0;
	int ret = 0;

	ret = battery_get_property(info,
			POWER_SUPPLY_PROP_CAPACITY, &cap);
	if (ret < 0)
		return ret;
	else
		return cap;
}

int battery_get_bat_real_capacity(struct charger_manager *info)
{
	int cap = 0;
	int ret = 0;

	ret = battery_get_property(info,
			POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX, &cap);
	if (ret < 0)
		return ret;
	else
		return cap;
}

int battery_get_bat_temperature(struct charger_manager *info)
{
	int temp, temp_comp, ibat;
	int i = 0;
	int ret = 0;

	ret = battery_get_property(info,
			POWER_SUPPLY_PROP_TEMP, &temp);
	if (ret < 0) {
		pr_err("get battery temp fail, ret = %d\n", ret);
		return ret;
	}

	if (info->temp_comp_len > 0) {
		ret = battery_get_property(info,
				POWER_SUPPLY_PROP_CURRENT_NOW, &ibat);
		if (ret < 0) {
			pr_err("get battery current fail, return uncomp temp = %d, ret= %d\n", temp, ret);
			return temp;
		}

		temp_comp = temp;
		for (i = info->temp_comp_len; i > 0; i--) {
			if(ibat > info->temp_comp[i - 1].ibat_min) {
				temp_comp = temp - info->temp_comp[i - 1].temp_comp;
				break;
			}
		}

		pr_info("temp comp: before(t:%d i:%d) after(t:%d)\n",
				temp, _uA_to_mA(ibat), temp_comp);

		return temp_comp;
	}

	return temp;
}

bool cne_is_TA_support_pd_pps(struct charger_manager *pinfo)
{
	if (pinfo->pd_type == CNE_PD_CONNECT_PE_READY_SNK_APDO)
		return true;
	return false;
}
