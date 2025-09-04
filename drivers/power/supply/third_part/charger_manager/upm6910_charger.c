/*
 * UPM6910D battery charging driver
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

#define pr_fmt(fmt)	"[cne-charge][upm6910dk]:%s: " fmt, __func__

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
#include <linux/pinctrl/consumer.h>

#include "upm6910_reg.h"
#include "upm6910.h"
#include "charger_class.h"

enum {
	PN_UPM6910D,
};

enum upm6910_part_no {
	UPM6910D = 0x02,
};

static int pn_data[] = {
	[PN_UPM6910D] = 0x02,
};

static char *pn_str[] = {
	[PN_UPM6910D] = "upm6910d",
};

struct upm6910 {
	struct device *dev;
	struct i2c_client *client;
	enum upm6910_part_no part_no;
	const char *chg_dev_name;
	const char *eint_name;
	int irq;
	u32 chg_en_gpio;
	//u32 intr_gpio
	struct mutex i2c_rw_lock;
	const char		*pinctrl_state_name;
	struct pinctrl		*irq_pinctrl;
	struct upm6910_platform_data *platform_data;
	struct charger_device *chg_dev;
	struct	mutex regulator_lock;
	struct	regulator *dpdm_reg;
	bool	dpdm_enabled;
};

static int __upm6910_read_reg(struct upm6910 *upm, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(upm->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __upm6910_write_reg(struct upm6910 *upm, int reg, u8 val)
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

static int upm6910_read_byte(struct upm6910 *upm, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&upm->i2c_rw_lock);
	ret = __upm6910_read_reg(upm, reg, data);
	mutex_unlock(&upm->i2c_rw_lock);

	return ret;
}

static int upm6910_write_byte(struct upm6910 *upm, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&upm->i2c_rw_lock);
	ret = __upm6910_write_reg(upm, reg, data);
	mutex_unlock(&upm->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

static int upm6910_update_bits(struct upm6910 *upm, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&upm->i2c_rw_lock);
	ret = __upm6910_read_reg(upm, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __upm6910_write_reg(upm, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&upm->i2c_rw_lock);
	return ret;
}

int upm6910_enter_hiz_mode(struct upm6910 *upm)
{
	u8 val = REG00_HIZ_ENABLE << REG00_ENHIZ_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_00, REG00_ENHIZ_MASK, val);
}

int upm6910_exit_hiz_mode(struct upm6910 *upm)
{
	u8 val = REG00_HIZ_DISABLE << REG00_ENHIZ_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_00, REG00_ENHIZ_MASK, val);
}

static int upm6910_set_stat_ctrl(struct upm6910 *upm, int ctrl)
{
	u8 val;

	val = ctrl;

	return upm6910_update_bits(upm, UPM6910_REG_00, REG00_STAT_CTRL_MASK,
				   val << REG00_STAT_CTRL_SHIFT);
}

static int upm6910_get_input_current_limit(struct upm6910 *upm, int *ilim)
{
	u8 iin;
	int ret;
	int temp;

	ret = upm6910_read_byte(upm, UPM6910_REG_00, &iin);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6910_REG_00, iin);
	}

	temp = (iin & REG00_IINLIM_MASK) >> REG00_IINLIM_SHIFT;
	temp = temp * REG00_IINLIM_LSB + REG00_IINLIM_BASE;
	*ilim = temp * 1000;
	pr_err("%s: get input curr %d uA\n", __func__, *ilim);

	return ret;
}

static int upm6910_set_input_current_limit(struct upm6910 *upm, int curr)
{
	u8 val;

	if (curr < REG00_IINLIM_BASE)
		curr = REG00_IINLIM_BASE;

	val = (curr - REG00_IINLIM_BASE) / REG00_IINLIM_LSB;
	return upm6910_update_bits(upm, UPM6910_REG_00, REG00_IINLIM_MASK,
				   val << REG00_IINLIM_SHIFT);
}

int upm6910_reset_watchdog_timer(struct upm6910 *upm)
{
	u8 val = REG01_WDT_RESET << REG01_WDT_RESET_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_01, REG01_WDT_RESET_MASK,
				   val);
}

int upm6910_enable_otg(struct upm6910 *upm)
{
	u8 val = REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_01, REG01_OTG_CONFIG_MASK,
				   val);
}

int upm6910_disable_otg(struct upm6910 *upm)
{
	u8 val = REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_01, REG01_OTG_CONFIG_MASK,
				   val);
}

int upm6910_enable_charger(struct upm6910 *upm)
{
	int ret;
	u8 val = REG01_CHG_ENABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    upm6910_update_bits(upm, UPM6910_REG_01, REG01_CHG_CONFIG_MASK, val);

	return ret;
}

int upm6910_disable_charger(struct upm6910 *upm)
{
	int ret;
	u8 val = REG01_CHG_DISABLE << REG01_CHG_CONFIG_SHIFT;

	ret =
	    upm6910_update_bits(upm, UPM6910_REG_01, REG01_CHG_CONFIG_MASK, val);
	return ret;
}

int upm6910_set_boost_current(struct upm6910 *upm, int curr)
{
	u8 val;

	val = REG02_BOOST_LIM_0P5A;
	if (curr == BOOSTI_1200)
		val = REG02_BOOST_LIM_1P2A;

	return upm6910_update_bits(upm, UPM6910_REG_02, REG02_BOOST_LIM_MASK,
				   val << REG02_BOOST_LIM_SHIFT);
}

static int upm6910_get_chargecurrent(struct upm6910 *upm, int *curr)
{
	u8 ichg;
	int ret;
	int temp;

	ret = upm6910_read_byte(upm, UPM6910_REG_02, &ichg);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6910_REG_02, ichg);
	}

	temp = (ichg & REG02_ICHG_MASK) >> REG02_ICHG_SHIFT;
	temp = temp * REG02_ICHG_LSB + REG02_ICHG_BASE;
	*curr = temp * 1000;
	pr_err("%s: get charging curr %d mA\n", __func__, *curr);

	return ret;
}

static int upm6910_set_chargecurrent(struct upm6910 *upm, int curr)
{
	u8 ichg;

	if (curr < REG02_ICHG_BASE)
		curr = REG02_ICHG_BASE;

	ichg = (curr - REG02_ICHG_BASE) / REG02_ICHG_LSB;
	return upm6910_update_bits(upm, UPM6910_REG_02, REG02_ICHG_MASK,
				   ichg << REG02_ICHG_SHIFT);

}

int upm6910_set_prechg_current(struct upm6910 *upm, int curr)
{
	u8 iprechg;

	if (curr < REG03_IPRECHG_BASE)
		curr = REG03_IPRECHG_BASE;

	iprechg = (curr - REG03_IPRECHG_BASE) / REG03_IPRECHG_LSB;

	return upm6910_update_bits(upm, UPM6910_REG_03, REG03_IPRECHG_MASK,
				   iprechg << REG03_IPRECHG_SHIFT);
}

int upm6910_set_term_current(struct upm6910 *upm, int curr)
{
	u8 iterm;

	if (curr < REG03_ITERM_BASE)
		curr = REG03_ITERM_BASE;

	iterm = (curr - REG03_ITERM_BASE) / REG03_ITERM_LSB;

	return upm6910_update_bits(upm, UPM6910_REG_03, REG03_ITERM_MASK,
				   iterm << REG03_ITERM_SHIFT);
}

static int upm6910_get_chargevolt(struct upm6910 *upm, int *volt)
{
	u8 volt_reg;
	int ret;
	int temp;

	ret = upm6910_read_byte(upm, UPM6910_REG_04, &volt_reg);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6910_REG_04, volt_reg);
	}

	temp = (volt_reg & REG04_VREG_MASK) >> REG04_VREG_SHIFT;
	temp = temp * REG04_VREG_LSB + REG04_VREG_BASE;
	*volt = temp * 1000;
	pr_err("%s: get CV %d uV\n", __func__, *volt);

	return ret;
}

static int upm6910_set_chargevolt(struct upm6910 *upm, int volt)
{
	u8 val;

	if (volt < REG04_VREG_BASE)
		volt = REG04_VREG_BASE;

	val = (volt - REG04_VREG_BASE) / REG04_VREG_LSB;
	return upm6910_update_bits(upm, UPM6910_REG_04, REG04_VREG_MASK,
				   val << REG04_VREG_SHIFT);
}

/*
int upm6910_enable_term(struct upm6910 *upm, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = REG05_TERM_ENABLE << REG05_EN_TERM_SHIFT;
	else
		val = REG05_TERM_DISABLE << REG05_EN_TERM_SHIFT;

	ret = upm6910_update_bits(upm, UPM6910_REG_05, REG05_EN_TERM_MASK, val);

	return ret;
}*/

