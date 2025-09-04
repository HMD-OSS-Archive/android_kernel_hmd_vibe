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

#define pr_fmt(fmt)	"[cne-charge][bq25601] %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
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
#include <linux/pinctrl/consumer.h>
#include "bq25601.h"
#include "hl7019d.h"
#include "charger_class.h"

/**********************************************************
 *
 *   [I2C Slave Setting]
 *
 *********************************************************/
struct init_data {
	u32 ichg;	/* charge current		*/
	u32 ilim;	/* input current		*/
	u32 vreg;	/* regulation voltage		*/
	u32 iterm;	/* termination current		*/
	u32 iprechg;	/* precharge current		*/
	u32 vlim;	/* minimum system voltage limit */
	u32 max_ichg;
	u32 max_vreg;
};

enum vendor_type {
	VENDOR_ID_UNKNOW,
	VENDOR_ID_CX25601,
	VENDOR_ID_SGM41513,
	VENDOR_ID_HL7019D,
};

struct bq25601_info {
	struct i2c_client *client;
	struct charger_device *chg_dev;
	struct charger_properties chg_props;
	struct device *dev;
	const char *chg_dev_name;
	const char *eint_name;
	struct mutex i2c_rw_lock;
	struct init_data init_data;
	enum vendor_type vendor_type;
	/* pinctrl parameters */
	const char		*pinctrl_state_name;
	struct pinctrl		*irq_pinctrl;
	int chg_en_gpio;
	int irq;
};


/**********************************************************
 *
 *   [Global Variable]
 *
 *********************************************************/

static struct bq25601_info  *g_chg_dev = NULL;

static bool is_user_enable_hiz_mode = false;

static const struct i2c_device_id bq25601_i2c_id[] = { {"bq25601", 0}, {} };

/**********************************************************
 *
 *   [Read / Write Function]
 *
 *********************************************************/
static int __bq25601_read_byte(struct bq25601_info *info, u8 reg, u8 *data)
{
	int retry_count = I2C_RW_RETRY_COUNT;
    s32 ret;

	while (retry_count > 0) {
		ret = i2c_smbus_read_byte_data(info->client, reg);
		if (ret >= 0)
			break;
		retry_count--;
	}
	if (ret < 0) {
        pr_err("can't read from reg 0x%02X\n", reg);
        return ret;
	}

	*data = (u8)ret;

    return 0;
}

static int __bq25601_write_byte(struct bq25601_info *info, int reg, u8 val)
{
	int retry_count = I2C_RW_RETRY_COUNT;
    s32 ret;

	while (retry_count > 0) {
		ret = i2c_smbus_write_byte_data(info->client, reg, val);
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

static int bq25601_read_reg(struct bq25601_info *info, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&info->i2c_rw_lock);
	ret = __bq25601_read_byte(info, reg, data);
	mutex_unlock(&info->i2c_rw_lock);

	return ret;
}

#if 0
static int bq25601_update_bits(struct bq25601_info *info, u8 reg,
					u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&info->i2c_rw_lock);
	ret = __bq25601_read_byte(info, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __bq25601_write_byte(info, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&info->i2c_rw_lock);
	return ret;
}
#endif

int bq25601_read_interface(unsigned char RegNum,
				    unsigned char *val, unsigned char MASK,
				    unsigned char SHIFT)
{
	struct bq25601_info *info = g_chg_dev;
	unsigned char reg = 0;
	int ret = 0;

	if(!info) {
		pr_err("chg_dev is null, return\n");
		return -ENODEV;
	}

	ret = bq25601_read_reg(info, RegNum, &reg);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	reg &= (MASK << SHIFT);
	*val = (reg >> SHIFT);

out:
	return ret;
}

static int bq25601_write_byte(struct bq25601_info *info, u8 reg, u8 data)
{
	int ret = 0;

	mutex_lock(&info->i2c_rw_lock);
	ret = __bq25601_write_byte(info, reg, data);
	mutex_unlock(&info->i2c_rw_lock);

	return ret;
}

unsigned int bq25601_config_interface(unsigned char RegNum,
					unsigned char val, unsigned char MASK,
					unsigned char SHIFT)
{
	struct bq25601_info *info = g_chg_dev;
	unsigned char reg = 0;
	unsigned char reg_ori = 0;
	int ret = 0;

	if(!info) {
		pr_err("chg_dev is null, return\n");
		return -ENODEV;
	}

	mutex_lock(&info->i2c_rw_lock);
	ret = __bq25601_read_byte(info, RegNum, &reg);
	if (ret) {
		pr_err("Failed: reg=%02X read fail, ret=%d\n", reg, ret);
		goto out;
	}

	reg_ori = reg;
	reg &= ~(MASK << SHIFT);
	reg |= (val << SHIFT);

	ret = __bq25601_write_byte(info, RegNum, reg);
	if (ret) {
		pr_err("Failed: reg=%02X write fail, ret=%d\n", reg, ret);
		goto out;
	}
out:
	mutex_unlock(&info->i2c_rw_lock);
	return ret;
}

/* write one register directly */
#if 0
static unsigned int bq25601_reg_config_interface(unsigned char RegNum,
		unsigned char val)
{
	struct bq25601_info *info = g_chg_dev;
	int ret;

	if(!info) {
		pr_err("%s: chg_dev is null, return\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&info->i2c_rw_lock);
	ret = __bq25601_write_byte(info, RegNum, val);
	mutex_unlock(&info->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", RegNum, ret);

	return ret;
}
#endif
/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
/* CON0---------------------------------------------------- */
void bq25601_set_en_hiz(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON0),
				       (unsigned char) (val),
				       (unsigned char) (CON0_EN_HIZ_MASK),
				       (unsigned char) (CON0_EN_HIZ_SHIFT)
				      );
}

void bq25601_set_iinlim(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON0),
				       (unsigned char) (val),
				       (unsigned char) (CON0_IINLIM_MASK),
				       (unsigned char) (CON0_IINLIM_SHIFT)
				      );
}

