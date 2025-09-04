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

#define pr_fmt(fmt)	"[cne-charge][sgm4154x] %s: " fmt, __func__

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
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include "sgm4154x.h"
#include "charger_class.h"
#include "cne_charger_intf.h"

/**********************************************************
 *
 *   [I2C Slave Setting]
 *
 *********************************************************/
/* SGM4154x REG06 BOOST_LIM[5:4], uV */
static const unsigned int BOOST_VOLT_LIMIT[] = {
	4850000, 5000000, 5150000, 5300000		
};
 /* SGM4154x REG02 BOOST_LIM[7:7], uA */
static const unsigned int BOOST_CURRENT_LIMIT[] = {
	1200000, 2000000
};

static const struct charger_properties sgm4154x_chg_props = {
	.alias_name = SGM4154X_NAME,
};

/**********************************************************
 *
 *   [Global Variable]
 *
 *********************************************************/
static struct sgm4154x_device *g_sgm_device = NULL;

/**********************************************************
 *
 *   [I2C Function For Read/Write sgm4154x]
 *
 *********************************************************/
static int __sgm4154x_read_byte(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
	int retry_count = I2C_RW_RETRY_COUNT;
    s32 ret;

	while (retry_count > 0) {
		ret = i2c_smbus_read_byte_data(sgm->client, reg);
		if (ret >= 0)
			break;
		retry_count--;
	}
    if (ret < 0) {
        pr_err("can't read from reg 0x%02X, ret=%d\n", reg, ret);
        return ret;
    }

    *data = (u8)ret;

    return 0;
}

static int __sgm4154x_write_byte(struct sgm4154x_device *sgm, int reg, u8 val)
{
	int retry_count = I2C_RW_RETRY_COUNT;
    s32 ret;

	while (retry_count > 0) {
		ret = i2c_smbus_write_byte_data(sgm->client, reg, val);
		if (ret >= 0)
			break;
		retry_count--;
	}
    if (ret < 0) {
        pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
               val, reg, ret);
        return ret;
    }
    return 0;
}

static int sgm4154x_read_reg(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, data);
	mutex_unlock(&sgm->i2c_rw_lock);

	return ret;
}

#if 0
static int sgm4154x_write_reg(struct sgm4154x_device *sgm, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_write_byte(sgm, reg, val);
	mutex_unlock(&sgm->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}
#endif

static int sgm4154x_update_bits(struct sgm4154x_device *sgm, u8 reg,
					u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __sgm4154x_write_byte(sgm, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sgm->i2c_rw_lock);
	return ret;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
 static int sgm4154x_set_term_curr(struct sgm4154x_device *sgm, int uA)
{
	u8 reg_val;

	if (uA < SGM4154X_TERMCHRG_I_MIN_uA)
		uA = SGM4154X_TERMCHRG_I_MIN_uA;
	else if (uA > SGM4154X_TERMCHRG_I_MAX_uA)
		uA = SGM4154X_TERMCHRG_I_MAX_uA;
	
	reg_val = (uA - SGM4154X_TERMCHRG_I_MIN_uA) / SGM4154X_TERMCHRG_CURRENT_STEP_uA;

	return sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_3,
				  SGM4154X_TERMCHRG_CUR_MASK, reg_val);
}

static int sgm4154x_set_prechrg_curr(struct sgm4154x_device *sgm, int uA)
{
	u8 reg_val;
	
	if (uA < SGM4154X_PRECHRG_I_MIN_uA)
		uA = SGM4154X_PRECHRG_I_MIN_uA;
	else if (uA > SGM4154X_PRECHRG_I_MAX_uA)
		uA = SGM4154X_PRECHRG_I_MAX_uA;

	reg_val = (uA - SGM4154X_PRECHRG_I_MIN_uA) / SGM4154X_PRECHRG_CURRENT_STEP_uA;

	reg_val = reg_val << 4;
	return sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_3,
				  SGM4154X_PRECHRG_CUR_MASK, reg_val);
}

static int sgm4154x_get_charging_current(struct charger_device *chg_dev, unsigned int *uA)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_2, &reg_val);
	if (ret) {
		pr_err("read reg fail\n");
		return ret;
	}

	*uA = (reg_val & SGM4154X_ICHRG_I_MASK) * SGM4154X_ICHRG_I_STEP_uA;
	if (*uA > SGM4154X_ICHRG_I_MAX_uA)
		*uA = SGM4154X_ICHRG_I_MAX_uA;

	pr_debug("uA:%d\n", *uA);

	return ret;
}

static int sgm4154x_set_ichrg_curr(struct sgm4154x_device *sgm, unsigned int uA)
{
	int ret = 0;
	u8 reg_val;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}

	pr_debug("uA:%d\n", uA);
	if (uA < SGM4154X_ICHRG_I_MIN_uA)
		uA = SGM4154X_ICHRG_I_MIN_uA;
	else if (uA > SGM4154X_ICHRG_I_MAX_uA)
		uA = SGM4154X_ICHRG_I_MAX_uA;

	if (uA > sgm->init_data.max_ichg)
		uA = sgm->init_data.max_ichg;

	reg_val = uA / SGM4154X_ICHRG_I_STEP_uA;

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_2,
				  SGM4154X_ICHRG_I_MASK, reg_val);
	
	return ret;
}

static int sgm4154x_set_charging_current(struct charger_device *chg_dev, unsigned int uA)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_set_ichrg_curr(sgm, uA);
}

static int sgm4154x_set_chrg_volt(struct sgm4154x_device *sgm, unsigned int chrg_volt)
{
	int ret;
	u8 val, reg_val, vreg_ft;
	int volt;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}

	if (chrg_volt < SGM4154X_VREG_V_MIN_uV)
		chrg_volt = SGM4154X_VREG_V_MIN_uV;
	else if (chrg_volt > SGM4154X_VREG_V_MAX_uV)
		chrg_volt = SGM4154X_VREG_V_MAX_uV;

	if (chrg_volt > sgm->init_data.max_vreg)
		chrg_volt = sgm->init_data.max_vreg;
	
	val = (chrg_volt - SGM4154X_VREG_V_MIN_uV) / SGM4154X_VREG_V_STEP_uV;
	reg_val = val << 3;
	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_4,
				  SGM4154X_VREG_V_MASK, reg_val);

	volt = val * SGM4154X_VREG_V_STEP_uV + SGM4154X_VREG_V_MIN_uV;
	if ((volt + SGM4154X_VREG_FT_ADD_8MV) < chrg_volt)
		vreg_ft = SGM4154X_VREG_FT_ADD_8MV;
	else
		vreg_ft = 0;

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_F,
			SGM4154X_VREG_FT_MASK, vreg_ft);

	vreg_ft = (vreg_ft & SGM4154X_VREG_FT_MASK) >> 6;
	pr_debug("set cv:%d, VREG:0x%x, VREG_FT:0x%x\n", chrg_volt, val, vreg_ft);

	return ret;
}

static int sgm4154x_set_constant_voltage(struct charger_device *chg_dev, unsigned int chrg_volt)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_set_chrg_volt(sgm, chrg_volt);
}

static int sgm4154x_get_constant_voltage(struct charger_device *chg_dev,unsigned int *volt)
{
	int ret;
	u8 vreg_val, vreg_ft;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_4, &vreg_val);
	if (ret)
		return ret;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_F, &vreg_ft);
	if (ret)
		return ret;	

	vreg_val = (vreg_val & SGM4154X_VREG_V_MASK)>>3;
	vreg_ft = (vreg_ft & SGM4154X_VREG_FT_MASK) >> 6;

	if (15 == vreg_val)
		*volt = 4352000; //default
	else if (vreg_val < 25)	
		*volt = vreg_val * SGM4154X_VREG_V_STEP_uV + SGM4154X_VREG_V_MIN_uV;

	if (vreg_ft == 0x1)
		*volt += 8000;  /* +8mV */
	else if (vreg_ft == 0x2)
		*volt -= 8000;  /* -8mV */
	else if (vreg_ft == 0x3)
		*volt -= 16000; /* -16mV */

	pr_debug("volt:%d, VREG:0x%x VREG_FT:0x%x\n", *volt, vreg_val, vreg_ft);

	return 0;
}