int upm6910_set_watchdog_timer(struct upm6910 *upm, u8 timeout)
{
	u8 temp;

	temp = (u8) (((timeout -
		       REG05_WDT_BASE) / REG05_WDT_LSB) << REG05_WDT_SHIFT);

	return upm6910_update_bits(upm, UPM6910_REG_05, REG05_WDT_MASK, temp);
}

int upm6910_disable_watchdog_timer(struct upm6910 *upm)
{
	u8 val = REG05_WDT_DISABLE << REG05_WDT_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_05, REG05_WDT_MASK, val);
}

int upm6910_enable_safety_timer(struct upm6910 *upm)
{
	const u8 val = REG05_CHG_TIMER_ENABLE << REG05_EN_TIMER_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_05, REG05_EN_TIMER_MASK,
				   val);
}

int upm6910_disable_safety_timer(struct upm6910 *upm)
{
	const u8 val = REG05_CHG_TIMER_DISABLE << REG05_EN_TIMER_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_05, REG05_EN_TIMER_MASK,
				   val);
}

int upm6910_set_acovp_threshold(struct upm6910 *upm, int volt)
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

	return upm6910_update_bits(upm, UPM6910_REG_06, REG06_OVP_MASK,
				   val << REG06_OVP_SHIFT);
}

int upm6910_set_boost_voltage(struct upm6910 *upm, int volt)
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

	return upm6910_update_bits(upm, UPM6910_REG_06, REG06_BOOSTV_MASK,
				   val << REG06_BOOSTV_SHIFT);
}