void bq25601_set_stat_ctrl(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON0),
				   (unsigned char) (val),
				   (unsigned char) (CON0_STAT_IMON_CTRL_MASK),
				   (unsigned char) (CON0_STAT_IMON_CTRL_SHIFT)
				   );
}

/* CON1---------------------------------------------------- */

void bq25601_set_reg_rst(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON11),
				       (unsigned char) (val),
				       (unsigned char) (CON11_REG_RST_MASK),
				       (unsigned char) (CON11_REG_RST_SHIFT)
				      );
	//disable safety timer
	ret = bq25601_config_interface((unsigned char) (BQ25601_CON5),
					(unsigned char) (0),
					(unsigned char) (CON5_EN_TIMER_MASK),
					(unsigned char) (CON5_EN_TIMER_SHIFT)
				      );
}

void bq25601_set_pfm(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON1),
			       (unsigned char) (val),
			       (unsigned char) (CON1_PFM_MASK),
			       (unsigned char) (CON1_PFM_SHIFT)
			      );

}

void bq25601_set_wdt_rst(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_WDT_RST_MASK),
				       (unsigned char) (CON1_WDT_RST_SHIFT)
				      );
}

void bq25601_set_otg_config(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_OTG_CONFIG_MASK),
				       (unsigned char) (CON1_OTG_CONFIG_SHIFT)
				      );
}

unsigned int bq25601_get_otg_config(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25601_read_interface((unsigned char) (BQ25601_CON1),
				     (&val),
				     (unsigned char) (CON1_OTG_CONFIG_MASK),
				     (unsigned char) (CON1_OTG_CONFIG_SHIFT)
				    );
	return val;
}

void bq25601_set_chg_config(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_CHG_CONFIG_MASK),
				       (unsigned char) (CON1_CHG_CONFIG_SHIFT)
				      );
}


void bq25601_set_sys_min(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_SYS_MIN_MASK),
				       (unsigned char) (CON1_SYS_MIN_SHIFT)
				      );
}

void bq25601_set_batlowv(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_MIN_VBAT_SEL_MASK),
				       (unsigned char) (CON1_MIN_VBAT_SEL_SHIFT)
				      );
}



/* CON2---------------------------------------------------- */
void bq25601_set_rdson(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_Q1_FULLON_MASK),
				       (unsigned char) (CON2_Q1_FULLON_SHIFT)
				      );
}

void bq25601_set_boost_lim(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_BOOST_LIM_MASK),
				       (unsigned char) (CON2_BOOST_LIM_SHIFT)
				      );
}

void bq25601_set_ichg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_ICHG_MASK),
				       (unsigned char) (CON2_ICHG_SHIFT)
				      );
}

/* CON3---------------------------------------------------- */

void bq25601_set_iprechg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON3),
				       (unsigned char) (val),
				       (unsigned char) (CON3_IPRECHG_MASK),
				       (unsigned char) (CON3_IPRECHG_SHIFT)
				      );
}

void bq25601_set_iterm(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON3),
				       (unsigned char) (val),
				       (unsigned char) (CON3_ITERM_MASK),
				       (unsigned char) (CON3_ITERM_SHIFT)
				      );
}

/* CON4---------------------------------------------------- */

void bq25601_set_vreg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_VREG_MASK),
				       (unsigned char) (CON4_VREG_SHIFT)
				      );
}

void bq25601_set_topoff_timer(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_TOPOFF_TIMER_MASK),
				       (unsigned char) (CON4_TOPOFF_TIMER_SHIFT)
				      );

}


void bq25601_set_vrechg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_VRECHG_MASK),
				       (unsigned char) (CON4_VRECHG_SHIFT)
				      );
}

/* CON5---------------------------------------------------- */

void bq25601_set_en_term(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_EN_TERM_MASK),
				       (unsigned char) (CON5_EN_TERM_SHIFT)
				      );
}



void bq25601_set_watchdog(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_WATCHDOG_MASK),
				       (unsigned char) (CON5_WATCHDOG_SHIFT)
				      );
}

void bq25601_set_en_timer(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_EN_TIMER_MASK),
				       (unsigned char) (CON5_EN_TIMER_SHIFT)
				      );
}

