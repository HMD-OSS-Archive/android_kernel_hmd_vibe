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

#ifndef _bq25601_SW_H_
#define _bq25601_SW_H_
#include <linux/i2c.h>

#define BQ25601_CON0      0x00
#define BQ25601_CON1      0x01
#define BQ25601_CON2      0x02
#define BQ25601_CON3      0x03
#define BQ25601_CON4      0x04
#define BQ25601_CON5      0x05
#define BQ25601_CON6      0x06
#define BQ25601_CON7      0x07
#define BQ25601_CON8      0x08
#define BQ25601_CON9      0x09
#define BQ25601_CON10      0x0A
#define	BQ25601_CON11		0x0B
#define BQ25601_REG_NUM 12

#define SGM41513_CON15    0x0f
#define SGM41513_REG_NUM 16

#define CHIP_ADDR_HL7019D 0x6C
#define CHIP_ADDR_CX25601 0x6B
#define CHIP_ADDR_SGM41513 0x1A
#define CHIP_ID_HL7019D 0x1
#define CHIP_ID_BQ25601 0x2
#define CHIP_ID_BQ25601D 0x7
#define CHIP_ID_SGM41513 0x0
#define CHIP_ID_SGM41513A_D 0x1

#define I2C_RW_RETRY_COUNT 3

/**********************************************************
 *
 *   [MASK/SHIFT]
 *
 *********************************************************/
//CON0
#define CON0_EN_HIZ_MASK   0x01
#define CON0_EN_HIZ_SHIFT  7

#define	CON0_STAT_IMON_CTRL_MASK	0x03
#define	CON0_STAT_IMON_CTRL_SHIFT 5

#define CON0_IINLIM_MASK   0x1F
#define CON0_IINLIM_SHIFT  0

//CON1
#define CON1_PFM_MASK     0x01
#define CON1_PFM_SHIFT    7

#define CON1_WDT_RST_MASK     0x01
#define CON1_WDT_RST_SHIFT    6

#define CON1_OTG_CONFIG_MASK	0x01
#define CON1_OTG_CONFIG_SHIFT	5

#define CON1_CHG_CONFIG_MASK        0x01
#define CON1_CHG_CONFIG_SHIFT       4

#define CON1_SYS_MIN_MASK        0x07
#define CON1_SYS_MIN_SHIFT       1

#define	CON1_MIN_VBAT_SEL_MASK	0x01
#define	CON1_MIN_VBAT_SEL_SHIFT	0

//CON2
#define CON2_BOOST_LIM_MASK   0x01
#define CON2_BOOST_LIM_SHIFT  7

#define	CON2_Q1_FULLON_MASK		0x01
#define	CON2_Q1_FULLON_SHIFT	6

#define CON2_ICHG_MASK    0x3F
#define CON2_ICHG_SHIFT   0

//CON3
#define CON3_IPRECHG_MASK   0x0F
#define CON3_IPRECHG_SHIFT  4

#define CON3_ITERM_MASK           0x0F
#define CON3_ITERM_SHIFT          0

//CON4
#define CON4_VREG_MASK     0x1F
#define CON4_VREG_SHIFT    3

#define	CON4_TOPOFF_TIMER_MASK 0x03
#define	CON4_TOPOFF_TIMER_SHIFT 1

#define CON4_VRECHG_MASK    0x01
#define CON4_VRECHG_SHIFT   0

//CON5
#define CON5_EN_TERM_MASK      0x01
#define CON5_EN_TERM_SHIFT     7

#define CON5_WATCHDOG_MASK     0x03
#define CON5_WATCHDOG_SHIFT    4

#define CON5_EN_TIMER_MASK      0x01
#define CON5_EN_TIMER_SHIFT     3

#define CON5_CHG_TIMER_MASK           0x01
#define CON5_CHG_TIMER_SHIFT          2

#define CON5_TREG_MASK     0x01
#define CON5_TREG_SHIFT    1


//CON6
#define	CON6_OVP_MASK		0x03
#define	CON6_OVP_SHIFT		6

#define	CON6_BOOSTV_MASK	0x3
#define	CON6_BOOSTV_SHIFT	4

#define	CON6_VINDPM_MASK	0x0F
#define	CON6_VINDPM_SHIFT	0

//CON7
#define	CON7_FORCE_DPDM_MASK	0x01
#define	CON7_FORCE_DPDM_SHIFT	7

