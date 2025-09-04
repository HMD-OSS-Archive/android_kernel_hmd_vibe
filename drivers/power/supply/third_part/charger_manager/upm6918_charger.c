/*
 * UPM6918D battery charging driver
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

#define pr_fmt(fmt)	"[cne-charge][upm6918dl]:%s: " fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/extcon-provider.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>

#include "upm6918_reg.h"
#include "upm6918.h"
#include "charger_class.h"
#include "cne_charger_intf.h"
#include "cne_charger.h"

#define BC12_CHECK_DEFAULT_DELAY_TIME_MS 150
#define BC12_CHECK_DEFAULT_DELAY_COUNT 5
#define HVDCP_HV_THRESHOLD_MV_MIN 7500
#define HVDCP_HV_DETECT_RETRY 5
#define HVDCP_DP_0P6V_HOLD_TIME_MS 1500
#define HVDCP_CHECK_DEFAULT_DELAY_COUNT 15

enum {
	PN_UPM6918D,
};

enum upm6918_part_no {
	UPM6918D = 0x02,
};

static int pn_data[] = {
	[PN_UPM6918D] = 0x02,
};

static char *pn_str[] = {
	[PN_UPM6918D] = "upm6918d",
};

enum detect_stage_type {
	DETECT_TYPE_UNKNOW,
	DETECT_TYPE_CONNECT,
	DETECT_TYPE_CHECK,
	DETECT_TYPE_CHECK_DONE,
	DETECT_TYPE_DISCONNECT,
	DETECT_TYPE_HVDCP,
	DETECT_TYPE_HVDCP_CHECK,
};

struct upm6918_state {
	bool vsys_stat;
	bool therm_stat;
	bool online;	
	u8 chrg_stat;
	u8 vbus_status;
	bool chrg_en;
	bool hiz_en;
	bool term_en;
	bool vbus_gd;
	u8 chrg_type;
	u8 health;
	u8 chrg_fault;
	u8 ntc_fault;
};

struct upm6918 {
	struct device *dev;
	struct i2c_client *client;
	enum upm6918_part_no part_no;
	bool dpdm_enabled;
	bool vbus_gd;
	int irq;
	u32 intr_gpio;
	const char *chg_dev_name;
	enum detect_stage_type detect_stage;
	struct mutex i2c_rw_lock;
	struct mutex regulator_lock;
	struct mutex detect_lock;
	struct power_supply *charger;
	struct upm6918_platform_data *platform_data;
	struct upm6918_state state;
	struct charger_device *chg_dev;
	struct task_struct *detect_task;
	struct regulator *dpdm_reg;
	wait_queue_head_t waitq;
	struct wakeup_source *charger_wakelock;
	struct delayed_work charger_change_work;

	/* pinctrl parameters */
	const char		*pinctrl_state_name;
	struct pinctrl		*irq_pinctrl;
	struct iio_channel *vbus_iio_channel;

	/* charger detect*/
	bool attach;
	bool tcpc_attach;
	bool ignore_detect;
	atomic_t chg_type_check_cnt;
	atomic_t vbus_check_cnt;
	atomic_t input_check_cnt;
	bool enable_hvdcp_check;
	ktime_t hvdcp_check_begin_time;
	int hvdcp_detect_retry_count;
	enum charger_type chg_type;
};

static int __upm6918_read_reg(struct upm6918 *upm, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(upm->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n, ret=%d", reg, ret);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __upm6918_write_reg(struct upm6918 *upm, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(upm->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}
	return 0;
}

static int upm6918_read_byte(struct upm6918 *upm, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&upm->i2c_rw_lock);
	ret = __upm6918_read_reg(upm, reg, data);
	mutex_unlock(&upm->i2c_rw_lock);

	return ret;
}

static int upm6918_write_byte(struct upm6918 *upm, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&upm->i2c_rw_lock);
	ret = __upm6918_write_reg(upm, reg, data);
	mutex_unlock(&upm->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

static int upm6918_update_bits(struct upm6918 *upm, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&upm->i2c_rw_lock);
	ret = __upm6918_read_reg(upm, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __upm6918_write_reg(upm, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&upm->i2c_rw_lock);
	return ret;
}

static int upm6918_enter_hiz_mode(struct upm6918 *upm)
{
	u8 val = REG00_HIZ_ENABLE << REG00_ENHIZ_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_00, REG00_ENHIZ_MASK, val);
}

static int upm6918_exit_hiz_mode(struct upm6918 *upm)
{
	u8 val = REG00_HIZ_DISABLE << REG00_ENHIZ_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_00, REG00_ENHIZ_MASK, val);
}

static int upm6918_set_stat_ctrl(struct upm6918 *upm, int ctrl)
{
	u8 val;

	val = ctrl;

	return upm6918_update_bits(upm, UPM6918_REG_00, REG00_STAT_CTRL_MASK,
				   val << REG00_STAT_CTRL_SHIFT);
}

static int upm6918_get_input_current_limit(struct upm6918 *upm, int *ilim)
{
	u8 iin;
	int ret;
	int temp;

	ret = upm6918_read_byte(upm, UPM6918_REG_00, &iin);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6918_REG_00, iin);
	}

	temp = (iin & REG00_IINLIM_MASK) >> REG00_IINLIM_SHIFT;
	temp = temp * REG00_IINLIM_LSB + REG00_IINLIM_BASE;
	*ilim = temp * 1000;
	pr_err("%s: get input curr %d uA\n", __func__, *ilim);

	return ret;
}

static int upm6918_set_input_current_limit(struct upm6918 *upm, int curr)
{
	u8 val;

	if (curr < REG00_IINLIM_BASE)
		curr = REG00_IINLIM_BASE;

	val = (curr - REG00_IINLIM_BASE) / REG00_IINLIM_LSB;
	return upm6918_update_bits(upm, UPM6918_REG_00, REG00_IINLIM_MASK,
				   val << REG00_IINLIM_SHIFT);
}

static int upm6918_reset_watchdog_timer(struct upm6918 *upm)
{
	u8 val = REG01_WDT_RESET << REG01_WDT_RESET_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_01, REG01_WDT_RESET_MASK,
				   val);
}

static int upm6918_enable_otg(struct upm6918 *upm)
{
	u8 val = REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_01, REG01_OTG_CONFIG_MASK,
				   val);
}

static int upm6918_disable_otg(struct upm6918 *upm)
{
	u8 val = REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_01, REG01_OTG_CONFIG_MASK,
				   val);
}

static int upm6918_enable_charger(struct upm6918 *upm)
{
	int ret;
	u8 val = REG01_CHG_ENABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    upm6918_update_bits(upm, UPM6918_REG_01, REG01_CHG_CONFIG_MASK, val);

	return ret;
}

static int upm6918_disable_charger(struct upm6918 *upm)
{
	int ret;
	u8 val = REG01_CHG_DISABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    upm6918_update_bits(upm, UPM6918_REG_01, REG01_CHG_CONFIG_MASK, val);
	return ret;
}

static int upm6918_set_boost_current(struct upm6918 *upm, int curr)
{
	u8 val;

	val = REG02_BOOST_LIM_0P5A;
	if (curr == BOOSTI_1200)
		val = REG02_BOOST_LIM_1P2A;

	return upm6918_update_bits(upm, UPM6918_REG_02, REG02_BOOST_LIM_MASK,
				   val << REG02_BOOST_LIM_SHIFT);
}

static int upm6918_get_chargecurrent(struct upm6918 *upm, int *curr)
{
	u8 ichg;
	int ret;
	int temp;

	ret = upm6918_read_byte(upm, UPM6918_REG_02, &ichg);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6918_REG_02, ichg);
	}

	temp = (ichg & REG02_ICHG_MASK) >> REG02_ICHG_SHIFT;
	temp = temp * REG02_ICHG_LSB + REG02_ICHG_BASE;
	*curr = temp * 1000;
	pr_err("%s: get charging curr %d mA\n", __func__, *curr);

	return ret;
}

static int upm6918_set_chargecurrent(struct upm6918 *upm, int curr)
{
	u8 ichg;

	if (curr < REG02_ICHG_BASE)
		curr = REG02_ICHG_BASE;

	ichg = (curr - REG02_ICHG_BASE) / REG02_ICHG_LSB;
	return upm6918_update_bits(upm, UPM6918_REG_02, REG02_ICHG_MASK,
				   ichg << REG02_ICHG_SHIFT);

}

static int upm6918_set_prechg_current(struct upm6918 *upm, int curr)
{
	u8 iprechg;

	if (curr < REG03_IPRECHG_BASE)
		curr = REG03_IPRECHG_BASE;

	iprechg = (curr - REG03_IPRECHG_BASE) / REG03_IPRECHG_LSB;

	return upm6918_update_bits(upm, UPM6918_REG_03, REG03_IPRECHG_MASK,
				   iprechg << REG03_IPRECHG_SHIFT);
}