static int sgm4154x_set_vindpm_offset_os(struct sgm4154x_device *sgm,u8 offset_os)
{
	int ret;	
	
	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_F,
				  SGM4154X_VINDPM_OS_MASK, offset_os);
	
	if (ret){
		pr_err("update viindpm fail\n");
		return ret;
	}
	
	return ret;
}
static int sgm4154x_set_input_volt_lim(struct charger_device *chg_dev, unsigned int vindpm)
{
	int ret;
	unsigned int offset;
	u8 reg_val;
	u8 os_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_debug("vindpm:%d\n", vindpm);
	if (vindpm < SGM4154X_VINDPM_V_MIN_uV ||
	    vindpm > SGM4154X_VINDPM_V_MAX_uV)
 		return -EINVAL;	
	
	if (vindpm < 5900000){
		os_val = 0;
		offset = 3900000;
	}		
	else if (vindpm >= 5900000 && vindpm < 7500000){
		os_val = 1;
		offset = 5900000; //uv
	}		
	else if (vindpm >= 7500000 && vindpm < 10500000){
		os_val = 2;
		offset = 7500000; //uv
	}		
	else{
		os_val = 3;
		offset = 10500000; //uv
	}		

	sgm4154x_set_vindpm_offset_os(sgm,os_val);
	reg_val = (vindpm - offset) / SGM4154X_VINDPM_STEP_uV;	

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_6,
				  SGM4154X_VINDPM_V_MASK, reg_val); 

	return ret;
}

static int sgm4154x_get_input_volt_lim(struct charger_device *chg_dev, u32 *uV)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u8 reg_os, reg_dpm;
	int os = 0, vindpm = 0;
	int ret = 0;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_F, &reg_os);
	ret += sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_6, &reg_dpm);
	if (ret < 0) {
		pr_err("read 0x0F or 0x06 reg fail\n");
		return ret;
	}

	vindpm = (reg_dpm & SGM4154X_VINDPM_V_MASK) >> 0;
	os = (reg_os & SGM4154X_VINDPM_OS_MASK) >> 0;
	if (os == 0)
		*uV = (vindpm * 100 + 3900) * 1000;
	else if (os == 1)
		*uV = (vindpm * 100 + 5900) * 1000;
	else if (os == 2)
		*uV = (vindpm * 100 + 7500) * 1000;
	else if (os == 3)
		*uV = (vindpm * 100 + 10500) * 1000;

	pr_debug("get vindpm:%d\n", *uV);

	return ret;
}

static int sgm4154x_set_input_curr_lim(struct sgm4154x_device *sgm, unsigned int iindpm)
{
	int ret;
	u8 reg_val;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}

	pr_debug("iindpm:%d\n", iindpm);
	if (iindpm < SGM4154X_IINDPM_I_MIN_uA)
		iindpm = SGM4154X_IINDPM_I_MIN_uA;
	else if (iindpm > SGM4154X_IINDPM_I_MAX_uA)
		iindpm = SGM4154X_IINDPM_I_MAX_uA;

	if (iindpm >= SGM4154X_IINDPM_I_MIN_uA && iindpm <= 3100000)//default
		reg_val = (iindpm - SGM4154X_IINDPM_I_MIN_uA) / SGM4154X_IINDPM_STEP_uA;
	else if (iindpm > 3100000 && iindpm < SGM4154X_IINDPM_I_MAX_uA)
		reg_val = 0x1E;
	else
		reg_val = SGM4154X_IINDPM_I_MASK;

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_0,
				  SGM4154X_IINDPM_I_MASK, reg_val);
	return ret;
}

static int sgm4154x_set_input_current(struct charger_device *chg_dev, unsigned int iindpm)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_set_input_curr_lim(sgm, iindpm);
}

static int sgm4154x_get_input_current(struct charger_device *chg_dev,unsigned int *ilim)
{
	int ret;	
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	
	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_0, &reg_val);
	if (ret)
		return ret;

	if (SGM4154X_IINDPM_I_MASK == (reg_val & SGM4154X_IINDPM_I_MASK))
		*ilim =  SGM4154X_IINDPM_I_MAX_uA;
	else
		*ilim = (reg_val & SGM4154X_IINDPM_I_MASK) *
			SGM4154X_IINDPM_STEP_uA + SGM4154X_IINDPM_I_MIN_uA;

	pr_debug("ilim:%d\n", *ilim);
	return 0;
}

static int sgm4154x_get_state(struct sgm4154x_device *sgm,
			     struct sgm4154x_state *state)
{
	u8 chrg_stat;
	u8 fault;
	u8 chrg_param_0,chrg_param_1,chrg_param_2;
	int ret;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_STAT, &chrg_stat);
	if (ret){
		ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_STAT, &chrg_stat);
		if (ret){
			pr_err("read SGM4154X_CHRG_STAT fail\n");
			return ret;
		}
	}
	state->chrg_type = chrg_stat & SGM4154X_VBUS_STAT_MASK;
	state->chrg_stat = chrg_stat & SGM4154X_CHG_STAT_MASK;
	state->online = !!(chrg_stat & SGM4154X_PG_STAT);
	state->therm_stat = !!(chrg_stat & SGM4154X_THERM_STAT);
	state->vsys_stat = !!(chrg_stat & SGM4154X_VSYS_STAT);
	
	pr_err("chrg_type =%d, chrg_stat =%d, online = %d\n",
		state->chrg_type, state->chrg_stat, state->online);
	

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_FAULT, &fault);
	if (ret){
		pr_err("read SGM4154X_CHRG_FAULT fail\n");
		return ret;
	}
	state->chrg_fault = fault;	
	state->ntc_fault = fault & SGM4154X_TEMP_MASK;
	state->health = state->ntc_fault;
	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_0, &chrg_param_0);
	if (ret){
		pr_err("read SGM4154X_CHRG_CTRL_0 fail\n");
		return ret;
	}
	state->hiz_en = !!(chrg_param_0 & SGM4154X_HIZ_EN);
	
	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_5, &chrg_param_1);
	if (ret){
		pr_err("read SGM4154X_CHRG_CTRL_5 fail\n");
		return ret;
	}
	state->term_en = !!(chrg_param_1 & SGM4154X_TERM_EN);
	
	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_A, &chrg_param_2);
	if (ret){
		pr_err("read SGM4154X_CHRG_CTRL_A fail\n");
		return ret;
	}
	state->vbus_gd = !!(chrg_param_2 & SGM4154X_VBUS_GOOD);

	return 0;
}

static int sgm4154x_set_hiz_en(struct sgm4154x_device *sgm, bool hiz_en)
{
	u8 reg_val;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}

	pr_debug("hiz_en:%d\n", hiz_en);
	reg_val = hiz_en ? SGM4154X_HIZ_EN : 0;

	return sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_0,
				  SGM4154X_HIZ_EN, reg_val);
}

static int sgm4154x_enable_hz(struct charger_device *chg_dev, bool hiz_en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_set_hiz_en(sgm, hiz_en);
}

static int sgm4154x_enable_charger(struct sgm4154x_device *sgm)
{
    int ret;
    
    ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1, SGM4154X_CHRG_EN,
                     SGM4154X_CHRG_EN);
	
    return ret;
}

static int sgm4154x_disable_charger(struct sgm4154x_device *sgm)
{
    int ret;
    
    ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1, SGM4154X_CHRG_EN,
                     0);
    return ret;
}

static int sgm4154x_enable_watchdog_reset(struct sgm4154x_device *sgm)
{
    int ret;

    ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1,
			SGM4154X_REG01_WD_RST_MASK, SGM4154X_REG01_WD_RST_EN);

    return ret;
}

static int sgm4154x_charging_switch(struct charger_device *chg_dev,bool enable)
{
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_debug("enable:%d\n", enable);
	if (enable)
		ret = sgm4154x_enable_charger(sgm);
	else
		ret = sgm4154x_disable_charger(sgm);
	return ret;
}