int upm6910_set_input_volt_limit(struct upm6910 *upm, int volt)
{
	u8 val;

	if (volt < REG06_VINDPM_BASE)
		volt = REG06_VINDPM_BASE;

	val = (volt - REG06_VINDPM_BASE) / REG06_VINDPM_LSB;
	return upm6910_update_bits(upm, UPM6910_REG_06, REG06_VINDPM_MASK,
				   val << REG06_VINDPM_SHIFT);
}

/*
static int upm6910_force_dpdm(struct upm6910 *upm)
{
	int ret = 0;
	const u8 val = REG07_FORCE_DPDM << REG07_FORCE_DPDM_SHIFT;

	ret = upm6910_update_bits(upm, UPM6910_REG_07,
			REG07_FORCE_DPDM_MASK, val);

	return ret;
}

staticint upm6910_enable_batfet(struct upm6910 *upm)
{
	const u8 val = REG07_BATFET_ON << REG07_BATFET_DIS_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}

int upm6910_disable_batfet(struct upm6910 *upm)
{
	const u8 val = REG07_BATFET_OFF << REG07_BATFET_DIS_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_07, REG07_BATFET_DIS_MASK,
				   val);
}

int upm6910_set_batfet_delay(struct upm6910 *upm, uint8_t delay)
{
	u8 val;

	if (delay == 0)
		val = REG07_BATFET_DLY_0S;
	else
		val = REG07_BATFET_DLY_10S;

	val <<= REG07_BATFET_DLY_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_07, REG07_BATFET_DLY_MASK,
				   val);
}

int upm6910_batfet_rst_en(struct upm6910 *upm, bool en)
{
	u8 val = 0;

	if (en) {
		val = REG07_BATFET_RST_ENABLE << REG07_BATFET_RST_EN_SHIFT;
	} else {
		val = REG07_BATFET_RST_DISABLE << REG07_BATFET_RST_EN_SHIFT;
	}

	return upm6910_update_bits(upm, UPM6910_REG_07, REG07_BATFET_RST_EN_MASK,
				   val);
}

static int upm6910_request_dpdm(struct upm6910 *upm, bool enable)
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

static int upm6910_get_charger_type(struct upm6910 *upm, int *type)
{
	int ret;
	u8 reg_val = 0;
	int vbus_stat = 0;
	int chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	ret = upm6910_read_byte(upm, UPM6910_REG_08, &reg_val);
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
		upm6910_request_dpdm(upm, false);
	} else {

	}

	*type = chg_type;

	return 0;
}

static int upm6910_reset_chip(struct upm6910 *upm)
{
	int ret;
	u8 val = REG0B_REG_RESET << REG0B_REG_RESET_SHIFT;

	ret =
	    upm6910_update_bits(upm, UPM6910_REG_0B, REG0B_REG_RESET_MASK, val);
	return ret;
}
*/