static int upm6918_set_term_current(struct upm6918 *upm, int curr)
{
	u8 iterm;

	if (curr < REG03_ITERM_BASE)
		curr = REG03_ITERM_BASE;

	iterm = (curr - REG03_ITERM_BASE) / REG03_ITERM_LSB;

	return upm6918_update_bits(upm, UPM6918_REG_03, REG03_ITERM_MASK,
				   iterm << REG03_ITERM_SHIFT);
}

static int upm6918_get_chargevolt(struct upm6918 *upm, int *volt)
{
	u8 volt_reg;
	int ret;
	int temp;

	ret = upm6918_read_byte(upm, UPM6918_REG_04, &volt_reg);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6918_REG_04, volt_reg);
	}

	temp = (volt_reg & REG04_VREG_MASK) >> REG04_VREG_SHIFT;
	temp = temp * REG04_VREG_LSB + REG04_VREG_BASE;
	*volt = temp * 1000;
	pr_err("%s: get CV %d uV\n", __func__, *volt);

	return ret;
}

static int upm6918_set_chargevolt(struct upm6918 *upm, int volt)
{
	u8 val;

	if (volt < REG04_VREG_BASE)
		volt = REG04_VREG_BASE;

	val = (volt - REG04_VREG_BASE) / REG04_VREG_LSB;
	return upm6918_update_bits(upm, UPM6918_REG_04, REG04_VREG_MASK,
				   val << REG04_VREG_SHIFT);
}

/*
static int upm6918_enable_term(struct upm6918 *upm, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = REG05_TERM_ENABLE << REG05_EN_TERM_SHIFT;
	else
		val = REG05_TERM_DISABLE << REG05_EN_TERM_SHIFT;

	ret = upm6918_update_bits(upm, UPM6918_REG_05, REG05_EN_TERM_MASK, val);

	return ret;
}*/

static int upm6918_set_watchdog_timer(struct upm6918 *upm, u8 timeout)
{
	u8 temp;

	temp = (u8) (((timeout -
		       REG05_WDT_BASE) / REG05_WDT_LSB) << REG05_WDT_SHIFT);

	return upm6918_update_bits(upm, UPM6918_REG_05, REG05_WDT_MASK, temp);
}

static int upm6918_disable_watchdog_timer(struct upm6918 *upm)
{
	u8 val = REG05_WDT_DISABLE << REG05_WDT_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_05, REG05_WDT_MASK, val);
}

static int upm6918_enable_safety_timer(struct upm6918 *upm)
{
	const u8 val = REG05_CHG_TIMER_ENABLE << REG05_EN_TIMER_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_05, REG05_EN_TIMER_MASK,
				   val);
}

static int upm6918_disable_safety_timer(struct upm6918 *upm)
{
	const u8 val = REG05_CHG_TIMER_DISABLE << REG05_EN_TIMER_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_05, REG05_EN_TIMER_MASK,
				   val);
}

static int upm6918_set_acovp_threshold(struct upm6918 *upm, int volt)
{
	u8 val;

	if (volt == VAC_OVP_14000)
		val = REG06_OVP_14P0V;
	else if (volt == VAC_OVP_10500)
		val = REG06_OVP_10P5V;
	else if (volt == VAC_OVP_6500)
		val = REG06_OVP_6P5V;
	else
		val = REG06_OVP_5P7V;

	return upm6918_update_bits(upm, UPM6918_REG_06, REG06_OVP_MASK,
				   val << REG06_OVP_SHIFT);
}

static int upm6918_set_boost_voltage(struct upm6918 *upm, int volt)
{
	u8 val;

	if (volt <= BOOSTV_4850)
		val = REG06_BOOSTV_4P85V;
	else if (volt <= BOOSTV_5150)
		val = REG06_BOOSTV_5P15V;
	else if (volt <= REG06_BOOSTV_5V)
		val = REG06_BOOSTV_5V;
	else
		val = REG06_BOOSTV_5P3V;

	return upm6918_update_bits(upm, UPM6918_REG_06, REG06_BOOSTV_MASK,
				   val << REG06_BOOSTV_SHIFT);
}

static int upm6918_set_input_volt_limit(struct upm6918 *upm, int volt)
{
	u8 val;

	if (volt > REG06_VINDPM_HIGH) {
		pr_err("upm6918 did not HV VINDPM!\n");
		return -1;
	}

	if (volt < REG06_VINDPM_BASE)
		volt = REG06_VINDPM_BASE;

	val = (volt - REG06_VINDPM_BASE) / REG06_VINDPM_LSB;
	return upm6918_update_bits(upm, UPM6918_REG_06, REG06_VINDPM_MASK,
				   val << REG06_VINDPM_SHIFT);
}

static int upm6918_force_dpdm(struct upm6918 *upm)
{
	int ret = 0;
	const u8 val = REG07_FORCE_DPDM << REG07_FORCE_DPDM_SHIFT;

	ret = upm6918_update_bits(upm, UPM6918_REG_07,
			REG07_FORCE_DPDM_MASK, val);

	return ret;
}

/*
static int upm6918_enable_batfet(struct upm6918 *upm)
{
	const u8 val = REG07_BATFET_ON << REG07_BATFET_DIS_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}

static int upm6918_disable_batfet(struct upm6918 *upm)
{
	const u8 val = REG07_BATFET_OFF << REG07_BATFET_DIS_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}

static int upm6918_set_batfet_delay(struct upm6918 *upm, uint8_t delay)
{
	u8 val;

	if (delay == 0)
		val = REG07_BATFET_DLY_0S;
	else
		val = REG07_BATFET_DLY_10S;

	val <<= REG07_BATFET_DLY_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_07, REG07_BATFET_DLY_MASK,
				   val);
}

static int upm6918_batfet_rst_en(struct upm6918 *upm, bool en)
{
	u8 val = 0;

	if (en) {
		val = REG07_BATFET_RST_ENABLE << REG07_BATFET_RST_EN_SHIFT;
	} else {
		val = REG07_BATFET_RST_DISABLE << REG07_BATFET_RST_EN_SHIFT;
	}

	return upm6918_update_bits(upm, UPM6918_REG_07, REG07_BATFET_RST_EN_MASK,
				   val);
}

static int upm6918_request_dpdm(struct upm6918 *upm, bool enable)
{
	int ret = 0;

	mutex_lock(&upm->regulator_lock);
	// fetch the DPDM regulator
	if (!upm->dpdm_reg && of_get_property(upm->dev->of_node, "dpdm-supply", NULL)) {
		upm->dpdm_reg = devm_regulator_get(upm->dev, "dpdm");
		if (IS_ERR(upm->dpdm_reg)) {
			ret = PTR_ERR(upm->dpdm_reg);
			pr_err("Couldn't get dpdm regulator ret=%d\n", ret);
			upm->dpdm_reg = NULL;
			mutex_unlock(&upm->regulator_lock);
			return ret;
		}
	}

	if (enable) {
		if (upm->dpdm_reg && !upm->dpdm_enabled) {
		pr_err("enabling DPDM regulator\n");
		ret = regulator_enable(upm->dpdm_reg);
		if (ret < 0)
			pr_err("Couldn't enable dpdm regulator ret=%d\n", ret);
		else
			upm->dpdm_enabled = true;
		}
	} else {
		if (upm->dpdm_reg && upm->dpdm_enabled) {
			pr_err("disabling DPDM regulator\n");
			ret = regulator_disable(upm->dpdm_reg);
			if (ret < 0)
				pr_err("Couldn't disable dpdm regulator ret=%d\n", ret);
			else
				upm->dpdm_enabled = false;
		}
	}
	mutex_unlock(&upm->regulator_lock);

	return ret;
}

static int upm6918_get_charger_type(struct upm6918 *upm, int *type)
{
	int ret;
	u8 reg_val = 0;
	int vbus_stat = 0;
	int chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	ret = upm6918_read_byte(upm, UPM6918_REG_08, &reg_val);
	if (ret)
		return ret;

	vbus_stat = (reg_val & REG08_VBUS_STAT_MASK);
	vbus_stat >>= REG08_VBUS_STAT_SHIFT;

	pr_err("reg08: 0x%02x\n", reg_val);

	switch (vbus_stat) {
	case REG08_VBUS_TYPE_NONE:
		chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		break;
	case REG08_VBUS_TYPE_SDP:
		chg_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case REG08_VBUS_TYPE_CDP:
		chg_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case REG08_VBUS_TYPE_DCP:
		chg_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case REG08_VBUS_TYPE_UNKNOWN:
		chg_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case REG08_VBUS_TYPE_NON_STD:
		chg_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case REG08_VBUS_TYPE_OTG:
		chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		break;
	default:
		chg_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	}

	if (chg_type == POWER_SUPPLY_USB_TYPE_UNKNOWN) {
		pr_err("unknown type\n");
	} else if (chg_type == POWER_SUPPLY_USB_TYPE_SDP ||
		chg_type == POWER_SUPPLY_USB_TYPE_CDP) {
		upm6918_request_dpdm(upm, false);
	} else {

	}

	*type = chg_type;

	return 0;
}

static int upm6918_reset_chip(struct upm6918 *upm)
{
	int ret;
	u8 val = REG0B_REG_RESET << REG0B_REG_RESET_SHIFT;

	ret =
	    upm6918_update_bits(upm, UPM6918_REG_0B, REG0B_REG_RESET_MASK, val);
	return ret;
}
*/