static int sgm4154x_set_recharge_volt(struct sgm4154x_device *sgm, int mV)
{
	u8 reg_val;
	pr_debug("mV:%d", mV);
	reg_val = (mV - SGM4154X_VRECHRG_OFFSET_mV) / SGM4154X_VRECHRG_STEP_mV;

	return sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_4,
				  SGM4154X_VRECHARGE, reg_val);
}

static int sgm4154x_set_wdt_rst(struct sgm4154x_device *sgm, bool is_rst)
{
	u8 val;
	
	if (is_rst)
		val = SGM4154X_WDT_RST_MASK;
	else
		val = 0;
	return sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1,
				  SGM4154X_WDT_RST_MASK, val);	
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_dump_register(struct charger_device *chg_dev)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	unsigned char reg[SGM4154X_REG_NUM] = { 0 };
	/* title format: REG00 REG01 ... */
	unsigned char d_title[6 * SGM4154X_REG_NUM + 1] = { 0 };
	unsigned char d_value[6 * SGM4154X_REG_NUM + 1] = { 0 };
	int i = 0;
	int ret = 0;

	memset(d_title, 0, ARRAY_SIZE(d_title));
	memset(d_value, 0, ARRAY_SIZE(d_value));
	for (i = 0; i < SGM4154X_REG_NUM; i++) {
		ret = sgm4154x_read_reg(sgm, i, &reg[i]);
		if (ret < 0) {
			pr_info("[sgm4154x] i2c transfor error, errno=%d\n", ret);
			break;
		}

		sprintf(&d_title[6 * i], "reg%02x ", i);
		sprintf(&d_value[6 * i], "0x%02x  ", reg[i]);
	}
	pr_info("title: %s \n", d_title);
	pr_info("value: %s \n", d_value);

	return ret;
}


/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_hw_chipid_detect(struct sgm4154x_device *sgm)
{
	int ret = 0;
	u8 reg = 0, val = 0;
	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_B, &reg);
	if (ret < 0) {
		pr_err("read SGM4154X_CHRG_CTRL_B fail\n");
		return ret;
	}

	val = (reg & SGM4154X_PN_MASK) >> SGM4154X_PN_SHIFT;
	if (val != CHIP_ID_SGM41542) {
		pr_err("vendor id is reg:0x%x pn:0x%x", reg, val);
		return -ENODEV;
	}

	return 0;
}

static int sgm4154x_reset_watch_dog_timer(struct charger_device
		*chg_dev)
{
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_debug("kick watch dog\n");

	ret = sgm4154x_set_wdt_rst(sgm,0x1);	/* RST watchdog */	

	return ret;
}

static int sgm4154x_get_charging_status(struct charger_device *chg_dev,
				       bool *is_done)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u8 data, chrg_stat;
	int ret;

	*is_done = false;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_STAT, &data);
	if(ret < 0) {
		pr_err("reg read fail\n");
		return ret;
	}
	chrg_stat = data & SGM4154X_CHG_STAT_MASK;
	if (chrg_stat == SGM4154X_TERM_CHRG)
		*is_done = true;

	pr_debug("reg:0x%x, done:%d\n", data, *is_done);
	return 0;
}

static int sgm4154x_is_enabled(struct charger_device *chg_dev, bool *en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u8 data, chrg_stat;
	int ret;

	*en = false;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_STAT, &data);
	if(ret < 0) {
		pr_err("reg read fail\n");
		return ret;
	}
	chrg_stat = data & SGM4154X_CHG_STAT_MASK;
	if (chrg_stat == SGM4154X_PRECHRG || chrg_stat == SGM4154X_FAST_CHRG)
		*en = true;

	pr_debug("reg:0x%x, en:%d\n", data, *en);

	return 0;
}

static int sgm4154x_set_en_timer(struct sgm4154x_device *sgm)
{
	int ret;	

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_5,
				SGM4154X_SAFETY_TIMER_EN, SGM4154X_SAFETY_TIMER_EN);

	return ret;
}

static int sgm4154x_set_disable_timer(struct sgm4154x_device *sgm)
{
	int ret;	

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_5,
				SGM4154X_SAFETY_TIMER_EN, 0);

	return ret;
}

static int sgm4154x_enable_safetytimer(struct charger_device *chg_dev,bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	pr_debug("en:%d\n", en);
	if (en)
		ret = sgm4154x_set_en_timer(sgm);
	else
		ret = sgm4154x_set_disable_timer(sgm);
	return ret;
}

static int sgm4154x_get_is_safetytimer_enable(struct charger_device
		*chg_dev,bool *en)
{
	int ret = 0;
	u8 val = 0;
	
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_debug("en:%d\n", en);
	ret = sgm4154x_read_reg(sgm,SGM4154X_CHRG_CTRL_5,&val);
	if (ret < 0)
	{
		pr_err("read SGM4154X_CHRG_CTRL_5 fail\n");
		return ret;
	}
	*en = !!(val & SGM4154X_SAFETY_TIMER_EN);
	return 0;
}

static int sgm4154x_enable_power_path(struct charger_device
		*chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	pr_debug("en:%d\n", en);

	if (en)
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1,
				SGM4154X_CHRG_EN, 1);
	else
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1,
				SGM4154X_CHRG_EN, 0);

	return ret;
}

static int sgm4154x_is_powerpath_enabled(struct charger_device
		*chg_dev, bool *en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	u8 data;
	int ret = 0;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_STAT, &data);
	if(ret < 0) {
		pr_err("reg read fail\n");
		return ret;
	}

	if ((data & SGM4154X_CHRG_EN) == 0)
		*en = true;
	else
		*en = false;

	pr_debug("en:%d\n", *en);

	return ret;
}

static int sgm4154x_set_watchdog_time(struct sgm4154x_device *sgm, int time)
{
	int ret;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_5,
				SGM4154X_WDT_TIMER_MASK, time);

	return ret;
}

static int sgm4154x_get_vbus_voltage(struct sgm4154x_device *sgm, int *mV)
{
	int value;
	int ret = 0;

	if (!sgm->vbus_iio_channel) {
		sgm->vbus_iio_channel = devm_iio_channel_get(sgm->dev, "vbus_adc");
		if (IS_ERR(sgm->vbus_iio_channel)) {
			sgm->vbus_iio_channel = NULL;
			pr_err("get vbus iio fail\n");
			return -ENODEV;
		}
	}

	ret = iio_read_channel_processed(sgm->vbus_iio_channel, &value);
	if (ret < 0) {
		pr_err("read vbus iio channel fail\n");
		return ret;
	}

	*mV = (((R_CHARGER_1 + R_CHARGER_2) * value) / R_CHARGER_2) / ADAPTER_MV_TO_UV;
	pr_debug("Vresis=%d, vbus=%dmV\n", value, *mV);

	return ret;
}

static int sgm4154x_psy_online_changed(struct sgm4154x_device *sgm)
{
	int ret = 0;
	union power_supply_propval propval;

	/* Get chg type det power supply */
	if (!sgm->charger) {
		sgm->charger = power_supply_get_by_name("charger");
		if (!sgm->charger) {
			pr_err("charger psy get fail\n");
 			return -EINVAL;
		}
	}

	propval.intval = sgm->attach;
	ret = power_supply_set_property(sgm->charger, 
			POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0)
		pr_err("psy online fail(%d)\n", ret);
	else
		pr_info("pwr_rdy = %d\n", sgm->attach);

	return ret;
}

static int sgm4154x_chg_type_changed(struct sgm4154x_device *sgm)
{
	int ret = 0;

	union power_supply_propval propval;

	/* Get chg type det power supply */
	if (!sgm->charger) {
		sgm->charger = power_supply_get_by_name("charger");
		if (!sgm->charger) {
			pr_err("charger psy get fail\n");
 			return -EINVAL;
		}
	}

	propval.intval = sgm->chg_type;
	ret = power_supply_set_property(sgm->charger,
					POWER_SUPPLY_PROP_CHARGE_TYPE,
					&propval);
	if (ret < 0)
		pr_err("psy type failed, ret = %d\n", ret);
	else
		pr_info("chg_type = %d\n", sgm->chg_type);

	return ret;
}