static int upm6910_set_int_mask(struct upm6910 *upm, int mask)
{
	u8 val;

	val = mask;

	return upm6910_update_bits(upm, UPM6910_REG_0A, REG0A_INT_MASK_MASK,
				   val << REG0A_INT_MASK_SHIFT);
}

static int upm6910_detect_device(struct upm6910 *upm)
{
	int ret;
	u8 data;

	ret = upm6910_read_byte(upm, UPM6910_REG_0B, &data);
	if (!ret)
		upm->part_no = (data & REG0B_PN_MASK) >> REG0B_PN_SHIFT;

	return ret;
}

/*
int upm6910_enable_hvdcp(struct upm6910 *upm, bool en)
{
	const u8 val = en << REG0C_EN_HVDCP_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_0C, REG0C_EN_HVDCP_MASK,
				   val);
}

static int upm6910_set_dp(struct upm6910 *upm, int dp_stat)
{
	const u8 val = dp_stat << REG0C_DP_MUX_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_0C, REG0C_DP_MUX_MASK,
				   val);
}

static int upm6910_set_dm(struct upm6910 *upm, int dm_stat)
{
	const u8 val = dm_stat << REG0C_DM_MUX_SHIFT;

	return upm6910_update_bits(upm, UPM6910_REG_0C, REG0C_DM_MUX_MASK,
				   val);
}
*/

void upm6910_dump_regs(struct upm6910 *upm)
{
	int addr;
	u8 val;
	int ret;

	for (addr = 0x0; addr <= 0x0C; addr++) {
		ret = upm6910_read_byte(upm, addr, &val);
		if (ret == 0)
			pr_err("Reg[%.2x] = 0x%.2x\n", addr, val);
	}
}

int upm6910_get_input_volt_limit(struct upm6910 *upm, int *volt)
{
	u8 val;
	int ret;
	int vindpm;

	ret = upm6910_read_byte(upm, UPM6910_REG_06, &val);
	if (ret == 0){
		pr_err("Reg[%.2x] = 0x%.2x\n", UPM6910_REG_06, val);
	}

	//pr_err("%s: vindpm_reg_val:0x%X\n", __func__, val);
	vindpm = (val & REG06_VINDPM_MASK) >> REG06_VINDPM_SHIFT;
	vindpm = vindpm * REG06_VINDPM_LSB + REG06_VINDPM_BASE;
	*volt = vindpm * 1000;
	pr_err("%s: vindpm_reg_val:%d\n", __func__, *volt);

	return ret;
}

static struct upm6910_platform_data *upm6910_parse_dt(struct device_node *np,
						      struct upm6910 *upm)
{
	int ret;
	struct upm6910_platform_data *pdata;