static int upm6918_set_int_mask(struct upm6918 *upm, int mask)
{
	u8 val;

	val = mask;

	return upm6918_update_bits(upm, UPM6918_REG_0A, REG0A_INT_MASK_MASK,
				   val << REG0A_INT_MASK_SHIFT);
}

static int upm6918_detect_device(struct upm6918 *upm)
{
	int ret;
	u8 data;

	ret = upm6918_read_byte(upm, UPM6918_REG_0B, &data);
	if (!ret)
		upm->part_no = (data & REG0B_PN_MASK) >> REG0B_PN_SHIFT;

	return ret;
}

static int upm6918_enable_hvdcp(struct upm6918 *upm, bool en)
{
	const u8 val = en << REG0C_EN_HVDCP_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_0C, REG0C_EN_HVDCP_MASK,
				   val);
}

/*
static int upm6918_set_dp(struct upm6918 *upm, int dp_stat)
{
	const u8 val = dp_stat << REG0C_DP_MUX_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_0C, REG0C_DP_MUX_MASK,
				   val);
}

static int upm6918_set_dm(struct upm6918 *upm, int dm_stat)
{
	const u8 val = dm_stat << REG0C_DM_MUX_SHIFT;

	return upm6918_update_bits(upm, UPM6918_REG_0C, REG0C_DM_MUX_MASK,
				   val);
}
*/

static void upm6918_dump_regs(struct upm6918 *upm)
{
	int addr;
	u8 val;
	int ret;

	for (addr = 0x0; addr <= 0x0C; addr++) {
		ret = upm6918_read_byte(upm, addr, &val);
		if (ret == 0)
			pr_err("Reg[%.2x] = 0x%.2x\n", addr, val);
	}
}

static int upm6918_get_input_volt_limit(struct upm6918 *upm, int *volt)
{
	u8 val;
	int ret;
	int vindpm;

	ret = upm6918_read_byte(upm, UPM6918_REG_06, &val);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6918_REG_06, val);
	}

	//pr_err("%s: vindpm_reg_val:0x%X\n", __func__, val);
	vindpm = (val & REG06_VINDPM_MASK) >> REG06_VINDPM_SHIFT;
	vindpm = vindpm * REG06_VINDPM_LSB + REG06_VINDPM_BASE;
	*volt = vindpm * 1000;
	pr_err("%s: vindpm_reg_val:%d\n", __func__, *volt);

	return ret;
}

static struct upm6918_platform_data *upm6918_parse_dt(struct device_node *np,
						      struct upm6918 *upm)
{
	int ret;
	struct upm6918_platform_data *pdata;