static int sgm4154x_hvdcp_type_changed(struct sgm4154x_device *sgm)
{
	int ret = 0;

	union power_supply_propval propval;

	/* Get chg type det power supply */
	if (!sgm->charger) {
		sgm->charger = power_supply_get_by_name("charger");
		if (!sgm->charger) {
			pr_err("charger psy get fail\n");
			return -EINVAL;
		}
	}

	propval.intval = sgm->chg_type;
	ret = power_supply_set_property(sgm->charger,
					POWER_SUPPLY_PROP_TYPE,
					&propval);
	if (ret < 0)
		pr_err("psy type failed, ret = %d\n", ret);
	else
		pr_info("chg_type = %d\n", sgm->chg_type);

	return ret;
}


static int sgm4154x_bc12_postprocess(struct sgm4154x_device *sgm)
{

	struct sgm4154x_state state;
	bool attach = false, inform_psy = true;
	int ret;

	pr_info("++ enter\n");

	if(!sgm) {
		pr_err("Cann't get sgm4154x_device\n");
		return -ENODEV;
	}

	attach = sgm->tcpc_attach;
	if (sgm->attach == attach) {
		pr_info("attach(%d) is the same\n", attach);
		inform_psy = !attach;
	}
	sgm->attach = attach;
	pr_info("attach = %d\n", attach);

	// if (!sgm->charger_wakelock->active)
	//	__pm_stay_awake(sgm->charger_wakelock);

	/* Plug out during BC12 */
	if (!attach) {
		sgm->chg_type = CHARGER_UNKNOWN;
		goto out;
	}

	/* Plug in */
	ret = sgm4154x_get_state(sgm, &state);
	mutex_lock(&sgm->lock);
	sgm->state = state;	
	mutex_unlock(&sgm->lock);	
	
	if(!sgm->state.vbus_gd) {
		pr_err("Vbus not present, disable charge\n");
		sgm4154x_disable_charger(sgm);
		sgm->chg_type = CHARGER_UNKNOWN;
		goto out;
	}
	if(!state.online)
	{
		pr_err("Vbus not online\n");
		sgm->chg_type = CHARGER_UNKNOWN;
		goto out;
	}	

	switch(sgm->state.chrg_type) {
		case SGM4154X_USB_SDP:
			pr_info("SGM4154x charger type: SDP\n");
			sgm->chg_type = STANDARD_HOST;
			break;

		case SGM4154X_USB_CDP:
			pr_info("SGM4154x charger type: CDP\n");
			sgm->chg_type = CHARGING_HOST;
			break;

		case SGM4154X_USB_DCP:
			pr_info("SGM4154x charger type: DCP\n");
			sgm->chg_type = STANDARD_CHARGER;
			break;

		case SGM4154X_UNKNOWN:
			pr_info("SGM4154x charger type: UNKNOWN\n");
			sgm->chg_type = NONSTANDARD_CHARGER;
			break;	

		case SGM4154X_NON_STANDARD:
			pr_info("SGM4154x charger type: NON_STANDARD\n");
			sgm->chg_type = NONSTANDARD_CHARGER;
			break;

		default:
			pr_info("SGM4154x charger type: default\n");
			sgm->chg_type = CHARGER_UNKNOWN;
			break;
	}

out:
	//release wakelock
	sgm4154x_dump_register(sgm->chg_dev);

	sgm4154x_psy_online_changed(sgm);
	if (!sgm->ignore_detect)
		sgm4154x_chg_type_changed(sgm);
	// pr_info("Relax wakelock\n");
	// __pm_relax(sgm->charger_wakelock);
	return 0;
}

/* Notify charger driver to VBUS attach/disattach */
static int sgm4154x_notify_vbus_changed(int attach)
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

static int sgm4154x_hw_init(struct sgm4154x_device *sgm)
{
	int ret = 0;	
	struct power_supply_battery_info bat_info = { };	

	bat_info.constant_charge_current_max_ua =
			SGM4154X_ICHRG_I_DEF_uA;

	bat_info.constant_charge_voltage_max_uv =
			SGM4154X_VREG_V_DEF_uV;

	bat_info.precharge_current_ua =
			SGM4154X_PRECHRG_I_DEF_uA;

	bat_info.charge_term_current_ua =
			SGM4154X_TERMCHRG_I_DEF_uA;

	sgm->init_data.max_ichg =
			SGM4154X_ICHRG_I_MAX_uA;

	ret = sgm4154x_set_ichrg_curr(sgm,
				bat_info.constant_charge_current_max_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_prechrg_curr(sgm, bat_info.precharge_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_chrg_volt(sgm,
				bat_info.constant_charge_voltage_max_uv);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_term_curr(sgm, bat_info.charge_term_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_input_curr_lim(sgm, sgm->init_data.ilim);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_recharge_volt(sgm, 100);//100~200mv
	if (ret)
		goto err_out;

	ret = sgm4154x_set_watchdog_time(sgm, SGM4154X_WDT_TIMER_40S);
	if (ret)
		goto err_out;

	/* Enter and exit hiz clear charging terminated flag */
	ret = sgm4154x_set_hiz_en(sgm, true);
	if (ret)
		pr_err("HIZ clear charging terminate failed!\n");
	ret = sgm4154x_set_hiz_en(sgm, false);
	if (ret)
		goto err_out;

	/* disable vindpm & iindpm irq */
	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_A, 0x3, 0x3);
	if (ret)
		goto err_out;

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_F,
			SGM4154X_VINDPM_OS_MASK, 0);
	if (ret)
		goto err_out;

	//OTG setting
	//sgm4154x_set_otg_voltage(g_sgm_device, 5000000); //5V
	//sgm4154x_set_otg_current(g_sgm_device, 1200000); //1.2A
	
	dev_notice(sgm->dev, "ichrg_curr:%d prechrg_curr:%d chrg_vol:%d"
		" term_curr:%d input_curr_lim:%d",
		bat_info.constant_charge_current_max_ua,
		bat_info.precharge_current_ua,
		bat_info.constant_charge_voltage_max_uv,
		bat_info.charge_term_current_ua,
		sgm->init_data.ilim);

	return 0;

err_out:
	return ret;

}

static int sgm4154x_parse_dt(struct sgm4154x_device *sgm)
{
	int ret;	
	int irqn = 0;

	ret = device_property_read_string(sgm->dev, "charger_name",
				    &sgm->chg_dev_name);
	if (ret < 0) {
		pr_info("use default charger name.\n");
		sgm->chg_dev_name = "primary_chg";
	}

	sgm->enable_otg_regulator = device_property_read_bool(sgm->dev,
				       "sgm,enable-otg-regulator");

	ret = device_property_read_u32(sgm->dev,
				       "max_vreg_uv",
				       &sgm->init_data.max_vreg);
	if (ret)
		sgm->init_data.max_vreg = SGM4154X_VREG_V_MAX_uV;

	ret = device_property_read_u32(sgm->dev,
				       "input-voltage-limit-microvolt",
				       &sgm->init_data.vlim);
	if (ret)
		sgm->init_data.vlim = SGM4154X_VINDPM_DEF_uV;

	if (sgm->init_data.vlim > SGM4154X_VINDPM_V_MAX_uV ||
	    sgm->init_data.vlim < SGM4154X_VINDPM_V_MIN_uV) {
	    dev_err(sgm->dev, "%s: VINDPM get failed\n", __func__);
		return -EINVAL;
	}

	ret = device_property_read_u32(sgm->dev,
				       "input-current-limit-microamp",
				       &sgm->init_data.ilim);
	if (ret)
		sgm->init_data.ilim = SGM4154X_IINDPM_DEF_uA;

	if (sgm->init_data.ilim > SGM4154X_IINDPM_I_MAX_uA ||
	    	sgm->init_data.ilim < SGM4154X_IINDPM_I_MIN_uA) {
	    dev_err(sgm->dev, "%s: IINDPM get failed\n", __func__);
		return -EINVAL;
	}

	sgm->pinctrl_state_name = of_get_property(sgm->dev->of_node, 
			"pinctrl-names", NULL);

	/* configure irq_pinctrl to enable irqs */
	if (sgm->pinctrl_state_name) {
		sgm->irq_pinctrl = pinctrl_get_select(sgm->dev,
						sgm->pinctrl_state_name);
		if (IS_ERR(sgm->irq_pinctrl)) {
			pr_err("Could not get/set %s pinctrl state rc = %ld\n",
						sgm->pinctrl_state_name,
						PTR_ERR(sgm->irq_pinctrl));
			return PTR_ERR(sgm->irq_pinctrl);
		}
	}

	if (of_get_property(sgm->dev->of_node, "sgm,irq-gpio", NULL)) {
		pr_info("parse sgm,irq-gpio info.\n");
		sgm->irq_gpio = of_get_named_gpio(sgm->dev->of_node, "sgm,irq-gpio", 0);
		if (!gpio_is_valid(sgm->irq_gpio))
		{
			dev_err(sgm->dev, "%s: %d gpio get failed\n", __func__, sgm->irq_gpio);
			return -EINVAL;
		}
		ret = gpio_request(sgm->irq_gpio, "sgm4154x irq");
		if (ret) {
			dev_err(sgm->dev, "%s: %d gpio request failed\n", __func__, sgm->irq_gpio);
			return ret;
		}
		gpio_direction_input(sgm->irq_gpio);
		irqn = gpio_to_irq(sgm->irq_gpio);
		if (irqn < 0) {
			dev_err(sgm->dev, "%s:%d gpio_to_irq failed\n", __func__, irqn);
			goto fail_free_gpio;
		}
		sgm->client->irq = irqn;
	} else {
		pr_info("sgm,irq-gpio not found.\n");
		sgm->client->irq = 0;
	}

	return 0;

fail_free_gpio:
	if (sgm->irq_gpio)
		gpio_free(sgm->irq_gpio);
	return ret;
}

static int sgm4154x_enable_vbus(struct regulator_dev *rdev)
{	
	int ret = 0;
	struct sgm4154x_device *sgm = g_sgm_device;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}
	
	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1,
		SGM4154X_OTG_EN, SGM4154X_OTG_EN);
	return ret;
}