void bq25601_set_chg_timer(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON5),
				       (unsigned char) (val),
				       (unsigned char) (CON5_CHG_TIMER_MASK),
				       (unsigned char) (CON5_CHG_TIMER_SHIFT)
				      );
}

/* CON6---------------------------------------------------- */

void bq25601_set_treg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_BOOSTV_MASK),
				       (unsigned char) (CON6_BOOSTV_SHIFT)
				      );
}

void bq25601_set_vindpm(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_VINDPM_MASK),
				       (unsigned char) (CON6_VINDPM_SHIFT)
				      );
}


void bq25601_set_ovp(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_OVP_MASK),
				       (unsigned char) (CON6_OVP_SHIFT)
				      );

}

void bq25601_set_boostv(unsigned int val)
{

	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_BOOSTV_MASK),
				       (unsigned char) (CON6_BOOSTV_SHIFT)
				      );
}



/* CON7---------------------------------------------------- */

void bq25601_set_tmr2x_en(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON7),
					(unsigned char) (val),
					(unsigned char) (CON7_TMR2X_EN_MASK),
					(unsigned char) (CON7_TMR2X_EN_SHIFT)
					);
}

void bq25601_set_batfet_disable(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON7),
				(unsigned char) (val),
				(unsigned char) (CON7_BATFET_DISABLE_MASK),
				(unsigned char) (CON7_BATFET_DISABLE_SHIFT)
				);
}


void bq25601_set_batfet_delay(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_BATFET_DLY_MASK),
				       (unsigned char) (CON7_BATFET_DLY_SHIFT)
				      );
}

void bq25601_set_batfet_reset_enable(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON7),
				(unsigned char) (val),
				(unsigned char) (CON7_BATFET_RST_EN_MASK),
				(unsigned char) (CON7_BATFET_RST_EN_SHIFT)
				);
}

void bq25601_set_vdpm_bat_track(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON7),
				(unsigned char) (val),
				(unsigned char) (CON7_VDPM_BAT_TRACK_MASK),
				(unsigned char) (CON7_VDPM_BAT_TRACK_SHIFT)
				);
}

/* CON8---------------------------------------------------- */

unsigned int bq25601_get_system_status(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25601_read_interface((unsigned char) (BQ25601_CON8),
				     (&val), (unsigned char) (0xFF),
				     (unsigned char) (0x0)
				    );
	return val;
}

unsigned int bq25601_get_vbus_stat(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25601_read_interface((unsigned char) (BQ25601_CON8),
				     (&val),
				     (unsigned char) (CON8_VBUS_STAT_MASK),
				     (unsigned char) (CON8_VBUS_STAT_SHIFT)
				    );
	return val;
}

unsigned int bq25601_get_chrg_stat(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25601_read_interface((unsigned char) (BQ25601_CON8),
				     (&val),
				     (unsigned char) (CON8_CHRG_STAT_MASK),
				     (unsigned char) (CON8_CHRG_STAT_SHIFT)
				    );
	return val;
}

unsigned int bq25601_get_vsys_stat(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25601_read_interface((unsigned char) (BQ25601_CON8),
				     (&val),
				     (unsigned char) (CON8_VSYS_STAT_MASK),
				     (unsigned char) (CON8_VSYS_STAT_SHIFT)
				    );
	return val;
}

unsigned int bq25601_get_pg_stat(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25601_read_interface((unsigned char) (BQ25601_CON8),
				     (&val),
				     (unsigned char) (CON8_PG_STAT_MASK),
				     (unsigned char) (CON8_PG_STAT_SHIFT)
				    );
	return val;
}

/*CON10----------------------------------------------------------*/