	pdata = devm_kzalloc(&upm->client->dev, sizeof(struct upm6910_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_string(np, "charger_name", &upm->chg_dev_name) < 0) {
		upm->chg_dev_name = "slave_chg";
		pr_err("no charger name\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,usb-vlim", &pdata->usb.vlim);
	if (ret) {
		pdata->usb.vlim = 4700;
		pr_err("Failed to read node of upm,upm6910,usb-vlim\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,usb-ilim", &pdata->usb.ilim);
	if (ret) {
		pdata->usb.ilim = 2000;
		pr_err("Failed to read node of upm,upm6910,usb-ilim\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,usb-vreg", &pdata->usb.vreg);
	if (ret) {
		pdata->usb.vreg = 4200;
		pr_err("Failed to read node of upm,upm6910,usb-vreg\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,usb-ichg", &pdata->usb.ichg);
	if (ret) {
		pdata->usb.ichg = 2000;
		pr_err("Failed to read node of upm,upm6910,usb-ichg\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,stat-pin-ctrl",
				   &pdata->statctrl);
	if (ret) {
		pdata->statctrl = 0;
		pr_err("Failed to read node of upm,upm6910,stat-pin-ctrl\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,precharge-current",
				   &pdata->iprechg);
	if (ret) {
		pdata->iprechg = 180;
		pr_err("Failed to read node of upm,upm6910,precharge-current\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,termination-current",
				   &pdata->iterm);
	if (ret) {
		pdata->iterm = 180;
		pr_err
		    ("Failed to read node of upm,upm6910,termination-current\n");
	}

	ret =
	    of_property_read_u32(np, "upm,upm6910,boost-voltage",
				 &pdata->boostv);
	if (ret) {
		pdata->boostv = 5000;
		pr_err("Failed to read node of upm,upm6910,boost-voltage\n");
	}

	ret =
	    of_property_read_u32(np, "upm,upm6910,boost-current",
				 &pdata->boosti);
	if (ret) {
		pdata->boosti = 1200;
		pr_err("Failed to read node of upm,upm6910,boost-current\n");
	}

	ret = of_property_read_u32(np, "upm,upm6910,vac-ovp-threshold",
				   &pdata->vac_ovp);
	if (ret) {
		pdata->vac_ovp = 6500;
		pr_err("Failed to read node of upm,upm6910,vac-ovp-threshold\n");
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

	if (of_find_property(np, "upm,chg-en-gpio", NULL)) {
		pr_info("config en pin\n");
		upm->chg_en_gpio = of_get_named_gpio(np, "upm,chg-en-gpio", 0);
		if (!gpio_is_valid(upm->chg_en_gpio)) {
			pr_err("%d gpio get failed\n", upm->chg_en_gpio);
		}

		ret = gpio_request(upm->chg_en_gpio, "slave en pin");
		if (ret) {
			pr_err("%s: %d gpio request failed\n", upm->chg_en_gpio);
		}
		gpio_direction_output(upm->chg_en_gpio, 0);
	}

	dev_notice(upm->dev, "ichrg_curr:%d prechrg_curr:%d"
		" term_curr:%d input_curr_lim:%d"
		" cv:%d vindpm:%d ovp:%d\n",
		pdata->usb.ichg, pdata->iprechg,
		pdata->iterm, pdata->usb.ilim,
		pdata->usb.vreg, pdata->usb.vlim, pdata->vac_ovp);

	return pdata;
}

/*
static irqreturn_t upm6910_irq_handler(int irq, void *data)
{
	int ret;
	u8 reg_stat;
	u8 reg_fault;
	u8 reg_dpm;
	struct upm6910 *upm = (struct upm6910 *)data;

	pr_info("%s entry\n",__func__);

	ret = upm6910_read_byte(upm, UPM6910_REG_08, &reg_stat);
	ret += upm6910_read_byte(upm, UPM6910_REG_09, &reg_fault);
	ret += upm6910_read_byte(upm, UPM6910_REG_0A, &reg_dpm);
	if (ret){
		pr_err("%s read regs(08-09-0A) fail\n", __func__);
		return ret;
	}
	pr_info("%s: stat:0x%x, fault:0x%x, dpm:0x%x\n",
			__func__, reg_stat, reg_fault, reg_dpm);

	return IRQ_HANDLED;
}
*/

/*
static int upm6910_register_interrupt(struct upm6910 *upm)
{
	int ret = 0;
	ret = devm_gpio_request_one(upm->dev, upm->intr_gpio, GPIOF_DIR_IN,
			devm_kasprintf(upm->dev, GFP_KERNEL,
			"upm6910_intr_gpio.%s", dev_name(upm->dev)));
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
					upm6910_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"upm_irq", upm);
	if (ret < 0) {
		pr_err("request thread irq failed:%d\n", ret);
		return ret;
	}

	enable_irq_wake(upm->irq);

	return 0;
}
*/

static int upm6910_init_device(struct upm6910 *upm)
{
	int ret;

	ret = upm6910_disable_watchdog_timer(upm);
	ret += upm6910_exit_hiz_mode(upm);
	ret += upm6910_set_input_current_limit(upm, upm->platform_data->usb.ilim);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret += upm6910_set_input_volt_limit(upm, upm->platform_data->usb.vlim);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret += upm6910_set_chargecurrent(upm, upm->platform_data->usb.ichg);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret += upm6910_set_chargevolt(upm, upm->platform_data->usb.vreg);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret += upm6910_set_stat_ctrl(upm, upm->platform_data->statctrl);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n", ret);

	ret += upm6910_set_prechg_current(upm, upm->platform_data->iprechg);
	if (ret)
		pr_err("Failed to set prechg current, ret = %d\n", ret);

	ret += upm6910_set_term_current(upm, upm->platform_data->iterm);
	if (ret)
		pr_err("Failed to set termination current, ret = %d\n", ret);

	ret += upm6910_set_boost_voltage(upm, upm->platform_data->boostv);
	if (ret)
		pr_err("Failed to set boost voltage, ret = %d\n", ret);

	ret += upm6910_set_boost_current(upm, upm->platform_data->boosti);
	if (ret)
		pr_err("Failed to set boost current, ret = %d\n", ret);

	ret += upm6910_set_acovp_threshold(upm, upm->platform_data->vac_ovp);
	if (ret)
		pr_err("Failed to set acovp threshold, ret = %d\n", ret);

	ret += upm6910_update_bits(upm, UPM6910_REG_04, REG04_VRECHG_MASK, 0x0);
	ret += upm6910_set_int_mask(upm, REG0A_IINDPM_INT_MASK | REG0A_VINDPM_INT_MASK);
	ret += upm6910_disable_safety_timer(upm);
	if (ret)
		pr_err("Failed to set vindpm and iindpm int mask\n");

	return 0;
}

static ssize_t
upm6910_show_registers(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct upm6910 *upm = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "upm6910 Reg");
	for (addr = 0x0; addr <= 0x0B; addr++) {
		ret = upm6910_read_byte(upm, addr, &val);
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
upm6910_store_registers(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct upm6910 *upm = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < 0x0B) {
		upm6910_write_byte(upm, (unsigned char) reg,
				   (unsigned char) val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, upm6910_show_registers,
		   upm6910_store_registers);

static struct attribute *upm6910_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group upm6910_attr_group = {
	.attrs = upm6910_attributes,
};

static struct of_device_id upm6910_charger_match_table[] = {
	{
	 .compatible = "upm,upm6910d",
	 .data = &pn_data[PN_UPM6910D],
	 },
	{},
};

/***********************************************************************
 *
 * Add charger ops funcs
 *
 **********************************************************************/
#define UPM6910D_NAME "upm6910d"

static const struct charger_properties upm6910_chg_props = {
	.alias_name = UPM6910D_NAME,
};

static int upm6910_enable_hz(struct charger_device *chg_dev, bool hiz_en)
{
	struct upm6910 *upm = charger_get_data(chg_dev);

	if(hiz_en)
		return upm6910_enter_hiz_mode(upm);
	else
		return upm6910_exit_hiz_mode(upm);
}

static int upm6910_dump_register(struct charger_device *chg_dev)
{
	struct upm6910 *upm = charger_get_data(chg_dev);

	upm6910_dump_regs(upm);

	return 0;
}

static int upm6910_charging_switch(struct charger_device *chg_dev, bool enable)
{
	int ret;
	struct upm6910 *upm = charger_get_data(chg_dev);

	pr_info("enable:%d\n", enable);
	if (enable)
		ret = upm6910_enable_charger(upm);
	else
		ret = upm6910_disable_charger(upm);

	return ret;
}

static int upm6910_get_charging_current(struct charger_device *chg_dev, unsigned int *uA)
{
	int ret = 0;
	struct upm6910 *upm = charger_get_data(chg_dev);

	ret = upm6910_get_chargecurrent(upm, uA);

	return ret;
}

static int upm6910_set_charging_current(struct charger_device *chg_dev, unsigned int uA)
{
	int ret = 0;
	struct upm6910 *upm = charger_get_data(chg_dev);

	ret = upm6910_set_chargecurrent(upm, uA/1000);

	return ret;
}

static int upm6910_set_input_current(struct charger_device *chg_dev, unsigned int iindpm)
{
	struct upm6910 *upm = charger_get_data(chg_dev);

	return upm6910_set_input_current_limit(upm, iindpm/1000);
}

static int upm6910_get_input_current(struct charger_device *chg_dev,unsigned int *ilim)
{
	struct upm6910 *upm = charger_get_data(chg_dev);

	return upm6910_get_input_current_limit(upm, ilim);
}

static int upm6910_set_constant_voltage(struct charger_device *chg_dev, unsigned int chrg_volt)
{
	struct upm6910 *upm = charger_get_data(chg_dev);

	return upm6910_set_chargevolt(upm, chrg_volt/1000);
}

static int upm6910_get_constant_voltage(struct charger_device *chg_dev,unsigned int *volt)
{
	int ret;
	struct upm6910 *upm = charger_get_data(chg_dev);

	ret = upm6910_get_chargevolt(upm, volt);
	if (ret)
		return ret;

	return 0;
}

static int upm6910_reset_watch_dog_timer(struct charger_device
		*chg_dev)
{
	int ret;
	struct upm6910 *upm = charger_get_data(chg_dev);

	pr_info("kick watch dog\n");

	ret =upm6910_reset_watchdog_timer(upm);	/* RST watchdog */

	return ret;
}

static int upm6910_set_input_volt_lim(struct charger_device *chg_dev, unsigned int vindpm)
{
	struct upm6910 *upm = charger_get_data(chg_dev);

	return upm6910_set_input_volt_limit(upm, vindpm/1000);
}

static int upm6910_get_input_volt_lim(struct charger_device *chg_dev, u32 *uV)
{
	struct upm6910 *upm = charger_get_data(chg_dev);
	int ret = 0;

	ret = upm6910_get_input_volt_limit(upm, uV);

	return ret;
}

static int upm6910_get_charging_status(struct charger_device *chg_dev,
				       bool *is_done)
{
	struct upm6910 *upm = charger_get_data(chg_dev);
	u8 data, chrg_stat;
	u8 reg_val;
	int ret;

	*is_done = false;

	ret = upm6910_read_byte(upm, UPM6910_REG_08, &reg_val);
	if (ret)
		return ret;

	data = (reg_val & REG08_CHRG_STAT_MASK);
	chrg_stat = data >> REG08_CHRG_STAT_SHIFT;

	if (chrg_stat == REG08_CHRG_STAT_CHGDONE)
		*is_done = true;

	pr_info("%s: charing is done %d\n", __func__, *is_done);

	return 0;
}

static int upm6910_is_enabled(struct charger_device *chg_dev, bool *en)
{
	struct upm6910 *upm = charger_get_data(chg_dev);
	u8 data, chrg_stat;
	u8 reg_val;
	int ret;

	ret = upm6910_read_byte(upm, UPM6910_REG_08, &reg_val);
	if (ret)
		return ret;

	data = (reg_val & REG08_CHRG_STAT_MASK);
	chrg_stat = data >> REG08_CHRG_STAT_SHIFT;
	if (chrg_stat == REG08_CHRG_STAT_PRECHG || chrg_stat == REG08_CHRG_STAT_FASTCHG)
		*en = true;

	pr_info("%s: charging enable is %d\n", __func__, *en);

	return 0;
}

static int upm6910_enable_safetytimer(struct charger_device *chg_dev,bool en)
{
	struct upm6910 *upm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("%s: en %d\n", __func__, en);
	if (en)
		ret = upm6910_enable_safety_timer(upm);
	else
		ret = upm6910_disable_safety_timer(upm);

	return ret;
}

static int upm6910_get_is_safetytimer_enable(struct charger_device
		*chg_dev,bool *en)
{
	int ret = 0;
	u8 val = 0;
	struct upm6910 *upm = charger_get_data(chg_dev);

	pr_info("%s: en %d\n", __func__, en);
	ret = upm6910_read_byte(upm, UPM6910_REG_08, &val);
	if (ret < 0)
	{
		pr_info("read UPM6910_REG_05 fail\n");
		return ret;
	}
	*en = (val & REG05_EN_TIMER_MASK) >> REG05_EN_TIMER_SHIFT;

	return 0;
}

/*
static int upm6910_enable_power_path(struct charger_device
		*chg_dev, bool en)
{
	return upm6910_charging_switch(chg_dev, en);
}

static int upm6910_is_powerpath_enabled(struct charger_device
		*chg_dev, bool *en)
{
	struct upm6910 *upm = charger_get_data(chg_dev);
	u8 data;
	int ret = 0;

	ret = upm6910_read_byte(upm, UPM6910_REG_08, &data);
	if(ret < 0) {
		pr_err("reg read fail\n");
		return ret;
	}

	if ((data & REG08_CHRG_STAT_MASK) == 0)
		*en = true;
	else
		*en = false;

	pr_info("%s: en %d\n", __func__, *en);

	return ret;
}

static int upm6910_enable_otg_config(struct charger_device *chg_dev, bool en)
{
	struct upm6910 *upm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("en:%d\n", en);
	if (en) {
		ret = upm6910_enable_otg(upm);
		ret += upm6910_disable_watchdog_timer(upm);
	} else {
		ret = upm6910_disable_otg(upm);
		ret += upm6910_set_watchdog_timer(upm, REG05_WDT_40S);
	}

	return ret;
}

static int upm6910_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{
	int ret = 0;
	struct upm6910 *upm = charger_get_data(chg_dev);
	unsigned int avg = (BOOSTI_500 + BOOSTI_1200) / 2;

	uA = uA / 1000;
	pr_info("%s: uA:%d, avg:%d\n", __func__, uA, avg);

	if (uA >= avg)
		ret = upm6910_set_boost_current(upm, BOOSTI_1200);
	else
		ret = upm6910_set_boost_current(upm, BOOSTI_500);

	return ret;
}
*/

static int upm6910_do_event(struct charger_device *chg_dev, u32 event,
			    u32 args)
{
	if (chg_dev == NULL)
		return -EINVAL;

	pr_info("%s: event = %d\n", __func__, event);
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

static int upm6910_init(struct charger_device *chg_dev)
{
	struct upm6910 *upm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("++\n");
	upm6910_init_device(upm);

	return ret;
}

static int upm6910_plug_in(struct charger_device *chg_dev)
{
	pr_info("++\n");
	return upm6910_charging_switch(chg_dev, false);
}

static int upm6910_plug_out(struct charger_device *chg_dev)
{
	pr_info("++\n");
	return upm6910_charging_switch(chg_dev, false);
}

static struct charger_ops upm6910_chg_ops = {

	.enable_hz = upm6910_enable_hz,

	/* Normal charging */
	.init = upm6910_init,
	.dump_registers = upm6910_dump_register,
	.enable = upm6910_charging_switch,
	.get_charging_current = upm6910_get_charging_current,
	.set_charging_current = upm6910_set_charging_current,
	.get_input_current = upm6910_get_input_current,
	.set_input_current = upm6910_set_input_current,
	.get_constant_voltage = upm6910_get_constant_voltage,
	.set_constant_voltage = upm6910_set_constant_voltage,
	.kick_wdt = upm6910_reset_watch_dog_timer,
	.set_mivr = upm6910_set_input_volt_lim,
	.get_mivr = upm6910_get_input_volt_lim,
	.is_charging_done = upm6910_get_charging_status,
	.plug_in = upm6910_plug_in,
	.plug_out = upm6910_plug_out,
	.is_enabled = upm6910_is_enabled,

	/* Safety timer */
	.enable_safety_timer = upm6910_enable_safetytimer,
	.is_safety_timer_enabled = upm6910_get_is_safetytimer_enable,

	/* Power path */
	//.enable_powerpath = upm6910_enable_power_path,
	//.is_powerpath_enabled = upm6910_is_powerpath_enabled,

	/* OTG */
	//.enable_otg = upm6910_enable_otg_config,
	//.set_boost_current_limit = upm6910_set_boost_current_limit,
	.event = upm6910_do_event,
};

int upm6910_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct upm6910 *upm;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret = 0;

	upm = devm_kzalloc(&client->dev, sizeof(struct upm6910), GFP_KERNEL);
	if (!upm)
		return -ENOMEM;

	client->addr = 0x6B;
	upm->dev = &client->dev;
	upm->client = client;

	i2c_set_clientdata(client, upm);

	mutex_init(&upm->i2c_rw_lock);
	mutex_init(&upm->regulator_lock);

	ret = upm6910_detect_device(upm);
	if (ret) {
		pr_err("No upm6910 device found!\n");
		return -ENODEV;
	}

	match = of_match_node(upm6910_charger_match_table, node);
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

	upm->platform_data = upm6910_parse_dt(node, upm);
	if (!upm->platform_data) {
		pr_err("No platform data provided.\n");
		return -EINVAL;
	}

	ret = upm6910_init_device(upm);
	if (ret) {
		pr_err("Failed to init device\n");
		return ret;
	}

	/* Register charger device */
	upm->chg_dev = charger_device_register(upm->chg_dev_name,
						&client->dev, upm,
						&upm6910_chg_ops,
						&upm6910_chg_props);
	//upm6910_register_interrupt(upm);

	ret = sysfs_create_group(&upm->dev->kobj, &upm6910_attr_group);
	if (ret)
		pr_err("failed to register sysfs. err: %d\n", ret);

	upm6910_dump_regs(upm);
	pr_err("upm6910 probe successfully, Part Num:%d, Revision:%d\n!", upm->part_no);

	return 0;
}
EXPORT_SYMBOL_GPL(upm6910_charger_probe);

int upm6910_charger_remove(struct i2c_client *client)
{
	struct upm6910 *upm = i2c_get_clientdata(client);

	mutex_destroy(&upm->i2c_rw_lock);

	sysfs_remove_group(&upm->dev->kobj, &upm6910_attr_group);

	return 0;
}
EXPORT_SYMBOL_GPL(upm6910_charger_remove);

void upm6910_charger_shutdown(struct i2c_client *client)
{

}
EXPORT_SYMBOL_GPL(upm6910_charger_shutdown);