static int sgm4154x_disable_vbus(struct regulator_dev *rdev)
{
	int ret = 0;
	struct sgm4154x_device *sgm = g_sgm_device;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1,
		SGM4154X_OTG_EN, 0);

	return ret;
}

static int sgm4154x_is_enabled_vbus(struct regulator_dev *rdev)
{
	u8 temp = 0;
	int ret = 0;
	struct sgm4154x_device *sgm = g_sgm_device;

	if (!sgm) {
		pr_err("no sgm device\n");
		return -ENODEV;
	}

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_1, &temp);
	return (temp & SGM4154X_OTG_EN)? 1 : 0;
}

static int sgm4154x_enable_otg(struct charger_device *chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("en:%d\n", en);
	if (en) {
		ret = sgm4154x_enable_vbus(NULL);
		ret += sgm4154x_set_watchdog_time(sgm,
				SGM4154X_WDT_TIMER_DISABLE);
	} else {
		ret = sgm4154x_disable_vbus(NULL);
		ret += sgm4154x_set_watchdog_time(sgm,
				SGM4154X_WDT_TIMER_40S);
	}
	return ret;
}

static int sgm4154x_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{	
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	unsigned int avg = (BOOST_CURRENT_LIMIT[0] + BOOST_CURRENT_LIMIT[1]) / 2;

	pr_debug("uA:%d, avg:%d\n", uA, avg);
	if (uA >= avg)
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_2, SGM4154X_BOOST_LIM,
                     BIT(7)); 
	else
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_2, SGM4154X_BOOST_LIM,
                     0);
	
	return ret;
}

static int sgm4154x_do_event(struct charger_device *chg_dev, u32 event,
			    u32 args)
{
	if (chg_dev == NULL)
		return -EINVAL;

	pr_debug("event = %d\n", event);
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

static bool sgm4154x_vbus_check(struct sgm4154x_device *sgm)
{
	u8 temp = 0;
	bool vbus_gd = false;
	int ret = 0;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_A, &temp);
	if(ret < 0) {
		pr_err("REG[0x0A] read fail\n");
		return false;
	}

	vbus_gd = !!(temp & SGM4154X_VBUS_GOOD);
	if (!vbus_gd)
		pr_info("reg0A:0x%x, vbus_gd:%d\n", temp, vbus_gd);

	return vbus_gd;
}

static int sgm4154x_enable_dpdm_func(struct sgm4154x_device *sgm)
{
	u8 temp = 0;
	int ret = 0;

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_7, &temp);
	if(ret < 0)
		pr_err("REG[0x07] read fail, before\n");

	pr_info("REG[0x07]=0x%x, before\n", temp);

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_7,
			SGM4154X_IINDET_EN_MASK, BIT(7));
	if(ret < 0) {
		pr_err("enable charge type detect fail.\n");
		return ret;
	}

	ret = sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_7, &temp);
	if(ret < 0)
		pr_err("REG[0x07] read fail, after\n");
	pr_info("REG[0x07]=0x%x, after\n", temp);

	return 0;
}

static int sgm4154x_bc12_preprocess(struct sgm4154x_device *sgm)
{
	int ret = 0;

	pr_info("++\n");

	if(sgm4154x_enable_dpdm_func(sgm)) {
		pr_err("enable charge type detect fail.\n");
		goto error;
	}

error:
	pr_info("--\n");
	return ret;
}

static int sgm4154x_ignore_chg_type_det(struct charger_device *chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("en:%d\n", en);
	if(!sgm){
		pr_info("sgm is null.\n");
		return -ENODEV;
	}

	mutex_lock(&sgm->detect_lock);
	sgm->ignore_detect = en;
	atomic_set(&sgm->chg_type_check_cnt, 1);
	wake_up(&sgm->waitq);
	mutex_unlock(&sgm->detect_lock);

	return ret;
}

static int sgm4154x_enable_chg_type_det(struct charger_device *chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	pr_info("en:%d\n", en);
	if(!sgm){
		pr_info("sgm is null.\n");
		return -ENODEV;
	}

	mutex_lock(&sgm->detect_lock);
	if (sgm->tcpc_attach == en) {
		pr_info("attach(%d) is the same\n", sgm->tcpc_attach);
		goto out;
	}

	sgm->tcpc_attach = en;
	sgm->detect_stage = en ? 
		DETECT_TYPE_CONNECT : DETECT_TYPE_DISCONNECT;
	atomic_set(&sgm->chg_type_check_cnt, 1);
	atomic_set(&sgm->vbus_check_cnt, 6); /* for vbus check */
	atomic_set(&sgm->input_check_cnt, 6); /* for input check */
	wake_up(&sgm->waitq);

out:
	mutex_unlock(&sgm->detect_lock);
	return ret;
}

static int sgm4154x_enable_hvdcp_type_detect(struct charger_device *chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	mutex_lock(&sgm->detect_lock);
	if (sgm->enable_hvdcp_check == en) {
		pr_info("%s hvdcp attach(%d) is the same\n", sgm->enable_hvdcp_check);
		goto out;
	}

	sgm->enable_hvdcp_check = en;
	sgm->detect_stage = en ? 
		DETECT_TYPE_HVDCP : DETECT_TYPE_DISCONNECT;
	sgm->hvdcp_detect_retry_count = 0;
	atomic_set(&sgm->chg_type_check_cnt, 1);
	wake_up(&sgm->waitq);

out:
	mutex_unlock(&sgm->detect_lock);

	return ret;
}

static int sgm4154x_plug_out(struct charger_device *chg_dev)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	// mutex_lock(&sgm->detect_lock);
	// sgm->attach = false;
	// sgm->tcpc_attach = false;
	// mutex_unlock(&sgm->detect_lock);

	ret = sgm4154x_set_vindpm_offset_os(sgm, 0);
	if (ret)
		pr_err("Failed to set vindpm_os, ret = %d\n", ret);

	return ret;
}