void bq25601_set_int_mask(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25601_config_interface((unsigned char) (BQ25601_CON10),
				       (unsigned char) (val),
				       (unsigned char) (CON10_INT_MASK_MASK),
				       (unsigned char) (CON10_INT_MASK_SHIFT)
				      );
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
/* title format: REG00 REG01 ... */
static int bq25601_dump_register(struct charger_device *chg_dev)
{
	struct bq25601_info *info = g_chg_dev;
	unsigned char reg[SGM41513_REG_NUM] = { 0 };
	unsigned char d_title[6 * SGM41513_REG_NUM + 1] = { 0 };
	unsigned char d_value[6 * SGM41513_REG_NUM + 1] = { 0 };
	int REG_COUNT = BQ25601_REG_NUM;
	int i = 0;
	int ret = 0;

	if(!info) {
		pr_err("chg_dev is null, return\n");
		return -ENODEV;
	}

	if (info->vendor_type == VENDOR_ID_SGM41513)
		REG_COUNT = SGM41513_REG_NUM;
	else if (info->vendor_type == VENDOR_ID_HL7019D)
		REG_COUNT = HL7019D_REG_NUM;

	memset(d_title, 0, ARRAY_SIZE(d_title));
	memset(d_value, 0, ARRAY_SIZE(d_value));
	for (i = 0; i < REG_COUNT; i++) {
		ret = bq25601_read_reg(info, i, &reg[i]);
		if (ret < 0) {
			pr_info("[bq25601] i2c transfor error, errno=%d\n", ret);
			break;
		}

		sprintf(&d_title[6 * i], "reg%02x ", i);
		sprintf(&d_value[6 * i], "0x%02x  ", reg[i]);
	}

	return ret;
}


/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int bq25601_hw_chipid_detect(struct bq25601_info *info)
{
	struct i2c_client *client = info->client;
	unsigned short i2c_addr = client->addr;
	int ret = 0;
	u8 val = 0, reg = 0;

	pr_info("i2c addr:0x%x\n", i2c_addr);

	if (i2c_addr == CHIP_ADDR_HL7019D) {
		/* HL7019D VID register is CON0A */
		ret = bq25601_read_reg(info, HL7019D_CONA, &reg);
		if (ret < 0) {
			pr_info("read chip id fail\n");
			return ret;
		}

		val = (reg >> HL7019D_PN_SHIFT) & HL7019D_PN_MASK;
		pr_info("HL7019D addr, vendor id is reg0B:0x%x, pn:0x%x", reg, val);
		if (val == CHIP_ID_HL7019D)
			info->vendor_type = VENDOR_ID_HL7019D;
		else {
			pr_err("vendor id is reg0A:0x%x, pn:0x%x", reg, val);
			info->vendor_type = VENDOR_ID_UNKNOW;
			return -ENODEV;
		}
	} else if (i2c_addr == CHIP_ADDR_SGM41513) {
		/* SGM41513 VID register is CON0A */
		ret = bq25601_read_reg(info, BQ25601_CON11, &reg);
		if (ret < 0) {
			pr_info("read chip id fail\n");
			return ret;
		}

		val = (reg >> CON11_PN_SHIFT) & CON11_PN_MASK;
		pr_info("SGM41513 addr, vendor id is reg0B:0x%x, pn:0x%x", reg, val);
		if (val == CHIP_ID_SGM41513 || val == CHIP_ID_SGM41513A_D)
			info->vendor_type = VENDOR_ID_SGM41513;
		else {
			pr_err("vendor id is reg0B:0x%x, pn:0x%x", reg, val);
			info->vendor_type = VENDOR_ID_UNKNOW;
			return -ENODEV;
		}
	} else {
		/* CX25601 VID register is CON0B
		 * HL7019 VID register is CON0A
		 */
		ret = bq25601_read_reg(info, BQ25601_CON11, &reg);
		if (ret < 0) {
			pr_info("read chip id fail\n");
			return ret;
		}

		val = (reg >> CON11_PN_SHIFT) & CON11_PN_MASK;
		pr_info("CX25601/HL7019 addr, vendor id is reg0B:0x%x, pn:0x%x", reg, val);
		if (val == CHIP_ID_BQ25601 || val == CHIP_ID_BQ25601D)
			info->vendor_type = VENDOR_ID_CX25601;
		else if (val == CHIP_ID_SGM41513 || val == CHIP_ID_SGM41513A_D)
			info->vendor_type = VENDOR_ID_SGM41513;
		else {
			pr_info("reg0B:0x%x is mismatch, read reg0A...\n", reg);
			ret = bq25601_read_reg(info, HL7019D_CONA, &reg);
			if (ret < 0) {
				pr_info("read chip id fail\n");
				return ret;
			}

			val = (reg >> HL7019D_PN_SHIFT) & HL7019D_PN_MASK;
			if (val == CHIP_ID_HL7019D)
				info->vendor_type = VENDOR_ID_HL7019D;
			else {
				pr_err("vendor id is reg0A:0x%x, pn:0x%x", reg, val);
				info->vendor_type = VENDOR_ID_UNKNOW;
				return -ENODEV;
			}
		}
	}

	pr_info("VID reg:0x%x, chg pn:0x%x\n", reg, val);

	return 0;
}


static int bq25601_enable_charging(struct charger_device *chg_dev,
				   bool en)
{
	struct bq25601_info *info = g_chg_dev;
	unsigned int val = en ? 1 : 0;
	int status = 0;

	pr_info("enable state : %d\n", en);
	if (info->vendor_type == VENDOR_ID_HL7019D) {
		if (en && !is_user_enable_hiz_mode) {
			pr_info("charging enable, clear hl7019 hiz mode\n");
			hl7019d_set_en_hiz(0);
		}
		status = hl7019d_enable_charging(chg_dev, en);
	} else
		bq25601_set_chg_config(val);

	return status;
}

static int bq25601_set_hiz_mode(struct charger_device *dev, bool hiz_en)
{
	struct bq25601_info *info = g_chg_dev;
	unsigned int val = hiz_en ? 1 : 0;

	pr_info("hiz_en:%d\n", hiz_en);
	is_user_enable_hiz_mode = hiz_en;

	if (info->vendor_type == VENDOR_ID_HL7019D)
		hl7019d_set_en_hiz(val);
	else
		bq25601_set_en_hiz(val);
	return 0;
}

static int bq25601_get_current(struct charger_device *chg_dev, unsigned int *uA)
{
	struct bq25601_info *info = g_chg_dev;
	int ret;
	u8 reg_val;
	u8 ichrg = 0;

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_get_current(chg_dev, uA);
	} else {
		ret = bq25601_read_reg(info, BQ25601_CON2, &reg_val);
		if (ret) {
			pr_err("read reg fail\n");
			return ret;
		}

		if (info->vendor_type == VENDOR_ID_SGM41513) {
			ichrg = (reg_val & BQ25601_ICHRG_I_MASK);
			if (ichrg <= 0x8)
				*uA = ichrg * 5000;
			else if (ichrg <= 0xf)
				*uA = (ichrg - 0x8) * 10000;
			else if (ichrg <= 0xf)
				*uA = (ichrg - 0x8) * 10000 + 40000;
			else if (ichrg <= 0x17)
				*uA = (ichrg - 0xf) * 20000 + 110000;
			else if (ichrg <= 0x20)
				*uA = (ichrg - 0x17) * 30000 + 270000;
			else if (ichrg <= 0x30)
				*uA = (ichrg - 0x20) * 60000 + 540000;
			else if (ichrg <= 0x3c)
				*uA = (ichrg - 0x30) * 120000 + 1500000;
			else
				*uA = 3000000;
		} else {
			*uA = (reg_val & BQ25601_ICHRG_I_MASK) * BQ25601_ICHRG_I_STEP_uA;
			if (*uA > BQ25601_ICHRG_I_MAX_uA)
				*uA = BQ25601_ICHRG_I_MAX_uA;
		}
	}

	pr_info("uA:%d\n", *uA);

	return ret;
}