#define CON7_TMR2X_EN_MASK      0x01
#define CON7_TMR2X_EN_SHIFT     6

#define CON7_BATFET_DISABLE_MASK      0x01
#define CON7_BATFET_DISABLE_SHIFT     5

#define	CON7_BATFET_DLY_MASK		0x01
#define	CON7_BATFET_DLY_SHIFT		3

#define	CON7_BATFET_RST_EN_MASK		0x01
#define	CON7_BATFET_RST_EN_SHIFT	2

#define	CON7_VDPM_BAT_TRACK_MASK	0x03
#define	CON7_VDPM_BAT_TRACK_SHIFT	0

//CON8
#define CON8_VBUS_STAT_MASK      0x07
#define CON8_VBUS_STAT_SHIFT     5

#define CON8_CHRG_STAT_MASK           0x03
#define CON8_CHRG_STAT_SHIFT          3

#define CON8_PG_STAT_MASK           0x01
#define CON8_PG_STAT_SHIFT          2

#define CON8_THERM_STAT_MASK           0x01
#define CON8_THERM_STAT_SHIFT          1

#define CON8_VSYS_STAT_MASK           0x01
#define CON8_VSYS_STAT_SHIFT          0

//CON9
#define CON9_WATCHDOG_FAULT_MASK      0x01
#define CON9_WATCHDOG_FAULT_SHIFT     7

#define CON9_OTG_FAULT_MASK           0x01
#define CON9_OTG_FAULT_SHIFT          6

#define CON9_CHRG_FAULT_MASK           0x03
#define CON9_CHRG_FAULT_SHIFT          4

#define CON9_BAT_FAULT_MASK           0x01
#define CON9_BAT_FAULT_SHIFT          3

#define CON9_NTC_FAULT_MASK           0x07
#define CON9_NTC_FAULT_SHIFT          0

//CON10
#define	CON10_VBUS_GD_MASK				0x01
#define	CON10_VBUS_GD_SHIFT				7

#define	CON10_VINDPM_STAT_MASK			0x01
#define	CON10_VINDPM_STAT_SHIFT			6

#define	CON10_IINDPM_STAT_MASK			0x01
#define	CON10_IINDPM_STAT_SHIFT			5

#define	CON10_TOPOFF_ACTIVE_MASK		0x01
#define	CON10_TOPOFF_ACTIVE_SHIFT		3

#define	CON10_ACOV_STAT_MASK			0x01
#define	CON10_ACOV_STAT_SHIFT			2

#define	CON10_VINDPM_INT_MASK			0x01
#define	CON10_VINDPM_INT_SHIFT			1

#define	CON10_INT_MASK_MASK				0x03
#define	CON10_INT_MASK_SHIFT			0

//CON11
#define CON11_REG_RST_MASK     0x01
#define CON11_REG_RST_SHIFT    7


#define CON11_PN_MASK		0x0F
#define CON11_PN_SHIFT		3

#define CON11_REV_MASK           0x03
#define CON11_REV_SHIFT          0

//CON15
#define CON15_SGM41513_OS_MASK    0x3
#define CON15_SGM41513_OS_SHIFT   0

//HL7019D
#define HL7019D_PN_MASK      0x3
#define HL7019D_PN_SHIFT     5


/* charger state */
#define BQ25601_CHRG_STATE_DISABLE	0x0
#define BQ25601_CHRG_STATE_PRE		0x1
#define BQ25601_CHRG_STATE_FAST		0x2
#define BQ25601_CHRG_STATE_TERM		0x3

/* precharge current  */
#define BQ25601_PRECHRG_CUR_MASK		GENMASK(7, 4)
#define BQ25601_PRECHRG_CURRENT_STEP_uA		60000
#define BQ25601_PRECHRG_I_MIN_uA		60000
#define BQ25601_PRECHRG_I_MAX_uA		780000
#define BQ25601_PRECHRG_I_DEF_uA		180000

/* termination current  */
#define BQ25601_TERMCHRG_CUR_MASK		GENMASK(3, 0)
#define BQ25601_TERMCHRG_CURRENT_STEP_uA	60000
#define BQ25601_TERMCHRG_I_MIN_uA		60000
#define BQ25601_TERMCHRG_I_MAX_uA		960000
#define BQ25601_TERMCHRG_I_DEF_uA		180000