static int sgm4154x_hvdcp_precheck(struct sgm4154x_device *sgm)
{
	int ret = 0;
	int dp_val, dm_val;

	pr_info("++\n");
	/*dp and dm connected,dp 0.6V dm Hiz*/
    dp_val = 0x2<<3;
    ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
				  SGM4154X_DP_VSEL_MASK, dp_val); //dp 0.6V
    if (ret) {
		pr_err("update dp vsel fail\n");
        return ret;
    }

	dm_val = 0;
	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
				  SGM4154X_DM_VSEL_MASK, dm_val); //dm Hiz
    if (ret) {
		pr_err("update dm vsel fail\n");
        return ret;
    }

	return ret;
}

static int sgm4154x_hvdcp_postcheck(struct sgm4154x_device *sgm)
{
	int ret = 0;
	int dp_val, dm_val, dpdm;
	const int dpdm_mask = (SGM4154X_DP_VSEL_MASK | SGM4154X_DM_VSEL_MASK);
	int vbus;

	pr_info("++\n");

	sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1, SGM4154X_CHRG_EN,
                     0);

	/* dp 3.3v and dm 0.6v out 9V */
	dp_val = SGM4154X_DPDM_VSEL_3P3V << SGM4154X_DP_VSEL_SHIFT;
	dm_val = SGM4154X_DPDM_VSEL_0P6V << SGM4154X_DM_VSEL_SHIFT;
	dpdm = (dp_val | dm_val);
	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
				  dpdm_mask, dpdm);
    if (ret < 0) {
		pr_err("dpdm vsel set 9V fail\n");
		ret = -1;
		goto out;
    }

	msleep(500);

	ret = sgm4154x_get_vbus_voltage(sgm, &vbus);
	if (ret < 0) {
		pr_err("get vbus voltage fail\n");
		ret = -2;
		goto out;
	}

	/* restore vbus voltage to 5V */
	/* dp 0.6v and dm 0v out 5V */
	dp_val = SGM4154X_DPDM_VSEL_0P6V << SGM4154X_DP_VSEL_SHIFT;
	dm_val = SGM4154X_DPDM_VSEL_0V << SGM4154X_DM_VSEL_SHIFT;
	dpdm = (dp_val | dm_val);
	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
				  dpdm_mask, dpdm);
	if (ret < 0)
		pr_err("dpdm vsel set 5V fail\n");

	msleep(100);

	if (vbus >= HVDCP_HV_THRESHOLD_MV_MIN) {
		sgm->chg_type = HVDCP_QC20_CHARGER;
		ret = 0;
		goto out;
	}

	ret = 1;

out:
	return ret;
}

static int sgm4154x_hvdcp_disconnect(struct sgm4154x_device *sgm)
{
	int ret = 0;

	pr_info("++ \n");

	/* dp and dm to Hiz*/
    ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
				  SGM4154X_DP_VSEL_MASK | SGM4154X_DM_VSEL_MASK, 0);
    if (ret) {
		pr_err("update dpdm to hiz fail\n");
        return ret;
    }

	/* reset hvdcp check */
	sgm->enable_hvdcp_check = false;

	return ret;
}

static int sgm4154x_enable_hvdcp_voltage(struct charger_device *chg_dev, u32 uV)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;
	int voltage;
	int dp_val, dm_val, dpdm;
	const int dpdm_mask = (SGM4154X_DP_VSEL_MASK | SGM4154X_DM_VSEL_MASK);

	voltage = uV / ADAPTER_V_TO_UV;
	switch (voltage) {
	case ADAPTER_DPDM_HIZ:
		dp_val = SGM4154X_DPDM_VSEL_HIZ << SGM4154X_DP_VSEL_SHIFT;
		dm_val = SGM4154X_DPDM_VSEL_HIZ << SGM4154X_DM_VSEL_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
					  dpdm_mask, dpdm);
		if (ret) {
			pr_err("update dpdm vsel to hiz fail\n");
			return ret;
		}
		break;

	case ADAPTER_5V:
		/* dp 0.6v and dm 0v out 5V */
		dp_val = SGM4154X_DPDM_VSEL_0P6V << SGM4154X_DP_VSEL_SHIFT;
		dm_val = SGM4154X_DPDM_VSEL_0V << SGM4154X_DM_VSEL_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
					  dpdm_mask, dpdm);
		if (ret) {
			pr_err("update dpdm vsel to 5V fail\n");
			return ret;
		}
		break;

	case ADAPTER_9V:
		/* dp 3.3v and dm 0.6v out 9V */
		dp_val = SGM4154X_DPDM_VSEL_3P3V << SGM4154X_DP_VSEL_SHIFT;
		dm_val = SGM4154X_DPDM_VSEL_0P6V << SGM4154X_DM_VSEL_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
					  dpdm_mask, dpdm);
		if (ret) {
			pr_err("update dpdm vsel to 9V fail\n");
			return ret;
		}
		break;
	case ADAPTER_12V:
		/* dp 0.6v and dm 0.6v out 12V */
		dp_val = SGM4154X_DPDM_VSEL_0P6V << SGM4154X_DP_VSEL_SHIFT;
		dm_val = SGM4154X_DPDM_VSEL_0P6V << SGM4154X_DM_VSEL_SHIFT;
		dpdm = (dp_val | dm_val);
		ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
					  dpdm_mask, dpdm);
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

static int sgm4154x_get_vbus(struct charger_device *chg_dev, u32 *vbus)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	return sgm4154x_get_vbus_voltage(sgm, vbus);
}