static int bq25601_set_current(struct charger_device *chg_dev,
			       u32 uA)
{
	struct bq25601_info *info = g_chg_dev;
	int ret = 0;
	u8 reg_val;

	pr_info("uA:%d\n", uA);
	if(!info) {
		pr_err("chg_dev is null, return\n");
		return -ENODEV;
	}

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_set_current(chg_dev, uA);
	} else {
		if (uA < BQ25601_ICHRG_I_MIN_uA)
			uA = BQ25601_ICHRG_I_MIN_uA;
		else if (uA > BQ25601_ICHRG_I_MAX_uA)
			uA = BQ25601_ICHRG_I_MAX_uA;

		if (uA > info->init_data.max_ichg)
			uA = info->init_data.max_ichg;

		if (info->vendor_type == VENDOR_ID_SGM41513) {
			if (uA < 40000)
				reg_val = uA / 5000;
			else if (uA <= 110000)
				reg_val = ((uA - 40000) / 10000) + 0x8;
			else if (uA <= 270000)
				reg_val = ((uA - 110000) / 20000) + 0xf;
			else if (uA <= 540000)
				reg_val = ((uA - 270000) / 30000) + 0x17;
			else if (uA <= 1500000)
				reg_val = ((uA - 540000) / 60000) + 0x20;
			else if (uA < 3000000)
				reg_val = ((uA - 1500000) / 120000) + 0x30;
			else
				reg_val = 0x3d;
		} else
			reg_val = uA / BQ25601_ICHRG_I_STEP_uA;

		bq25601_set_ichg(reg_val);
	}

	return ret;
}

static int bq25601_set_input_current(struct charger_device *chg_dev,
				     u32 iindpm)
{
	struct bq25601_info *info = g_chg_dev;
	int ret = 0;
	u8 reg_val;

	pr_info("iindpm:%d\n", iindpm);
	if(!info) {
		pr_err("chg_dev is null, return\n");
		return -ENODEV;
	}

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_set_input_current(chg_dev, iindpm);
	} else {
		if (iindpm < BQ25601_IINDPM_I_MIN_uA)
			iindpm = BQ25601_IINDPM_I_MIN_uA;
		else if (iindpm > BQ25601_IINDPM_I_MAX_uA)
			iindpm = BQ25601_IINDPM_I_MAX_uA;

		if (iindpm >= BQ25601_IINDPM_I_MIN_uA && iindpm <= 3100000)
			reg_val = (iindpm-BQ25601_IINDPM_I_MIN_uA) / BQ25601_IINDPM_STEP_uA;
		else if (iindpm > 3100000 && iindpm < BQ25601_IINDPM_I_MAX_uA)
			reg_val = 0x1E;
		else
			reg_val = BQ25601_IINDPM_I_MASK;

		bq25601_set_iinlim(reg_val);
	}

	return ret;
}

static int bq25601_get_input_current(struct charger_device *chg_dev, u32 *uA)
{
	struct bq25601_info *info = g_chg_dev;
	int ret = 0;
	u8 reg_val;

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_get_input_current(chg_dev, uA);
	} else {
		ret = bq25601_read_interface(BQ25601_CON0, &reg_val,
				CON0_IINLIM_MASK, CON0_IINLIM_SHIFT);
		if (ret < 0) {
			pr_err("read REG00 fail\n");
			return ret;
		}

		*uA = reg_val * BQ25601_IINDPM_STEP_uA + BQ25601_IINDPM_I_MIN_uA;
	}

	pr_info("uA:%d\n", *uA);

	return ret;
}