/* charge current  */
#define BQ25601_ICHRG_I_MASK		GENMASK(5, 0)
#define BQ25601_ICHRG_I_MIN_uA			0
#define BQ25601_ICHRG_I_STEP_uA	    60000
#define BQ25601_ICHRG_I_MAX_uA			3000000
#define BQ25601_ICHRG_I_DEF_uA			2040000

/* charge voltage  */
#define BQ25601_VREG_V_MASK		GENMASK(7, 3)
#define BQ25601_VREG_V_MAX_uV	    4624000
#define BQ25601_VREG_V_MIN_uV	    3856000
#define BQ25601_VREG_V_DEF_uV	    4208000
#define BQ25601_VREG_V_STEP_uV	    32000

/* iindpm current  */
#define BQ25601_IINDPM_I_MASK		GENMASK(4, 0)
#define BQ25601_IINDPM_I_MIN_uA	100000
#define BQ25601_IINDPM_I_MAX_uA	3800000
#define BQ25601_IINDPM_STEP_uA	    100000
#define BQ25601_IINDPM_DEF_uA	    2400000

/* vindpm voltage  */
#define BQ25601_VINDPM_V_MASK      GENMASK(3, 0)
#define BQ25601_VINDPM_V_MIN_uV    3900000
#define BQ25601_VINDPM_V_MAX_uV    5400000
#define SGM41513_VINDPM_V_MAX_uV   12000000
#define BQ25601_VINDPM_STEP_uV     100000
#define BQ25601_VINDPM_DEF_uV	    BQ25601_VINDPM_V_MIN_uV
#define BQ25601_VINDPM_OS_MASK     GENMASK(1, 0)

/**********************************************************
 *
 *   [Extern Function]
 *
 *********************************************************/
unsigned int bq25601_config_interface(unsigned char RegNum,
					unsigned char val, unsigned char MASK,
					unsigned char SHIFT);
int bq25601_read_interface(unsigned char RegNum,
				    unsigned char *val, unsigned char MASK,
				    unsigned char SHIFT);

//CON0----------------------------------------------------
void bq25601_set_en_hiz(unsigned int val);
void bq25601_set_vindpm(unsigned int val);
void bq25601_set_iinlim(unsigned int val);
//CON1----------------------------------------------------
void bq25601_set_reg_rst(unsigned int val);
void bq25601_set_pfm(unsigned int val);
void bq25601_set_wdt_rst(unsigned int val);
void bq25601_set_chg_config(unsigned int val);
void bq25601_set_otg_config(unsigned int val);
void bq25601_set_sys_min(unsigned int val);
void bq25601_set_boost_lim(unsigned int val);
//CON2----------------------------------------------------
void bq25601_set_ichg(unsigned int val);
void bq25601_set_rdson(unsigned int val);
//CON3----------------------------------------------------
void bq25601_set_iprechg(unsigned int val);
void bq25601_set_iterm(unsigned int val);
//CON4----------------------------------------------------
void bq25601_set_vreg(unsigned int val);
void bq25601_set_batlowv(unsigned int val);
void bq25601_set_vrechg(unsigned int val);
void bq25601_set_topoff_timer(unsigned int val);
//CON5----------------------------------------------------
void bq25601_set_en_term(unsigned int val);
void bq25601_set_term_stat(unsigned int val);
void bq25601_set_watchdog(unsigned int val);
void bq25601_set_en_timer(unsigned int val);
void bq25601_set_chg_timer(unsigned int val);
//CON6----------------------------------------------------
void bq25601_set_treg(unsigned int val);
//CON7----------------------------------------------------
void bq25601_set_tmr2x_en(unsigned int val);
void bq25601_set_batfet_disable(unsigned int val);
void bq25601_set_int_mask(unsigned int val);
void bq25601_set_vdpm_bat_track(unsigned int val);
//CON8----------------------------------------------------
unsigned int bq25601_get_system_status(void);
unsigned int bq25601_get_vbus_stat(void);
unsigned int bq25601_get_chrg_stat(void);
unsigned int bq25601_get_vsys_stat(void);
//---------------------------------------------------------

int bq25601_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id);
int bq25601_charger_remove(struct i2c_client *client);
void bq25601_charger_shutdown(struct i2c_client *client);

#endif // _bq25601_SW_H_