	pdata = devm_kzalloc(&upm->client->dev, sizeof(struct upm6918_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_string(np, "charger_name", &upm->chg_dev_name) < 0) {
		upm->chg_dev_name = "primary_chg";
		pr_err("no charger name\n");
	}

	ret = of_get_named_gpio(np, "upm,intr_gpio", 0);
	if (ret < 0) {
		pr_err("%s no upm,intr_gpio(%d)\n", __func__, ret);
	} else
		upm->intr_gpio = ret;
	pr_err("%s intr_gpio = %u\n", __func__, upm->intr_gpio);

	ret = of_property_read_u32(np, "upm,upm6918,usb-vlim", &pdata->usb.vlim);
	if (ret) {
		pdata->usb.vlim = 4700;
		pr_err("Failed to read node of upm,upm6918,usb-vlim\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,usb-ilim", &pdata->usb.ilim);
	if (ret) {
		pdata->usb.ilim = 2000;
		pr_err("Failed to read node of upm,upm6918,usb-ilim\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,usb-vreg", &pdata->usb.vreg);
	if (ret) {
		pdata->usb.vreg = 4200;
		pr_err("Failed to read node of upm,upm6918,usb-vreg\n");
	}

	/* the low vreg caused charging terminated when the device reboot at the hight battery level */
	pdata->usb.vreg = 4450;

	ret = of_property_read_u32(np, "upm,upm6918,usb-ichg", &pdata->usb.ichg);
	if (ret) {
		pdata->usb.ichg = 2000;
		pr_err("Failed to read node of upm,upm6918,usb-ichg\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,stat-pin-ctrl",
				   &pdata->statctrl);
	if (ret) {
		pdata->statctrl = 0;
		pr_err("Failed to read node of upm,upm6918,stat-pin-ctrl\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,precharge-current", &pdata->iprechg);
	if (ret) {
		pdata->iprechg = 180;
		pr_err("Failed to read node of upm,upm6918,precharge-current\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,termination-current",
			&pdata->iterm);
	if (ret) {
		pdata->iterm = 180;
		pr_err("Failed to read node of upm,upm6918,termination-current\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,boost-voltage", &pdata->boostv);
	if (ret) {
		pdata->boostv = 5000;
		pr_err("Failed to read node of upm,upm6918,boost-voltage\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,boost-current", &pdata->boosti);
	if (ret) {
		pdata->boosti = 1200;
		pr_err("Failed to read node of upm,upm6918,boost-current\n");
	}

	ret = of_property_read_u32(np, "upm,upm6918,vac-ovp-threshold",
				   &pdata->vac_ovp);
	if (ret) {
		pdata->vac_ovp = 6500;
		pr_err("Failed to read node of upm,upm6918,vac-ovp-threshold\n");
	}

	/* configure irq_pinctrl to enable irqs */
	upm->pinctrl_state_name = of_get_property(upm->dev->of_node, "pinctrl-names", NULL);
	if (upm->pinctrl_state_name) {
		upm->irq_pinctrl = pinctrl_get_select(upm->dev,
						upm->pinctrl_state_name);
		if (IS_ERR(upm->irq_pinctrl)) {
			pr_err("Could not get/set %s pinctrl state rc = %ld\n",
						upm->pinctrl_state_name,
						PTR_ERR(upm->irq_pinctrl));
		}
	}

	dev_notice(upm->dev, "ichrg_curr:%d prechrg_curr:%d"
		" term_curr:%d input_curr_lim:%d"
		" cv:%d vindpm:%d ovp:%d\n",
		pdata->usb.ichg, pdata->iprechg,
		pdata->iterm, pdata->usb.ilim,
		pdata->usb.vreg, pdata->usb.vlim, pdata->vac_ovp);

	return pdata;
}

/* Notify charger driver to VBUS attach/disattach */
static int upm6918_notify_vbus_changed(int attach)
{
	int ret = 0;
	union power_supply_propval propval;
	struct power_supply *charger_psy = NULL;

	/* Get chg type det power supply */
	charger_psy = power_supply_get_by_name("charger");
	if (!charger_psy) {
		pr_err("charger psy get fail\n");
		return -EINVAL;
	}

	propval.intval = attach;
	ret = power_supply_set_property(charger_psy,
				POWER_SUPPLY_PROP_USB_TYPE, &propval);
	if (ret < 0)
		pr_err("psy vbus fail(%d)\n", ret);
	else
		pr_info("Type CC mode = %d\n", attach);

	return ret;
}

static int upm6918_init_device(struct upm6918 *upm)
{
	int ret;

	upm6918_disable_watchdog_timer(upm);
	ret = upm6918_set_input_current_limit(upm, upm->platform_data->usb.ilim);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret = upm6918_set_input_volt_limit(upm, upm->platform_data->usb.vlim);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret = upm6918_set_chargecurrent(upm, upm->platform_data->usb.ichg);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret = upm6918_set_chargevolt(upm, upm->platform_data->usb.vreg);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret = upm6918_set_stat_ctrl(upm, upm->platform_data->statctrl);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret = upm6918_set_prechg_current(upm, upm->platform_data->iprechg);
	if (ret)
		pr_err("Failed to set prechg current, ret = %d\n", ret);

	ret = upm6918_set_term_current(upm, upm->platform_data->iterm);
	if (ret)
		pr_err("Failed to set termination current, ret = %d\n", ret);

	ret = upm6918_set_boost_voltage(upm, upm->platform_data->boostv);
	if (ret)
		pr_err("Failed to set boost voltage, ret = %d\n", ret);

	ret = upm6918_set_boost_current(upm, upm->platform_data->boosti);
	if (ret)
		pr_err("Failed to set boost current, ret = %d\n", ret);

	ret = upm6918_set_acovp_threshold(upm, upm->platform_data->vac_ovp);
	if (ret)
		pr_err("Failed to set acovp threshold, ret = %d\n", ret);

	ret = upm6918_update_bits(upm, UPM6918_REG_04, REG04_VRECHG_MASK, 0x0);
	ret = upm6918_set_watchdog_timer(upm, REG05_WDT_40S);
	/* Enter and exit hiz clear charging terminated flag */
	ret = upm6918_enter_hiz_mode(upm);
	ret = upm6918_exit_hiz_mode(upm);
	ret = upm6918_set_int_mask(upm, REG0A_IINDPM_INT_MASK | REG0A_VINDPM_INT_MASK);
	if (ret)
		pr_err("Failed to set vindpm and iindpm int mask\n");

	return 0;
}

static ssize_t
upm6918_show_registers(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct upm6918 *upm = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "upm6918 Reg");
	for (addr = 0x0; addr <= 0x0B; addr++) {
		ret = upm6918_read_byte(upm, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t
upm6918_store_registers(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct upm6918 *upm = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < 0x0B) {
		upm6918_write_byte(upm, (unsigned char) reg,
				   (unsigned char) val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, upm6918_show_registers,
		   upm6918_store_registers);

static struct attribute *upm6918_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group upm6918_attr_group = {
	.attrs = upm6918_attributes,
};

static struct of_device_id upm6918_charger_match_table[] = {
	{
	 .compatible = "upm,upm6918d",
	 .data = &pn_data[PN_UPM6918D],
	 },
	{},
};
MODULE_DEVICE_TABLE(of, upm6918_charger_match_table);

/***********************************************************************
 *
 *  add funtions for charger device ops
 *
 ***********************************************************************/
#define UPM_NAME "UPM6918D"
#define UPM6918D_NAME "upm6918d"
#define R_CHARGER_1 300
#define R_CHARGER_2 39

static const struct charger_properties upm6918_chg_props = {
	.alias_name = UPM6918D_NAME,
};

static int upm6918_enable_hz(struct charger_device *chg_dev, bool hiz_en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);

	if(hiz_en)
		return upm6918_enter_hiz_mode(upm);
	else
		return upm6918_exit_hiz_mode(upm);
}

static int upm6918_dump_register(struct charger_device *chg_dev)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	
	upm6918_dump_regs(upm);
	
	return 0;
}

static int upm6918_charging_switch(struct charger_device *chg_dev, bool enable)
{
	int ret;
	struct upm6918 *upm = charger_get_data(chg_dev);

	pr_debug("enable:%d\n", enable);
	if (enable)
		ret = upm6918_enable_charger(upm);
	else
		ret = upm6918_disable_charger(upm);

	return ret;
}

static int upm6918_get_charging_current(struct charger_device *chg_dev, unsigned int *uA)
{
	int ret = 0;
	struct upm6918 *upm = charger_get_data(chg_dev);

	ret = upm6918_get_chargecurrent(upm, uA);

	return ret;
}

static int upm6918_set_charging_current(struct charger_device *chg_dev, unsigned int uA)
{
	int ret = 0;
	struct upm6918 *upm = charger_get_data(chg_dev);

	ret = upm6918_set_chargecurrent(upm, uA/1000);
	
	return ret;
}

static int upm6918_set_input_current(struct charger_device *chg_dev, unsigned int iindpm)
{
	struct upm6918 *upm = charger_get_data(chg_dev);

	return upm6918_set_input_current_limit(upm, iindpm/1000);
}

static int upm6918_get_input_current(struct charger_device *chg_dev,unsigned int *ilim)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	
	return upm6918_get_input_current_limit(upm, ilim);
}

static int upm6918_set_constant_voltage(struct charger_device *chg_dev, unsigned int chrg_volt)
{
	struct upm6918 *upm = charger_get_data(chg_dev);

	return upm6918_set_chargevolt(upm, chrg_volt/1000);
}

static int upm6918_get_constant_voltage(struct charger_device *chg_dev,unsigned int *volt)
{
	int ret;
	struct upm6918 *upm = charger_get_data(chg_dev);

	ret = upm6918_get_chargevolt(upm, volt);
	if (ret)
		return ret;

	return 0;
}

static int upm6918_reset_watch_dog_timer(struct charger_device
		*chg_dev)
{
	int ret;
	struct upm6918 *upm = charger_get_data(chg_dev);

	pr_debug("kick watch dog\n");

	ret =upm6918_reset_watchdog_timer(upm);	/* RST watchdog */	

	return ret;
}

static int upm6918_set_input_volt_lim(struct charger_device *chg_dev, unsigned int vindpm)
{
	struct upm6918 *upm = charger_get_data(chg_dev);

	return upm6918_set_input_volt_limit(upm, vindpm/1000);
}

static int upm6918_get_input_volt_lim(struct charger_device *chg_dev, u32 *uV)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	int ret = 0;

	ret = upm6918_get_input_volt_limit(upm, uV);

	return ret;
}

static int upm6918_get_charging_status(struct charger_device *chg_dev,
				       bool *is_done)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	u8 data, chrg_stat;
	u8 reg_val;
	int ret;

	*is_done = false;

	ret = upm6918_read_byte(upm, UPM6918_REG_08, &reg_val);
	if (ret)
		return ret;

	data = (reg_val & REG08_CHRG_STAT_MASK);
	chrg_stat = data >> REG08_CHRG_STAT_SHIFT;

	if (chrg_stat == REG08_CHRG_STAT_CHGDONE)
		*is_done = true;

	pr_debug("%s: charing is done %d\n", __func__, *is_done);

	return 0;
}

static int upm6918_is_enabled(struct charger_device *chg_dev, bool *en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	u8 data, chrg_stat;
	u8 reg_val;
	int ret;

	ret = upm6918_read_byte(upm, UPM6918_REG_08, &reg_val);
	if (ret)
		return ret;

	data = (reg_val & REG08_CHRG_STAT_MASK);
	chrg_stat = data >> REG08_CHRG_STAT_SHIFT;
	if (chrg_stat == REG08_CHRG_STAT_PRECHG || chrg_stat == REG08_CHRG_STAT_FASTCHG)
		*en = true;

	pr_debug("%s: charging enable is %d\n", __func__, *en);

	return 0;
}

static int upm6918_enable_safetytimer(struct charger_device *chg_dev,bool en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	int ret = 0;

	pr_debug("%s: en %d\n", __func__, en);
	if (en)
		ret = upm6918_enable_safety_timer(upm);
	else
		ret = upm6918_disable_safety_timer(upm);

	return ret;
}

static int upm6918_get_is_safetytimer_enable(struct charger_device
		*chg_dev,bool *en)
{
	int ret = 0;
	u8 val = 0;
	struct upm6918 *upm = charger_get_data(chg_dev);

	pr_debug("%s: en %d\n", __func__, en);
	ret = upm6918_read_byte(upm, UPM6918_REG_08, &val);
	if (ret < 0)
	{
		pr_info("read UPM6918_REG_05 fail\n");
		return ret;
	}
	*en = (val & REG05_EN_TIMER_MASK) >> REG05_EN_TIMER_SHIFT;

	return 0;
}

static int upm6918_enable_power_path(struct charger_device
		*chg_dev, bool en)
{
	return upm6918_charging_switch(chg_dev, en);
}

static int upm6918_is_powerpath_enabled(struct charger_device
		*chg_dev, bool *en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	u8 data;
	int ret = 0;

	ret = upm6918_read_byte(upm, UPM6918_REG_08, &data);
	if(ret < 0) {
		pr_err("reg read fail\n");
		return ret;
	}

	if ((data & REG08_CHRG_STAT_MASK) == 0)
		*en = true;
	else
		*en = false;

	pr_debug("%s: en %d\n", __func__, *en);

	return ret;
}

static int upm6918_enable_otg_config(struct charger_device *chg_dev, bool en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	int ret = 0;

	pr_debug("en:%d\n", en);
	if (en) {
		ret = upm6918_enable_otg(upm);
		ret += upm6918_disable_watchdog_timer(upm);
	} else {
		ret = upm6918_disable_otg(upm);
		ret += upm6918_set_watchdog_timer(upm, REG05_WDT_40S);
	}

	return ret;
}

static int upm6918_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{	
	int ret = 0;
	struct upm6918 *upm = charger_get_data(chg_dev);
	unsigned int avg = (BOOSTI_500 + BOOSTI_1200) / 2;

	uA = uA / 1000;
	pr_debug("%s: uA:%d, avg:%d\n", __func__, uA, avg);

	if (uA >= avg)
		ret = upm6918_set_boost_current(upm, BOOSTI_1200);
	else
		ret = upm6918_set_boost_current(upm, BOOSTI_500);

	return ret;
}

static int upm6918_do_event(struct charger_device *chg_dev, u32 event,
			    u32 args)
{
	if (chg_dev == NULL)
		return -EINVAL;

	pr_debug("%s: event = %d\n", __func__, event);
#if 0
	switch (event) {
	case EVENT_EOC:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
		break;
	case EVENT_RECHARGE:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_RECHG);
		break;
	default:
		break;
	}
#endif
	return 0;
}

/***********************************************************************
 *
 *  Notify Charger Driver to update chg type
 *
 ***********************************************************************/
static int upm6918_hvdcp_type_changed(struct upm6918 *upm)
{
	int ret = 0;

	union power_supply_propval propval;

	/* Get chg type det power supply */
	if (!upm->charger) {
		upm->charger = power_supply_get_by_name("charger");
		if (!upm->charger) {
			pr_err("%s: charger psy get fail\n", __func__);
			return -EINVAL;
		}
	}

	propval.intval = upm->chg_type;
	ret = power_supply_set_property(upm->charger,
					POWER_SUPPLY_PROP_TYPE,
					&propval);
	if (ret < 0)
		pr_err("%s: psy type failed, ret = %d\n", __func__, ret);
	else
		pr_info("%s: chg_type = %d\n", __func__, upm->chg_type);

	return ret;
}

static int upm6918_psy_online_changed(struct upm6918 *upm)
{
	int ret = 0;
	union power_supply_propval propval;

	/* Get chg type det power supply */
	if (!upm->charger) {
		upm->charger = power_supply_get_by_name("charger");
		if (!upm->charger) {
			pr_err("%s: charger psy get fail\n", __func__);
 			return -EINVAL;
		}
	}

	propval.intval = upm->attach;
	ret = power_supply_set_property(upm->charger, 
			POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0)
		pr_err("%s: psy online fail(%d)\n", __func__, ret);
	else
		pr_debug("%s: pwr_rdy = %d\n", __func__, upm->attach);

	return ret;
}

static int upm6918_chg_type_changed(struct upm6918 *upm)
{
	int ret = 0;

	union power_supply_propval propval;

	/* Get chg type det power supply */
	if (!upm->charger) {
		upm->charger = power_supply_get_by_name("charger");
		if (!upm->charger) {
			pr_err("%s: charger psy get fail\n", __func__);
 			return -EINVAL;
		}
	}

	propval.intval = upm->chg_type;
	ret = power_supply_set_property(upm->charger,
					POWER_SUPPLY_PROP_CHARGE_TYPE,
					&propval);
	if (ret < 0)
		pr_err("%s: psy type failed, ret = %d\n", __func__, ret);
	else
		pr_info("%s: chg_type = %d\n", __func__, upm->chg_type);

	return ret;
}

/***********************************************************************
 *
 *  notify this mode to ignore/enable BC1.2/QC
 *
 ***********************************************************************/
static int upm6918_ignore_chg_type_det(struct charger_device *chg_dev, bool en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("%s: en %d\n", __func__, en);
	if(!upm){
		pr_info("%s: upm is null.\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&upm->detect_lock);
	upm->ignore_detect = en;
	atomic_set(&upm->chg_type_check_cnt, 1);
	wake_up(&upm->waitq);
	mutex_unlock(&upm->detect_lock);

	return ret;
}

static int upm6918_enable_chg_type_det(struct charger_device *chg_dev, bool en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("%s: en %d\n", __func__, en);
	if(!upm){
		pr_info("%s: upm is null.\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&upm->detect_lock);
	if (upm->tcpc_attach == en) {
		pr_info("attach(%d) is the same\n", upm->tcpc_attach);
		goto out;
	}

	upm->tcpc_attach = en;
	upm->detect_stage = en ? 
		DETECT_TYPE_CONNECT : DETECT_TYPE_DISCONNECT;
	atomic_set(&upm->chg_type_check_cnt, 1);
	atomic_set(&upm->vbus_check_cnt, 6); /* for vbus check */
	atomic_set(&upm->input_check_cnt, 6); /* for input check */
	wake_up(&upm->waitq);

out:
	mutex_unlock(&upm->detect_lock);
	return ret;
}

static int upm6918_enable_hvdcp_type_detect(struct charger_device *chg_dev, bool en)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	int ret = 0;

	mutex_lock(&upm->detect_lock);
	if (upm->enable_hvdcp_check == en) {
		pr_info("%s hvdcp attach(%d) is the same\n", upm->enable_hvdcp_check);
		goto out;
	}

	upm->enable_hvdcp_check = en;
	upm->detect_stage = en ? 
		DETECT_TYPE_HVDCP : DETECT_TYPE_DISCONNECT;
	upm->hvdcp_detect_retry_count = 0;
	atomic_set(&upm->chg_type_check_cnt, 1);
	wake_up(&upm->waitq);

out:
	mutex_unlock(&upm->detect_lock);

	return ret;
}

/***********************************************************************
 *
 *  HVDCP functions
 *
 ***********************************************************************/
static int upm6918_get_vbus_voltage(struct upm6918 *upm, int *mV)
{
	int value;
	int ret = 0;

	if (!upm->vbus_iio_channel) {
		upm->vbus_iio_channel = devm_iio_channel_get(upm->dev, "vbus_adc");
		if (IS_ERR(upm->vbus_iio_channel)) {
			upm->vbus_iio_channel = NULL;
			pr_err("%s: get vbus iio fail\n", __func__);
			return -ENODEV;
		}
	}

	ret = iio_read_channel_processed(upm->vbus_iio_channel, &value);
	if (ret < 0) {
		pr_err("%s: read vbus iio channel fail\n", __func__);
		return ret;
	}

	*mV = (((R_CHARGER_1 + R_CHARGER_2) * value) / R_CHARGER_2) / ADAPTER_MV_TO_UV;
	pr_info("%s: Vresis=%d, vbus=%dmV\n", __func__, value, *mV);

	return ret;
}

static int upm6918_get_vbus(struct charger_device *chg_dev, u32 *vbus)
{
	struct upm6918 *upm = charger_get_data(chg_dev);

	return upm6918_get_vbus_voltage(upm, vbus);
}

static bool upm6918_vbus_check(struct upm6918 *upm)
{
	u8 temp = 0;
	bool vbus_gd = false;
	int ret = 0;

	ret = upm6918_read_byte(upm, UPM6918_REG_0A, &temp);
	if(ret < 0) {
		pr_err("%s: REG[0x0A] read fail\n", __func__);
		return false;
	}

	vbus_gd = !!(temp & REG0A_VBUS_GD_MASK);
	if (!vbus_gd)
		pr_info("%s: reg0A:0x%x, vbus_gd:%d\n", __func__, temp, vbus_gd);

	return vbus_gd;
}

static int upm6918_enable_hvdcp_voltage(struct charger_device *chg_dev, u32 uV)
{
	struct upm6918 *upm = charger_get_data(chg_dev);
	int ret = 0;
	int voltage;
	int dp_val, dm_val, dpdm;
	const int dpdm_mask = (REG0C_DP_MUX_MASK | REG0C_DM_MUX_MASK);

	voltage = uV / ADAPTER_V_TO_UV;
	switch (voltage) {
	case ADAPTER_DPDM_HIZ:
		dp_val = REG0C_DPDM_OUT_HIZ << REG0C_DP_MUX_SHIFT;
		dm_val = REG0C_DPDM_OUT_HIZ << REG0C_DM_MUX_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = upm6918_update_bits(upm, UPM6918_REG_0C, dpdm_mask, dpdm);
		if (ret) {
			pr_err("update dpdm vsel to hiz fail\n");
			return ret;
		}
		break;
	case ADAPTER_5V:
		/* dp 0.6v and dm 0v out 5V */
		dp_val = REG0C_DPDM_OUT_0P6V << REG0C_DP_MUX_SHIFT;
		dm_val = REG0C_DPDM_OUT_0P << REG0C_DM_MUX_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = upm6918_update_bits(upm, UPM6918_REG_0C, dpdm_mask, dpdm);
		if (ret) {
			pr_err("update dpdm vsel to 5V fail\n");
			return ret;
		}
		break;
	case ADAPTER_9V:
		/* dp 3.3v and dm 0.6v out 9V */
		dp_val = REG0C_DPDM_OUT_3P3V << REG0C_DP_MUX_SHIFT;
		dm_val = REG0C_DPDM_OUT_0P6V << REG0C_DM_MUX_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = upm6918_update_bits(upm, UPM6918_REG_0C, dpdm_mask, dpdm);
		if (ret) {
			pr_err("update dpdm vsel to 9V fail\n");
			return ret;
		}
		break;
	case ADAPTER_12V:
		/* dp 0.6v and dm 0.6v out 12V */
		dp_val = REG0C_DPDM_OUT_0P6V << REG0C_DP_MUX_SHIFT;
		dm_val = REG0C_DPDM_OUT_0P6V << REG0C_DM_MUX_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = upm6918_update_bits(upm, UPM6918_REG_0C, dpdm_mask, dpdm);
		if (ret) {
			pr_err("update dpdm vsel to 12V fail\n");
			return ret;
		}
		break;
	default:
		pr_err("unsupport voltage:%dV\n", voltage);
		return -EINVAL;
	}

	return ret;
}

static int upm6918_hvdcp_precheck(struct upm6918 *upm)
{
	int ret = 0;
	int val;
	
	pr_info("++\n");
	/*enable hvdcp*/
	ret = upm6918_enable_hvdcp(upm, 1);
	/*dp 0.6 dm hiz*/
	val = REG0C_DPDM_OUT_0P6V;
	ret = upm6918_update_bits(upm, UPM6918_REG_0C, REG0C_DP_MUX_MASK,
				   val << REG0C_DP_MUX_SHIFT);
	val = REG0C_DPDM_OUT_HIZ;
	ret = upm6918_update_bits(upm, UPM6918_REG_0C, REG0C_DM_MUX_MASK,
				   val << REG0C_DM_MUX_SHIFT);

	return ret;
}

static int upm6918_hvdcp_postcheck(struct upm6918 *upm)
{
	int ret = 0;
	int dp_val, dm_val, dpdm;
	const int dpdm_mask = (REG0C_DP_MUX_MASK | REG0C_DM_MUX_MASK);
	int vbus;

	pr_info("++\n");

	upm6918_disable_charger(upm);
	/* dp 3.3v and dm 0.6v out 9V */
	dp_val = REG0C_DPDM_OUT_3P3V << REG0C_DP_MUX_SHIFT;
	dm_val = REG0C_DPDM_OUT_0P6V << REG0C_DM_MUX_SHIFT;
	dpdm = (dp_val | dm_val);
	ret = upm6918_update_bits(upm, UPM6918_REG_0C, dpdm_mask, dpdm);
    if (ret < 0) {
		pr_err("dpdm vsel set 9V fail\n");
		ret = -1;
		goto out;
    }

	msleep(500);
	ret = upm6918_get_vbus_voltage(upm, &vbus);
	if (ret < 0) {
		pr_err("get vbus voltage fail\n");
		ret = -2;
		goto out;
	}

	/* restore vbus voltage to 5V */
	/* dp 0.6v and dm 0v out 5V */
	dp_val = REG0C_DPDM_OUT_0P6V << REG0C_DP_MUX_SHIFT;
	dm_val = REG0C_DPDM_OUT_0P << REG0C_DM_MUX_SHIFT;
	dpdm = (dp_val | dm_val);
	ret = upm6918_update_bits(upm, UPM6918_REG_0C, dpdm_mask, dpdm);
	if (ret < 0)
		pr_err("dpdm vsel set 5V fail\n");

	msleep(100);

	if (vbus >= HVDCP_HV_THRESHOLD_MV_MIN) {
		upm->chg_type = HVDCP_QC20_CHARGER;
		ret = 0;
		goto out;
	}

	ret = 1;

out:
	return ret;
}

static int upm6918_hvdcp_disconnect(struct upm6918 *upm)
{
	int ret = 0;

	pr_info("++ \n");

	/* dp and dm to Hiz*/
    ret = upm6918_update_bits(upm, UPM6918_REG_0C,
				  REG0C_DP_MUX_MASK | REG0C_DM_MUX_MASK, 0);
    if (ret) {
		pr_err("%s: update dpdm to hiz fail\n", __func__);
        return ret;
    }

	/* reset hvdcp check */
	upm->enable_hvdcp_check = false;

	return ret;
}

/***********************************************************************
 *
 *  before and after BC1.2 detection
 *
 ***********************************************************************/
static int upm6918_get_state(struct upm6918 *upm, struct upm6918_state *state)
{
	u8 chrg_stat;
	u8 fault;
	u8 chrg_param_0, chrg_param_1, chrg_param_2;
	int ret;

	ret = upm6918_read_byte(upm, UPM6918_REG_08, &chrg_stat);
	if (ret){
		ret = upm6918_read_byte(upm, UPM6918_REG_08, &chrg_stat);
		if (ret){
			pr_err("read SGM4154X_CHRG_STAT fail\n");
			return ret;
		}
	}
	state->chrg_type = chrg_stat & REG08_VBUS_STAT_MASK;
	state->chrg_stat = chrg_stat & REG08_CHRG_STAT_MASK;
	state->online = !!(chrg_stat & REG08_PG_STAT_MASK);
	state->therm_stat = !!(chrg_stat & REG08_THERM_STAT_MASK);
	state->vsys_stat = !!(chrg_stat & REG08_VSYS_STAT_MASK);
	pr_err("chrg_type =%d, chrg_stat =%d, online = %d\n",
		state->chrg_type, state->chrg_stat, state->online);

	ret = upm6918_read_byte(upm, UPM6918_REG_09, &fault);
	if (ret){
		pr_err("read SGM4154X_CHRG_FAULT fail\n");
		return ret;
	}
	state->chrg_fault = fault;	
	state->ntc_fault = fault & REG09_FAULT_NTC_MASK;
	state->health = state->ntc_fault;

	ret = upm6918_read_byte(upm, UPM6918_REG_00, &chrg_param_0);
	if (ret){
		pr_err("read SGM4154X_CHRG_CTRL_0 fail\n");
		return ret;
	}
	state->hiz_en = !!(chrg_param_0 & REG00_ENHIZ_MASK);

	ret = upm6918_read_byte(upm, UPM6918_REG_05, &chrg_param_1);
	if (ret){
		pr_err("read SGM4154X_CHRG_CTRL_5 fail\n");
		return ret;
	}
	state->term_en = !!(chrg_param_1 & REG05_EN_TERM_MASK);

	ret = upm6918_read_byte(upm, UPM6918_REG_0A, &chrg_param_2);
	if (ret){
		pr_err("read SGM4154X_CHRG_CTRL_A fail\n");
		return ret;
	}
	state->vbus_gd = !!(chrg_param_2 & REG0A_VBUS_GD_MASK);

	return 0;
}

static int upm6918_bc12_preprocess(struct upm6918 *upm)
{
	int ret = 0;

	pr_info("++\n");

	if (upm6918_force_dpdm(upm)) {
		pr_err("enable charge type detect fail.\n");
		goto error;
	}

error:
	pr_info("--\n");
	return ret;
}

static int upm6918_bc12_postprocess(struct upm6918 *upm)
{

	struct upm6918_state state;
	bool attach = false, inform_psy = true;
	int ret;

	pr_info("++ enter\n");

	if(!upm) {
		pr_err("Cann't get upm6918_device\n");
		return -ENODEV;
	}

	attach = upm->tcpc_attach;
	if (upm->attach == attach) {
		pr_info("attach(%d) is the same\n", attach);
		inform_psy = !attach;
	}
	upm->attach = attach;
	pr_info("attach = %d\n", attach);

	// if (!upm->charger_wakelock->active)
	//	__pm_stay_awake(upm->charger_wakelock);

	/* Plug out during BC12 */
	if (!attach) {
		upm->chg_type = CHARGER_UNKNOWN;
		goto out;
	}

	/* Plug in */
	ret = upm6918_get_state(upm, &state);
	upm->state = state;	
	
	if(!upm->state.vbus_gd) {
		pr_err("Vbus not present, disable charge\n");
		upm6918_disable_charger(upm);
		upm->chg_type = CHARGER_UNKNOWN;
		goto out;
	}
	if(!state.online)
	{
		pr_err("Vbus not online\n");
		upm->chg_type = CHARGER_UNKNOWN;
		goto out;
	}	

	switch(upm->state.chrg_type) {
		case REG08_VBUS_TYPE_SDP:
			pr_info("upm6918 charger type: SDP\n");
			upm->chg_type = STANDARD_HOST;
			break;

		case REG08_VBUS_TYPE_CDP:
			pr_info("upm6918 charger type: CDP\n");
			upm->chg_type = CHARGING_HOST;
			break;

		case REG08_VBUS_TYPE_DCP:
			pr_info("upm6918 charger type: DCP\n");
			upm->chg_type = STANDARD_CHARGER;
			break;

		case REG08_VBUS_TYPE_UNKNOWN:
			pr_info("upm6918 charger type: UNKNOWN\n");
			upm->chg_type = NONSTANDARD_CHARGER;
			break;	

		case REG08_VBUS_TYPE_NON_STD:
			pr_info("upm6918 charger type: NON_STANDARD\n");
			upm->chg_type = NONSTANDARD_CHARGER;
			break;

		default:
			pr_info("upm6918 charger type: default\n");
			upm->chg_type = CHARGER_UNKNOWN;
			break;
	}

out:
	//release wakelock
	upm6918_dump_register(upm->chg_dev);

	upm6918_psy_online_changed(upm);
	if (!upm->ignore_detect)
		upm6918_chg_type_changed(upm);
	// pr_info("Relax wakelock\n");
	// __pm_relax(upm->charger_wakelock);
	return 0;
}

/***********************************************************************
 *
 *  Charger detect work
 *
 ***********************************************************************/
static int upm6918_detect_task_threadfn(void *data)
{
	struct upm6918 *upm = data;
	bool attach = false;
	bool done = false;
	bool is_hvdcp_check = false;
	ktime_t now, check_time;
	bool need_wait = false;
	enum detect_stage_type stage;
	u8 reg;
	int ret;

	pr_info("enter\n");
	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(upm->waitq,
					     atomic_read(&upm->chg_type_check_cnt) > 0);
		if (ret < 0) {
			pr_info("wait event been interrupted(%d)\n", ret);
			continue;
		}

		mutex_lock(&upm->detect_lock);

		need_wait = false;
		attach = upm->tcpc_attach;
		stage = upm->detect_stage;

		/* Power Delivery Charger, ignore BC12 */
		if (attach && upm->ignore_detect) {
			pr_info("ignore charger detect\n");

			if (upm->detect_stage == DETECT_TYPE_HVDCP ||
				upm->detect_stage == DETECT_TYPE_HVDCP_CHECK)
					is_hvdcp_check = true;

			upm->detect_stage = DETECT_TYPE_CHECK_DONE;
			atomic_set(&upm->chg_type_check_cnt, 0);
			upm6918_hvdcp_disconnect(upm);
			if (is_hvdcp_check)
				upm6918_hvdcp_type_changed(upm);

			goto next_loop;
		}

		switch (stage) {
			case DETECT_TYPE_CONNECT:
				pr_info("enter DETECT_TYPE_CONNECT\n");
				if(!attach) {
					pr_err("usb disconnect, goto next check\n", attach);
					goto next_loop;
				}

				if(!upm6918_vbus_check(upm)) {
					pr_info("vbus check count:%d\n", atomic_read(&upm->vbus_check_cnt));
					atomic_dec(&upm->vbus_check_cnt);
					if (atomic_read(&upm->vbus_check_cnt) > 0) {
						need_wait = true;
						goto next_loop;
					}
				}

				ret = upm6918_hvdcp_disconnect(upm);
				ret += upm6918_bc12_preprocess(upm);
				if(ret < 0) {
					pr_err("enable dpdm detect fail\n", attach);
					upm->detect_stage = DETECT_TYPE_CHECK_DONE;
					goto next_loop;
				}

				atomic_set(&upm->chg_type_check_cnt,
					BC12_CHECK_DEFAULT_DELAY_COUNT);
				upm->detect_stage = DETECT_TYPE_CHECK;
				need_wait = true;
				break;

			case DETECT_TYPE_CHECK:
				pr_info("enter DETECT_TYPE_CHECK\n");
				if(!attach) {
					pr_err("usb disconnect, goto next check\n", attach);
					atomic_set(&upm->chg_type_check_cnt, 1);
					need_wait = false;
					upm->detect_stage = DETECT_TYPE_DISCONNECT;
					goto next_loop;
				}

				ret = upm6918_read_byte(upm, UPM6918_REG_08, &reg);
				if (ret) {
					pr_err("read UPM6918_REG_08 fail ret=%d\n", ret);
					atomic_dec(&upm->input_check_cnt);
					if (atomic_read(&upm->input_check_cnt) > 0) {
						pr_info("input check count ++\n");
						need_wait = true;
					} else {
						pr_info("Retry read UPM6918_REG_08 failed!\n");
						atomic_set(&upm->chg_type_check_cnt, 1);
						need_wait = false;
						upm->detect_stage = DETECT_TYPE_DISCONNECT;
					}
					goto next_loop;
				}

				done = (reg & REG08_PG_STAT_MASK) >> REG08_PG_STAT_SHIFT;
				pr_info("DET_DONE reg:0x%x done=%d attach:%d\n", reg, done, attach);
				if (done) {
					upm6918_bc12_postprocess(upm);
					upm->detect_stage = DETECT_TYPE_CHECK_DONE;
				} else if (atomic_read(&upm->chg_type_check_cnt) > 0) {
					pr_info("chg_type_check_cnt:%d\n",
							atomic_read(&upm->chg_type_check_cnt));
					atomic_dec(&upm->chg_type_check_cnt);
					upm->detect_stage = DETECT_TYPE_CHECK;
					need_wait = true;
				} else {
					pr_err("did not detected the result\n");
					atomic_set(&upm->chg_type_check_cnt, 1);
					upm->detect_stage = DETECT_TYPE_DISCONNECT;
				}
				break;

			case DETECT_TYPE_DISCONNECT:
				pr_info("enter DETECT_TYPE_DISCONNECT\n");
				atomic_set(&upm->chg_type_check_cnt, 0);
				upm6918_hvdcp_disconnect(upm);
				upm6918_bc12_postprocess(upm);
				break;

			case DETECT_TYPE_HVDCP:
				pr_info("enter detect HVDCP\n");
				upm6918_hvdcp_precheck(upm);
				atomic_set(&upm->chg_type_check_cnt,
					HVDCP_CHECK_DEFAULT_DELAY_COUNT);
				upm->hvdcp_check_begin_time = ktime_get_boottime();
				upm->detect_stage = DETECT_TYPE_HVDCP_CHECK;
				break;

			case DETECT_TYPE_HVDCP_CHECK:
				pr_info("enter detect HVDCP check\n");
				if(!attach) {
					pr_err("usb disconnect, goto next check\n", attach);
					atomic_set(&upm->chg_type_check_cnt, 1);
					need_wait = false;
					upm->detect_stage = DETECT_TYPE_DISCONNECT;
					goto next_loop;
				}
				now = ktime_get_boottime();
				check_time = ktime_sub(now, upm->hvdcp_check_begin_time);
				if(ktime_to_ms(check_time) > HVDCP_DP_0P6V_HOLD_TIME_MS) {
					atomic_set(&upm->chg_type_check_cnt, 0);
					ret = upm6918_hvdcp_postcheck(upm);
					/*
					 * ret = 0: detect QC2.0 success
					 * ret = 1: vbus check fail, need retry
					 * ret < 0: i2c or iio fail
					 */
					if (ret == 0) {
						pr_info("hvdcp detect success\n");
						upm6918_hvdcp_type_changed(upm);
						upm->detect_stage = DETECT_TYPE_CHECK_DONE;
					} else if (ret == 1) {
						pr_info("hvdcp vbus check fail, retry:%d\n",
								upm->hvdcp_detect_retry_count);
						if (upm->hvdcp_detect_retry_count < HVDCP_HV_DETECT_RETRY) {
							upm6918_hvdcp_disconnect(upm);
							need_wait = true;
							atomic_set(&upm->chg_type_check_cnt, 1);
							upm->detect_stage = DETECT_TYPE_HVDCP;
							upm->hvdcp_detect_retry_count++;
						} else {
							upm6918_hvdcp_type_changed(upm);
						}
					} else if (ret < 0) {
						pr_err("hvdcp postcheck i2c or iio fail\n");
						upm6918_hvdcp_type_changed(upm);
						break;
					}
				} else {
					atomic_dec(&upm->chg_type_check_cnt);
					need_wait = true;
				}
				break;

			case DETECT_TYPE_CHECK_DONE:
			default:
				pr_info("detect stage(%d).\n", upm->detect_stage);
				atomic_set(&upm->chg_type_check_cnt, 0);
				break;
		}

next_loop:
		mutex_unlock(&upm->detect_lock);
		if(need_wait)
			msleep(BC12_CHECK_DEFAULT_DELAY_TIME_MS);
	}

	pr_info("exit\n");
	return 0;
}

static struct charger_ops upm6918_chg_ops = {
	.enable_hz = upm6918_enable_hz,

	/* Normal charging */
	.dump_registers = upm6918_dump_register,
	.enable = upm6918_charging_switch,
	.get_charging_current = upm6918_get_charging_current,
	.set_charging_current = upm6918_set_charging_current,
	.get_input_current = upm6918_get_input_current,
	.set_input_current = upm6918_set_input_current,
	.get_constant_voltage = upm6918_get_constant_voltage,
	.set_constant_voltage = upm6918_set_constant_voltage,
	.kick_wdt = upm6918_reset_watch_dog_timer,
	.set_mivr = upm6918_set_input_volt_lim,
	.get_mivr = upm6918_get_input_volt_lim,
	.is_charging_done = upm6918_get_charging_status,
	//.plug_out = upm6918_plug_out,
	.is_enabled = upm6918_is_enabled,

	/* Safety timer */
	.enable_safety_timer = upm6918_enable_safetytimer,
	.is_safety_timer_enabled = upm6918_get_is_safetytimer_enable,

	/* Power path */
	.enable_powerpath = upm6918_enable_power_path,
	.is_powerpath_enabled = upm6918_is_powerpath_enabled,

	/* OTG */
	.enable_otg = upm6918_enable_otg_config,	
	.set_boost_current_limit = upm6918_set_boost_current_limit,
	.event = upm6918_do_event,

	/* charger type detection */
	.enable_chg_type_det = upm6918_enable_chg_type_det,
	.enable_hvdcp_det = upm6918_enable_hvdcp_type_detect,
	.ignore_chg_type_det = upm6918_ignore_chg_type_det,

	.enable_hvdcp_voltage = upm6918_enable_hvdcp_voltage,
	.get_vbus_adc = upm6918_get_vbus,
};

static void charger_change_work(struct work_struct *work) {
	struct delayed_work *delay_work = NULL;
	struct upm6918 *upm = NULL;
	u8 reg_fault;
	u8 otg_sts;
	u8 vbus_sts;
	int vbus = 0;
	bool en_otg = false;
	bool prev_vbus_gd = false;

	delay_work = container_of(work, struct delayed_work, work);
	if(delay_work == NULL) {
		pr_err("Cann't get charger_change_work\n");
		return;
	}
	upm = container_of(delay_work, struct upm6918, charger_change_work);
	if(upm == NULL) {
		pr_err("Cann't get upm\n");
		return;
	}

	upm6918_read_byte(upm, UPM6918_REG_09, &reg_fault);
	upm6918_read_byte(upm, UPM6918_REG_01, &otg_sts);
	en_otg = !!(otg_sts & REG01_OTG_CONFIG_MASK);
	pr_info("%s: fault:0x%x, en otg:%d, hvdcp check:%d\n",
		__func__, reg_fault, en_otg, upm->detect_stage);

	if (en_otg) {
		pr_err("OTG mode, skip adapter/usb check\n");
		if (upm->charger_wakelock)
			__pm_relax(upm->charger_wakelock);
		return;
	}

	/* VBUS Insert check */
	upm6918_read_byte(upm, UPM6918_REG_0A, &vbus_sts);
	prev_vbus_gd = upm->vbus_gd;
	upm->vbus_gd = !!(vbus_sts & REG0A_VBUS_GD_MASK);
	upm6918_get_vbus_voltage(upm, &vbus);
	if (!prev_vbus_gd && upm->vbus_gd && (vbus >= 4400)) {
		pr_err("adapter/usb inserted\n");
		upm6918_notify_vbus_changed(1);
	} else if (prev_vbus_gd && !upm->vbus_gd && (vbus < 4400)) {
		pr_err("adapter/usb removed\n");
		upm6918_notify_vbus_changed(0);
	}

	if (upm->dev->parent)
		pm_runtime_put(upm->dev->parent);
	__pm_relax(upm->charger_wakelock);
}

static irqreturn_t upm6918_irq_handler(int irq, void *data)
{
	struct upm6918 *upm = (struct upm6918 *)data;

	pr_info("%s entry\n",__func__);
	__pm_stay_awake(upm->charger_wakelock);
	/* present the i2c bus device runtime suspend */
	if (upm->dev->parent)
		pm_runtime_get(upm->dev->parent);
	schedule_delayed_work(&upm->charger_change_work, msecs_to_jiffies(50));

	return IRQ_HANDLED;
}

static int upm6918_register_interrupt(struct upm6918 *upm)
{
	int ret = 0;
	ret = devm_gpio_request_one(upm->dev, upm->intr_gpio, GPIOF_DIR_IN,
			devm_kasprintf(upm->dev, GFP_KERNEL,
			"upm6918_intr_gpio.%s", dev_name(upm->dev)));
	if (ret < 0) {
		pr_err("%s gpio request fail(%d)\n",
				      __func__, ret);
		return ret;
	}

	gpio_direction_input(upm->intr_gpio);
	upm->client->irq = gpio_to_irq(upm->intr_gpio);
	if (upm->client->irq < 0) {
		pr_err("%s gpio2irq fail(%d)\n",
				      __func__, upm->client->irq);
		return upm->client->irq;
	}
	pr_err("%s irq = %d\n", __func__, upm->client->irq);

	ret = devm_request_threaded_irq(upm->dev, upm->client->irq, NULL,
					upm6918_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"upm_irq", upm);
	if (ret < 0) {
		pr_err("request thread irq failed:%d\n", ret);
		return ret;
	}

	enable_irq_wake(upm->client->irq);

	return 0;
}

/****************************************************************************/
int upm6918_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct upm6918 *upm;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret = 0;

	upm = devm_kzalloc(&client->dev, sizeof(struct upm6918), GFP_KERNEL);
	if (!upm)
		return -ENOMEM;

	client->addr = 0x6B;
	upm->dev = &client->dev;
	upm->client = client;

	i2c_set_clientdata(client, upm);

	mutex_init(&upm->i2c_rw_lock);
	mutex_init(&upm->regulator_lock);
	mutex_init(&upm->detect_lock);
	atomic_set(&upm->chg_type_check_cnt, 0);
	atomic_set(&upm->vbus_check_cnt, 0);
	atomic_set(&upm->input_check_cnt, 0);
	init_waitqueue_head(&upm->waitq);
	INIT_DELAYED_WORK(&upm->charger_change_work, charger_change_work);
	upm->tcpc_attach = false;
	upm->attach = false;
	upm->chg_type = CHARGER_UNKNOWN;
	upm->detect_stage = DETECT_TYPE_UNKNOW;

	ret = upm6918_detect_device(upm);
	if (ret) {
		pr_err("No upm6918 device found, retry!\n");
		ret = upm6918_detect_device(upm);
		if (ret) {
			pr_err("No upm6918 device found, stop!\n");
			return -ENODEV;
		}
	}

	match = of_match_node(upm6918_charger_match_table, node);
	if (match == NULL) {
		pr_err("device tree match not found\n");
		return -EINVAL;
	}

	if (upm->part_no != *(int *)match->data) {
		pr_err("part no mismatch, hw:%s, devicetree:%s\n",
			pn_str[upm->part_no], pn_str[*(int *) match->data]);
        mutex_destroy(&upm->i2c_rw_lock);
		mutex_destroy(&upm->regulator_lock);
        return -EINVAL;
	}

	upm->platform_data = upm6918_parse_dt(node, upm);
	if (!upm->platform_data) {
		pr_err("No platform data provided.\n");
		return -EINVAL;
	}

	ret = upm6918_init_device(upm);
	if (ret) {
		pr_err("Failed to init device\n");
		return ret;
	}

	upm->charger_wakelock =	wakeup_source_register(NULL, "upm6918 suspend wakelock");
	upm->detect_task = kthread_run(upm6918_detect_task_threadfn,
			upm, "upm6918_thread");
	ret = PTR_ERR_OR_ZERO(upm->detect_task);
	if (ret < 0){
		pr_err("create detect thread fail.\n");
		goto fail_free_gpio;
	}

	ret = sysfs_create_group(&upm->dev->kobj, &upm6918_attr_group);
	if (ret)
		pr_err("failed to register sysfs. err: %d\n", ret);

	/* Register charger device */
	upm->chg_dev = charger_device_register(upm->chg_dev_name,
						&client->dev, upm,
						&upm6918_chg_ops,
						&upm6918_chg_props);
	if (IS_ERR_OR_NULL(upm->chg_dev)) {
		pr_err("register charger device failed\n");
		ret = PTR_ERR(upm->chg_dev);
		goto fail_free_gpio;
	}

	upm6918_register_interrupt(upm);
	/* init check for vbus */
	upm6918_irq_handler(-1, upm);
	pr_err("upm6918 probe successfully!\n");

	return 0;

fail_free_gpio:
	if (upm->intr_gpio)
		gpio_free(upm->intr_gpio);
	pr_err("probe fail\n");

	return ret;
}
EXPORT_SYMBOL_GPL(upm6918_charger_probe);

int upm6918_charger_remove(struct i2c_client *client)
{
	struct upm6918 *upm = i2c_get_clientdata(client);

	mutex_destroy(&upm->i2c_rw_lock);
	mutex_destroy(&upm->detect_lock);
	wakeup_source_unregister(upm->charger_wakelock);
	cancel_delayed_work_sync(&upm->charger_change_work);
	sysfs_remove_group(&upm->dev->kobj, &upm6918_attr_group);

	return 0;
}
EXPORT_SYMBOL_GPL(upm6918_charger_remove);

void upm6918_charger_shutdown(struct i2c_client *client)
{
	struct upm6918 *upm = i2c_get_clientdata(client);
	int ret = 0;

	pr_err("upm6918_charger_shutdown\n");
	if (client->irq)
		disable_irq(client->irq);
	cancel_delayed_work_sync(&upm->charger_change_work);

	ret = upm6918_update_bits(upm, UPM6918_REG_01, REG01_OTG_CONFIG_MASK,
			(REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT));
	if (ret)
		pr_err("reset otg fail\n");

	ret = upm6918_update_bits(upm, UPM6918_REG_0C,
			REG0C_DP_MUX_MASK | REG0C_DM_MUX_MASK, 0);
	if (ret)
		pr_err("update dpdm to hiz fail\n");

	ret = upm6918_reset_watchdog_timer(upm);
	if (ret)
		pr_err("Failed to disable charger, ret = %d\n", ret);
}
EXPORT_SYMBOL_GPL(upm6918_charger_shutdown);