static int bq25601_set_cv_voltage(struct charger_device *chg_dev,
				  u32 cv)
{
	struct bq25601_info *info = g_chg_dev;
	int ret = 0;
	u8 reg_val;

	pr_info("cv:%d\n", cv);
	if(!info) {
		pr_err("chg_dev is null, return\n");
		return -ENODEV;
	}

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_set_cv_voltage(chg_dev, cv);
	} else {
		if (cv < BQ25601_VREG_V_MIN_uV)
			cv = BQ25601_VREG_V_MIN_uV;
		else if (cv > BQ25601_VREG_V_MAX_uV)
			cv = BQ25601_VREG_V_MAX_uV;

		if (cv > info->init_data.max_vreg)
			cv = info->init_data.max_vreg;

		reg_val = (cv - BQ25601_VREG_V_MIN_uV) / BQ25601_VREG_V_STEP_uV;
		bq25601_set_vreg(reg_val);
	}

	return ret;
}

static int bq25601_reset_watch_dog_timer(struct charger_device
		*chg_dev)
{
	struct bq25601_info *info = g_chg_dev;
	int ret = 0;

	pr_info("charging_reset_watch_dog_timer\n");

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_reset_watch_dog_timer(chg_dev);
	} else {
		bq25601_set_wdt_rst(0x1);	/* Kick watchdog */
		bq25601_set_watchdog(0x1);	/* WDT 40s */
	}

	return ret;
}


static int bq25601_set_vindpm_voltage(struct charger_device *chg_dev,
				      u32 vindpm)
{
	struct bq25601_info *info = g_chg_dev;
	int ret = 0;
	u8 os_val, reg_val;
	unsigned int offset;

	pr_info("cv:%d\n", vindpm);
	if(!info) {
		pr_err("chg_dev is null, return\n");
		return -ENODEV;
	}

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_set_vindpm_voltage(chg_dev, vindpm);
	} else if (info->vendor_type == VENDOR_ID_SGM41513) {
		if (vindpm < BQ25601_VINDPM_V_MIN_uV ||
		    vindpm > SGM41513_VINDPM_V_MAX_uV)
			return -EINVAL;

		if (vindpm < 5900000) {
			os_val = 0;
			offset = 3900000;
		} else if (vindpm >= 5900000 && vindpm < 7500000) {
			os_val = 1;
			offset = 5900000; //uv
		} else if (vindpm >= 7500000 && vindpm < 10500000) {
			os_val = 2;
			offset = 7500000; //uv
		} else {
			os_val = 3;
			offset = 10500000; //uv
		}

		bq25601_config_interface(SGM41513_CON15, os_val,
				CON15_SGM41513_OS_MASK, CON15_SGM41513_OS_SHIFT);

		reg_val = (vindpm - offset) / BQ25601_VINDPM_STEP_uV;
		bq25601_set_vindpm(reg_val);

	} else {
		if (vindpm < BQ25601_VINDPM_V_MIN_uV)
			vindpm = BQ25601_VINDPM_V_MIN_uV;
		else if (vindpm > BQ25601_VINDPM_V_MAX_uV)
			vindpm = BQ25601_VINDPM_V_MAX_uV;

		reg_val = (vindpm - BQ25601_VINDPM_V_MIN_uV) / BQ25601_VINDPM_STEP_uV;
		bq25601_set_vindpm(reg_val);
	}

	return ret;

}

static int bq25601_get_vindpm_voltage(struct charger_device *chg_dev,
				      u32 *uV)
{
	struct bq25601_info *info = g_chg_dev;
	u8 reg_os, reg_dpm;
	int os = 0, vindpm = 0;
	int base_vindpm = 3900;
	int ret = 0;

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_get_vindpm_voltage(chg_dev, uV);
	} else {
		ret = bq25601_read_reg(info, BQ25601_CON6, &reg_dpm);
		if (ret < 0) {
			pr_err("read 0x06 reg fail\n");
			return ret;
		}

		if (info->vendor_type == VENDOR_ID_SGM41513) {
			ret = bq25601_read_reg(info, SGM41513_CON15, &reg_os);
			if (ret < 0) {
				pr_err("read 0x0F reg fail\n");
				return ret;
			}

			os = (reg_os & CON15_SGM41513_OS_MASK) >> CON15_SGM41513_OS_SHIFT;
			if (os == 0)
				base_vindpm = 3900;
			else if (os == 1)
				base_vindpm = 5900;
			else if (os == 2)
				base_vindpm = 7500;
			else if (os == 3)
				base_vindpm = 10500;
		}

		vindpm = (reg_dpm & CON6_VINDPM_MASK) >> CON6_VINDPM_SHIFT;
		*uV = (base_vindpm + vindpm * 100) * 1000;
	}

	pr_info("get vindpm:%d\n", *uV);

	return ret;
}

static int bq25601_enable_safetytimer(struct charger_device *chg_dev,
				      bool en)
{
	struct bq25601_info *info = g_chg_dev;
	int status = 0;

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		status = hl7019d_enable_safety_timer(chg_dev, en);
	} else {
		if (en)
			bq25601_set_en_timer(0x1);
		else
			bq25601_set_en_timer(0x0);
	}

	return status;
}