static int sgm4154x_detect_task_threadfn(void *data)
{
	struct sgm4154x_device *sgm = data;
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
		ret = wait_event_interruptible(sgm->waitq,
					     atomic_read(&sgm->chg_type_check_cnt) > 0);
		if (ret < 0) {
			pr_info("wait event been interrupted(%d)\n", ret);
			continue;
		}

		mutex_lock(&sgm->detect_lock);

		need_wait = false;
		attach = sgm->tcpc_attach;
		stage = sgm->detect_stage;

		/* Power Delivery Charger, ignore BC12 */
		if (attach && sgm->ignore_detect) {
			pr_info("ignore charger detect\n");

			if (sgm->detect_stage == DETECT_TYPE_HVDCP ||
				sgm->detect_stage == DETECT_TYPE_HVDCP_CHECK)
					is_hvdcp_check = true;

			sgm->detect_stage = DETECT_TYPE_CHECK_DONE;
			atomic_set(&sgm->chg_type_check_cnt, 0);
			sgm4154x_hvdcp_disconnect(sgm);
			if (is_hvdcp_check)
				sgm4154x_hvdcp_type_changed(sgm);

			goto next_loop;
		}

		switch (stage) {
			case DETECT_TYPE_CONNECT:
				pr_info("enter DETECT_TYPE_CONNECT\n");
				if(!attach) {
					pr_err("usb disconnect, goto next check\n", attach);
					goto next_loop;
				}

				if(!sgm4154x_vbus_check(sgm)) {
					pr_info("vbus check count:%d\n", atomic_read(&sgm->vbus_check_cnt));
					atomic_dec(&sgm->vbus_check_cnt);
					if (atomic_read(&sgm->vbus_check_cnt) > 0) {
						need_wait = true;
						goto next_loop;
					}
				}

				ret = sgm4154x_hvdcp_disconnect(sgm);
				ret += sgm4154x_bc12_preprocess(sgm);
				if(ret < 0) {
					pr_err("enable dpdm detect fail\n", attach);
					sgm->detect_stage = DETECT_TYPE_CHECK_DONE;
					goto next_loop;
				}

				atomic_set(&sgm->chg_type_check_cnt,
					BC12_CHECK_DEFAULT_DELAY_COUNT);
				sgm->detect_stage = DETECT_TYPE_CHECK;
				need_wait = true;
				break;

			case DETECT_TYPE_CHECK:
				pr_info("enter DETECT_TYPE_CHECK\n");
				if(!attach) {
					pr_err("usb disconnect, goto next check\n", attach);
					atomic_set(&sgm->chg_type_check_cnt, 1);
					need_wait = false;
					sgm->detect_stage = DETECT_TYPE_DISCONNECT;
					goto next_loop;
				}

				ret = sgm4154x_read_reg(sgm, SGM4154X_INPUT_DET, &reg);
				if (ret) {
					pr_err("read SGM4154X_INPUT_DET fail ret=%d\n", ret);
					atomic_dec(&sgm->input_check_cnt);
					if (atomic_read(&sgm->input_check_cnt) > 0) {
						pr_info("input check count ++\n");
						need_wait = true;
					} else {
						pr_info("Retry read SGM4154X_INPUT_DET failed!\n");
						atomic_set(&sgm->chg_type_check_cnt, 1);
						need_wait = false;
						sgm->detect_stage = DETECT_TYPE_DISCONNECT;
					}
					goto next_loop;
				}

				done = (reg & SGM4154X_DET_DONE_MASK)
						>> SGM4154X_DET_DONE_SHIFT;
				pr_info("DET_DONE reg:0x%x done=%d attach:%d\n", reg, done, attach);
				if (done) {
					sgm4154x_bc12_postprocess(sgm);
					sgm->detect_stage = DETECT_TYPE_CHECK_DONE;
				} else if (atomic_read(&sgm->chg_type_check_cnt) > 0) {
					pr_info("chg_type_check_cnt:%d\n",
							atomic_read(&sgm->chg_type_check_cnt));
					atomic_dec(&sgm->chg_type_check_cnt);
					sgm->detect_stage = DETECT_TYPE_CHECK;
					need_wait = true;
				} else {
					pr_err("didnot detected the result\n");
					atomic_set(&sgm->chg_type_check_cnt, 1);
					sgm->detect_stage = DETECT_TYPE_DISCONNECT;
				}
				break;

			case DETECT_TYPE_DISCONNECT:
				pr_info("enter DETECT_TYPE_DISCONNECT\n");
				atomic_set(&sgm->chg_type_check_cnt, 0);
				sgm4154x_hvdcp_disconnect(sgm);
				sgm4154x_bc12_postprocess(sgm);
				break;

			case DETECT_TYPE_HVDCP:
				pr_info("enter detect HVDCP\n");
				sgm4154x_hvdcp_precheck(sgm);
				atomic_set(&sgm->chg_type_check_cnt,
					HVDCP_CHECK_DEFAULT_DELAY_COUNT);
				sgm->hvdcp_check_begin_time = ktime_get_boottime();
				sgm->detect_stage = DETECT_TYPE_HVDCP_CHECK;
				break;

			case DETECT_TYPE_HVDCP_CHECK:
				pr_info("enter detect HVDCP check\n");
				if(!attach) {
					pr_err("usb disconnect, goto next check\n", attach);
					atomic_set(&sgm->chg_type_check_cnt, 1);
					need_wait = false;
					sgm->detect_stage = DETECT_TYPE_DISCONNECT;
					goto next_loop;
				}
				now = ktime_get_boottime();
				check_time = ktime_sub(now, sgm->hvdcp_check_begin_time);
				if(ktime_to_ms(check_time) > HVDCP_DP_0P6V_HOLD_TIME_MS) {
					atomic_set(&sgm->chg_type_check_cnt, 0);
					ret = sgm4154x_hvdcp_postcheck(sgm);
					/*
					 * ret = 0: detect QC2.0 success
					 * ret = 1: vbus check fail, need retry
					 * ret < 0: i2c or iio fail
					 */
					if (ret == 0) {
						pr_info("hvdcp detect success\n");
						sgm4154x_hvdcp_type_changed(sgm);
						sgm->detect_stage = DETECT_TYPE_CHECK_DONE;
					} else if (ret == 1) {
						pr_info("hvdcp vbus check fail, retry:%d\n",
								sgm->hvdcp_detect_retry_count);
						if (sgm->hvdcp_detect_retry_count < HVDCP_HV_DETECT_RETRY) {
							sgm4154x_hvdcp_disconnect(sgm);
							need_wait = true;
							atomic_set(&sgm->chg_type_check_cnt, 1);
							sgm->detect_stage = DETECT_TYPE_HVDCP;
							sgm->hvdcp_detect_retry_count++;
						} else {
							sgm4154x_hvdcp_type_changed(sgm);
						}
					} else if (ret < 0) {
						pr_err("hvdcp postcheck i2c or iio fail\n");
						sgm4154x_hvdcp_type_changed(sgm);
						break;
					}
				} else {
					atomic_dec(&sgm->chg_type_check_cnt);
					need_wait = true;
				}
				break;

			case DETECT_TYPE_CHECK_DONE:
			default:
				pr_info("detect stage(%d).\n", sgm->detect_stage);
				atomic_set(&sgm->chg_type_check_cnt, 0);
				break;
		}

next_loop:
		mutex_unlock(&sgm->detect_lock);
		if(need_wait)
			msleep(BC12_CHECK_DEFAULT_DELAY_TIME_MS);
	}

	pr_info("exit\n");
	return 0;
}

static struct regulator_ops sgm4154x_vbus_ops = {
	.enable = sgm4154x_enable_vbus,
	.disable = sgm4154x_disable_vbus,
	.is_enabled = sgm4154x_is_enabled_vbus,
};

static const struct regulator_desc sgm4154x_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &sgm4154x_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int sgm4154x_vbus_regulator_register(struct sgm4154x_device *sgm)
{
	struct regulator_config config = {};
	int ret = 0;
	/* otg regulator */
	config.dev = sgm->dev;
	config.driver_data = sgm;
	sgm->otg_rdev = devm_regulator_register(sgm->dev,
						&sgm4154x_otg_rdesc, &config);
	sgm->otg_rdev->constraints->valid_ops_mask |= REGULATOR_CHANGE_STATUS;
	if (IS_ERR(sgm->otg_rdev)) {
		ret = PTR_ERR(sgm->otg_rdev);
		pr_err("register otg regulator failed (%d)\n", ret);
	}
	return ret;
}

static struct charger_ops sgm4154x_chg_ops = {
	.enable_hz = sgm4154x_enable_hz,

	/* Normal charging */
	.dump_registers = sgm4154x_dump_register,
	.enable = sgm4154x_charging_switch,
	.get_charging_current = sgm4154x_get_charging_current,
	.set_charging_current = sgm4154x_set_charging_current,
	.get_input_current = sgm4154x_get_input_current,
	.set_input_current = sgm4154x_set_input_current,
	.get_constant_voltage = sgm4154x_get_constant_voltage,
	.set_constant_voltage = sgm4154x_set_constant_voltage,
	.kick_wdt = sgm4154x_reset_watch_dog_timer,
	.set_mivr = sgm4154x_set_input_volt_lim,
	.get_mivr = sgm4154x_get_input_volt_lim,
	.is_charging_done = sgm4154x_get_charging_status,
	.plug_out = sgm4154x_plug_out,
	.is_enabled = sgm4154x_is_enabled,

	/* Safety timer */
	.enable_safety_timer = sgm4154x_enable_safetytimer,
	.is_safety_timer_enabled = sgm4154x_get_is_safetytimer_enable,


	/* Power path */
	.enable_powerpath = sgm4154x_enable_power_path,
	.is_powerpath_enabled = sgm4154x_is_powerpath_enabled,

	/* OTG */
	.enable_otg = sgm4154x_enable_otg,	
	.set_boost_current_limit = sgm4154x_set_boost_current_limit,
	.event = sgm4154x_do_event,

	/* charger type detection */
	.enable_chg_type_det = sgm4154x_enable_chg_type_det,
	.enable_hvdcp_det = sgm4154x_enable_hvdcp_type_detect,
	.ignore_chg_type_det = sgm4154x_ignore_chg_type_det,

	.enable_hvdcp_voltage = sgm4154x_enable_hvdcp_voltage,
	.get_vbus_adc = sgm4154x_get_vbus,
};