static int bq25601_get_is_safetytimer_enable(struct charger_device
		*chg_dev, bool *en)
{
	struct bq25601_info *info = g_chg_dev;
	unsigned char val = 0;
	int ret = 0;

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_is_safety_timer_enabled(chg_dev, en);
	} else {
		bq25601_read_interface(BQ25601_CON5, &val, CON5_EN_TIMER_MASK,
			       CON5_EN_TIMER_SHIFT);
		*en = (bool)val;
	}

	return ret;
}

static int bq25601_init(struct charger_device *dev)
{
	struct bq25601_info *info = g_chg_dev;
	int ret = 0;

	pr_info("++\n");

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		hl7019d_charger_ic_init(dev);
	} else {
		bq25601_set_ovp(0x2);		/* OVP 10.5V */
		bq25601_set_vindpm(0x8);	/* VIN DPM check 4.7V */
		bq25601_set_wdt_rst(0x1);	/* Kick watchdog */
		bq25601_set_sys_min(0x5);	/* Minimum system voltage 3.5V */
		if (info->vendor_type == VENDOR_ID_SGM41513) {
			bq25601_set_iprechg(0xf); /* Precharge max current 240mA */
			bq25601_set_iterm(0xf);	/* Termination current 240mA */
		} else {
			bq25601_set_iprechg(0x8); /* Precharge current 540mA */
			bq25601_set_iterm(0x3);	/* Termination current 240mA */
		}
		bq25601_set_vreg(0x12);		/* VREG 4.45V */
		bq25601_set_pfm(0x0);       /* enable pfm */
		bq25601_set_rdson(0x0);     /* close rdson */
		bq25601_set_batlowv(0x0);	/* BATLOWV 2.8V */
		bq25601_set_vrechg(0x0);	/* VRECHG 0.1V (CV-100mV) */
		bq25601_set_en_term(0x1);	/* Enable termination */
		bq25601_set_watchdog(0x0);	/* WDT disable */
		bq25601_set_en_timer(0x0);	/* Enable charge timer */
		bq25601_set_int_mask(0x3);	/* Disable vindpm/iindpm interrupt */
		bq25601_set_ichg(0x0);		/* charge current set to min */
		bq25601_set_iinlim(0x0);	/* input current set to min */
	}
	return ret;
}

static int cx2560x_init_charge(struct bq25601_info *info)
{
	int ret;
	u8 data;

	ret = bq25601_write_byte(info, 0x40, 0x50);
	ret = bq25601_write_byte(info, 0x40, 0x57);
	ret = bq25601_write_byte(info, 0x40, 0x44);
	ret = bq25601_write_byte(info, 0x41, 0x04);
	ret = bq25601_read_reg(info, 0x41, &data);
	if (data != 0x04)
		pr_err("Failed to trim cx2560x: reg=%02X, data=%02X\n", 0x41, data);
	else
		pr_info("Trim cx2560x OK\n");

	ret = bq25601_write_byte(info, 0x40, 0x00);

	return ret;
}

static int bq25601_hw_init(struct bq25601_info *info)
{
	int ret = 0;

	/* enter and exit hiz, disable ic automatically enter hiz */
	if (info->vendor_type == VENDOR_ID_HL7019D)
		hl7019d_set_en_hiz(1);

	bq25601_set_en_hiz(0x0);	/* disable hiz mode */
	bq25601_init(info->chg_dev);

	if (info->vendor_type == VENDOR_ID_CX25601)
		cx2560x_init_charge(info);

	pr_info("hw_init done\n");
	return ret;
}

static int bq25601_plug_out(struct charger_device *chg_dev)
{
	pr_info("++\n");

	return bq25601_enable_charging(chg_dev, false);
}

static int bq25601_plug_in(struct charger_device *chg_dev)
{
	struct bq25601_info *info = g_chg_dev;

	pr_info("++\n");

	if (info->vendor_type == VENDOR_ID_HL7019D && !is_user_enable_hiz_mode) {
		pr_info("plugin, clear hl7019 hiz mode\n");
		hl7019d_set_en_hiz(0);
	}

	bq25601_enable_charging(chg_dev, false);

	return 0;
}

static int bq25601_is_enabled(struct charger_device *chg_dev, bool *en)
{
	struct bq25601_info *info = g_chg_dev;
	unsigned int state = 0;
	int ret = 0;

	if (info->vendor_type == VENDOR_ID_HL7019D) {
		ret = hl7019d_is_enabled(chg_dev, en);
	} else {
		*en = false;

		state = bq25601_get_chrg_stat();
		if (state == BQ25601_CHRG_STATE_PRE || state == BQ25601_CHRG_STATE_FAST)
			*en = true;
	}
	pr_info("en=%d\n", *en);

	return ret;
}