static void charger_change_work(struct work_struct *work) {
	struct delayed_work *delay_work = NULL;
	struct sgm4154x_device *sgm = NULL;
	u8 fault;
	u8 vbus_sts;
	u8 otg_sts;
	int vbus = 0;
	bool en_otg = false;
	bool prev_vbus_gd = false;

	delay_work = container_of(work, struct delayed_work, work);
	if(delay_work == NULL) {
		pr_err("Cann't get charger_change_work\n");
		return;
	}
	sgm = container_of(delay_work, struct sgm4154x_device, charger_change_work);
	if(sgm == NULL) {
		pr_err("Cann't get sgm \n");
		return;
	}

	sgm4154x_read_reg(sgm, SGM4154X_CHRG_FAULT, &fault);
	sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_1, &otg_sts);
	en_otg = !!(otg_sts & SGM4154X_OTG_EN);
	pr_info("%s: fault:0x%x, en otg:%d, hvdcp check:%d\n", __func__,
		fault, en_otg, sgm->detect_stage);

	if (en_otg) {
		pr_err("OTG mode, skip adapter/usb check\n");
		if (sgm->charger_wakelock)
			__pm_relax(sgm->charger_wakelock);
		return;
	}

	/* VBUS Insert check */
	sgm4154x_read_reg(sgm, SGM4154X_CHRG_CTRL_A, &vbus_sts);
	prev_vbus_gd = sgm->vbus_gd;
	sgm->vbus_gd = !!(vbus_sts & SGM4154X_VBUS_GOOD);
	sgm4154x_get_vbus_voltage(sgm, &vbus);
	if (!prev_vbus_gd && sgm->vbus_gd && (vbus >= 4400)) {
		pr_err("adapter/usb inserted\n");
		sgm4154x_notify_vbus_changed(1);
	} else if (prev_vbus_gd && !sgm->vbus_gd && (vbus < 4400)) {
		pr_err("adapter/usb removed\n");
		sgm4154x_notify_vbus_changed(0);
	}

	if (sgm->dev->parent)
		pm_runtime_put(sgm->dev->parent);
	__pm_relax(sgm->charger_wakelock);
}

static irqreturn_t sgm4154x_irq_handler_thread(int irq, void *private)
{
	struct sgm4154x_device *sgm = private;

	pr_info("%s entry\n",__func__);
	__pm_stay_awake(sgm->charger_wakelock);
	/* Runtime resume the i2c bus device from runtime suspend */
	if (sgm->dev->parent)
		pm_runtime_get(sgm->dev->parent);
	schedule_delayed_work(&sgm->charger_change_work, msecs_to_jiffies(50));

	return IRQ_HANDLED;
}

int sgm4154x_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct sgm4154x_device *sgm;

    char *name = NULL;
	
	pr_info("++ enter\n");

	sgm = devm_kzalloc(dev, sizeof(*sgm), GFP_KERNEL);
	if (!sgm)
		return -ENOMEM;

	sgm->client = client;
	sgm->dev = dev;
	
	mutex_init(&sgm->lock);
	mutex_init(&sgm->i2c_rw_lock);
	mutex_init(&sgm->detect_lock);
	atomic_set(&sgm->chg_type_check_cnt, 0);
	atomic_set(&sgm->vbus_check_cnt, 0);
	atomic_set(&sgm->input_check_cnt, 0);
	init_waitqueue_head(&sgm->waitq);
	INIT_DELAYED_WORK(&sgm->charger_change_work, charger_change_work);
	sgm->tcpc_attach = false;
	sgm->attach = false;
	sgm->chg_type = CHARGER_UNKNOWN;
	sgm->detect_stage = DETECT_TYPE_UNKNOW;

	i2c_set_clientdata(client, sgm);

	ret = sgm4154x_hw_chipid_detect(sgm);
	if (ret < 0){
		pr_err("device not found, retry !!!\n");
		ret = sgm4154x_hw_chipid_detect(sgm);
		if (ret < 0) {
			pr_err("device not found, stop !!!\n");
			return ret;
		}
	}

	ret = sgm4154x_parse_dt(sgm);
	if (ret < 0) {
		pr_err("dts parse error !!!\n");
		return ret;
	}

	ret = sgm4154x_hw_init(sgm);
	if (ret < 0) {
		pr_err("cannot initialize the chip.\n");
		return ret;
	}

	/* for otg regulator */
	g_sgm_device = sgm;

	name = devm_kasprintf(sgm->dev, GFP_KERNEL, "%s","sgm4154x suspend wakelock");
	sgm->charger_wakelock =	wakeup_source_register(NULL, name);

	sgm->detect_task = kthread_run(sgm4154x_detect_task_threadfn,
			sgm, "sgm4154x_thread");
	ret = PTR_ERR_OR_ZERO(sgm->detect_task);
	if (ret < 0){
		pr_err("create detect thread fail.\n");
		goto fail_free_gpio;
	}

	/* Register interrupt */
	if (client->irq) {
		pr_info("register irq handler.\n");
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sgm4154x_irq_handler_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"sgm4154x irq", sgm);
		if (ret) {
			pr_err("request irq for irq=%d fail.\n", client->irq);
			goto fail_free_gpio;
		}

		enable_irq_wake(client->irq);
	}

	/* Register regulator */
	if (sgm->enable_otg_regulator) {
		ret = sgm4154x_vbus_regulator_register(sgm);
		if (ret < 0) {
			pr_err("register vbus regulator failed\n");
			goto fail_free_gpio;
		}
	}

	/* Register charger device */
	sgm->chg_dev = charger_device_register(sgm->chg_dev_name,
						&client->dev, sgm,
						&sgm4154x_chg_ops,
						&sgm4154x_chg_props);
	if (IS_ERR_OR_NULL(sgm->chg_dev)) {
		pr_err("register charger device failed\n");
		ret = PTR_ERR(sgm->chg_dev);
		goto fail_free_gpio;
	}

	/* init check for vbus */
	sgm4154x_irq_handler_thread(-1, sgm);
	pr_info("probe success.\n");

	return ret;

fail_free_gpio:
	if (sgm->irq_gpio)
		gpio_free(sgm->irq_gpio);

	g_sgm_device = NULL;

	pr_err("probe fail\n");

	return ret;
}
EXPORT_SYMBOL_GPL(sgm4154x_driver_probe);

int sgm4154x_charger_remove(struct i2c_client *client)
{
    struct sgm4154x_device *sgm = i2c_get_clientdata(client);

    regulator_unregister(sgm->otg_rdev);
	
	mutex_destroy(&sgm->lock);
    mutex_destroy(&sgm->i2c_rw_lock);
	mutex_destroy(&sgm->detect_lock);
	wakeup_source_unregister(sgm->charger_wakelock);
	cancel_delayed_work_sync(&sgm->charger_change_work);

    return 0;
}
EXPORT_SYMBOL_GPL(sgm4154x_charger_remove);

void sgm4154x_charger_shutdown(struct i2c_client *client)
{
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);
	int ret = 0;

	pr_err("sgm4154x_charger_shutdown\n");
	if (client->irq)
		disable_irq(client->irq);
	cancel_delayed_work_sync(&sgm->charger_change_work);

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_1,
			SGM4154X_OTG_EN, 0);
	if (ret)
		pr_err("reset otg fail\n");

	ret = sgm4154x_update_bits(sgm, SGM4154X_CHRG_CTRL_D,
			SGM4154X_DP_VSEL_MASK | SGM4154X_DM_VSEL_MASK, 0);
	if (ret)
		pr_err("update dpdm to hiz fail\n");

	ret = sgm4154x_set_vindpm_offset_os(sgm, 0);
	if (ret)
		pr_err("Failed to set vindpm_os, ret = %d\n", ret);

	ret = sgm4154x_enable_watchdog_reset(sgm);
	if (ret)
		pr_err("Failed to disable charger, ret = %d\n", ret);
}
EXPORT_SYMBOL_GPL(sgm4154x_charger_shutdown);