static int bq25601_parse_dt(struct bq25601_info *info,
			    struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret = 0;

	pr_info("enter \n");
	if (!np) {
		pr_info("no of node\n");
		return -ENODEV;
	}

	if (of_property_read_string(np, "charger_name",
				    &info->chg_dev_name) < 0) {
		info->chg_dev_name = "slave_chg";
		pr_info("no charger name\n");
	}

	if (of_property_read_string(np, "alias_name",
				    &(info->chg_props.alias_name)) < 0) {
		info->chg_props.alias_name = "bq25601";
		pr_info("no alias name\n");
	}

	if (of_property_read_u32(np, "max_ichg_ua",
			&info->init_data.max_ichg) < 0) {
		info->init_data.max_ichg = BQ25601_ICHRG_I_MAX_uA;
		pr_info("use default max_ichg\n");
	}

	if (of_property_read_u32(np, "max_vreg_uv",
			&info->init_data.max_vreg) < 0) {
		info->init_data.max_vreg = BQ25601_VREG_V_MAX_uV;
		pr_info("use default max_vreg\n");
	}

	info->pinctrl_state_name = of_get_property(np, 
			"pinctrl-names", NULL);
	if (info->pinctrl_state_name) {
		pr_info("init pinctrl\n");
		info->irq_pinctrl = pinctrl_get_select(info->dev,
						info->pinctrl_state_name);
		if (IS_ERR(info->irq_pinctrl)) {
			pr_err("Could not get/set %s pinctrl state rc = %ld\n",
						info->pinctrl_state_name,
						PTR_ERR(info->irq_pinctrl));
			return PTR_ERR(info->irq_pinctrl);
		}
	}

	if (of_find_property(np, "chg-en-gpio", NULL)) {
		pr_info("config en pin\n");
		info->chg_en_gpio = of_get_named_gpio(np, "chg-en-gpio", 0);
		if (!gpio_is_valid(info->chg_en_gpio)) {
			pr_err("%d gpio get failed\n", info->chg_en_gpio);
			return -EINVAL;
		}

		ret = gpio_request(info->chg_en_gpio, "slave en pin");
		if (ret) {
			pr_err("%s: %d gpio request failed\n", info->chg_en_gpio);
			return ret;
		}
		gpio_direction_output(info->chg_en_gpio, 0);
	}

	return 0;
}

static int bq25601_do_event(struct charger_device *chg_dev, u32 event,
			    u32 args)
{
	if (chg_dev == NULL)
		return -EINVAL;

	pr_info("event = %d\n", event);
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

static struct charger_ops bq25601_chg_ops = {

	.enable_hz = bq25601_set_hiz_mode,

	/* Normal charging */
	.init = bq25601_init,
	.dump_registers = bq25601_dump_register,
	.enable = bq25601_enable_charging,
	.get_charging_current = bq25601_get_current,
	.set_charging_current = bq25601_set_current,
	.set_input_current = bq25601_set_input_current,
	.get_input_current = bq25601_get_input_current,
	.set_constant_voltage = bq25601_set_cv_voltage,
	.kick_wdt = bq25601_reset_watch_dog_timer,
	.set_mivr = bq25601_set_vindpm_voltage,
	.get_mivr = bq25601_get_vindpm_voltage,
	.plug_out = bq25601_plug_out,
	.plug_in = bq25601_plug_in,
	.is_enabled = bq25601_is_enabled,

	/* Safety timer */
	.enable_safety_timer = bq25601_enable_safetytimer,
	.is_safety_timer_enabled = bq25601_get_is_safetytimer_enable,

	/* Power path */
	/*.enable_powerpath = bq25601_enable_power_path, */
	/*.is_powerpath_enabled = bq25601_get_is_power_path_enable, */

	.event = bq25601_do_event,

};

int bq25601_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	struct bq25601_info *info = NULL;

	pr_info("enter\n");

	info = devm_kzalloc(&client->dev, sizeof(struct bq25601_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	info->dev = &client->dev;
	g_chg_dev = info;

	mutex_init(&info->i2c_rw_lock);

	ret = bq25601_hw_chipid_detect(info);
	if (ret < 0) {
		pr_err("device not found !!!\n");
		return ret;
	}

	ret = bq25601_parse_dt(info, &client->dev);
	if (ret < 0) {
		pr_err("parse device tree error !!!\n");
		return ret;
	}

	ret = bq25601_hw_init(info);
	if (ret < 0) {
		pr_err("device init fail !!!\n");
		goto fail_free_gpio;
	}

	/* Register charger device */
	info->chg_dev = charger_device_register(info->chg_dev_name,
						&client->dev, info,
						&bq25601_chg_ops,
						&info->chg_props);
	if (IS_ERR_OR_NULL(info->chg_dev)) {
		pr_info("register charger device  failed\n");
		ret = PTR_ERR(info->chg_dev);
		goto fail_free_gpio;
	}

	bq25601_dump_register(info->chg_dev);

	pr_info("probe success\n");

	return 0;

fail_free_gpio:
	if(info->chg_en_gpio > 0)
		gpio_free(info->chg_en_gpio);
	g_chg_dev = NULL;

	pr_err("probe fail\n");

	return ret;
}
EXPORT_SYMBOL_GPL(bq25601_driver_probe);

int bq25601_charger_remove(struct i2c_client *client)
{
    // todo     

    return 0;
}
EXPORT_SYMBOL_GPL(bq25601_charger_remove);


void bq25601_charger_shutdown(struct i2c_client *client)
{
    // todo
}
EXPORT_SYMBOL_GPL(bq25601_charger_shutdown);

