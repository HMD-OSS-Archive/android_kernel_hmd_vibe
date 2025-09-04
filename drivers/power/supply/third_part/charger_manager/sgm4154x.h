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

#ifndef _SGM4154X_CHARGER_H
#define _SGM4154X_CHARGER_H

#include <linux/iio/consumer.h>
#include <linux/i2c.h>
#include "cne_charger.h"

#define SGM4154X_NAME		"sgm4154x"

#define SGM4154X_PN_ID      (BIT(6) | BIT(5) | BIT(4) | BIT(3))

#define CHIP_ID_SGM41542		0xD

#define BC12_CHECK_DEFAULT_DELAY_TIME_MS 150
#define BC12_CHECK_DEFAULT_DELAY_COUNT 5

#define HVDCP_HV_THRESHOLD_MV_MIN 7500
#define HVDCP_HV_DETECT_RETRY 5

#define HVDCP_DP_0P6V_HOLD_TIME_MS 1500
#define HVDCP_CHECK_DEFAULT_DELAY_COUNT 15

#define R_CHARGER_1 300
#define R_CHARGER_2 39

#define I2C_RW_RETRY_COUNT 3

/*define register*/
#define SGM4154X_CHRG_CTRL_0	0x00
#define SGM4154X_CHRG_CTRL_1	0x01
#define SGM4154X_CHRG_CTRL_2	0x02
#define SGM4154X_CHRG_CTRL_3	0x03
#define SGM4154X_CHRG_CTRL_4	0x04
#define SGM4154X_CHRG_CTRL_5	0x05
#define SGM4154X_CHRG_CTRL_6	0x06
#define SGM4154X_CHRG_CTRL_7	0x07
#define SGM4154X_CHRG_STAT	    0x08
#define SGM4154X_CHRG_FAULT	    0x09
#define SGM4154X_CHRG_CTRL_A	0x0a
#define SGM4154X_CHRG_CTRL_B	0x0b
#define SGM4154X_CHRG_CTRL_C	0x0c
#define SGM4154X_CHRG_CTRL_D	0x0d
#define SGM4154X_INPUT_DET   	0x0e
#define SGM4154X_CHRG_CTRL_F	0x0f

/* define register total count  */
#define SGM4154X_REG_NUM    (SGM4154X_CHRG_CTRL_F + 1)

#define SGM4154X_REG01_WD_RST_MASK	BIT(6)
#define SGM4154X_REG01_WD_RST_EN	BIT(6)

/* charge status flags  */
#define SGM4154X_CHRG_EN		BIT(4)
#define SGM4154X_HIZ_EN		    BIT(7)
#define SGM4154X_TERM_EN		BIT(7)
#define SGM4154X_VAC_OVP_MASK	GENMASK(7, 6)
#define SGM4154X_DPDM_ONGOING   BIT(7)
#define SGM4154X_VBUS_GOOD      BIT(7)
#define SGM4154X_VBUS_GOOD_SHIFT      0x7

#define SGM4154X_BOOSTV 		GENMASK(5, 4)
#define SGM4154X_BOOST_LIM 		BIT(7)
#define SGM4154X_OTG_EN		    BIT(5)

/* Part ID  */
#define SGM4154X_PN_MASK	    GENMASK(6, 3)
#define SGM4154X_PN_SHIFT	    0x3

/* DPDM */
#define SGM4154X_IINDET_EN_MASK BIT(7)
#define SGM4154X_IINDET_EN_SHIFT       0x7
#define SGM4154X_DET_DONE_MASK BIT(7)
#define SGM4154X_DET_DONE_SHIFT       0x7

#define SGM4154X_VREG_FT_MASK	GENMASK(7, 6)
#define SGM4154X_VREG_FT_ADD_8MV	BIT(6)

/* WDT TIMER SET  */
#define SGM4154X_WDT_TIMER_MASK        GENMASK(5, 4)
#define SGM4154X_WDT_TIMER_DISABLE     0
#define SGM4154X_WDT_TIMER_40S         BIT(4)
#define SGM4154X_WDT_TIMER_80S         BIT(5)
#define SGM4154X_WDT_TIMER_160S        (BIT(4)| BIT(5))

#define SGM4154X_WDT_RST_MASK          BIT(6)

/* SAFETY TIMER SET  */
#define SGM4154X_SAFETY_TIMER_MASK     GENMASK(3, 3)
#define SGM4154X_SAFETY_TIMER_DISABLE     0
#define SGM4154X_SAFETY_TIMER_EN       BIT(3)
#define SGM4154X_SAFETY_TIMER_5H         0
#define SGM4154X_SAFETY_TIMER_10H      BIT(2)

/* recharge voltage  */
#define SGM4154X_VRECHARGE              BIT(0)
#define SGM4154X_VRECHRG_STEP_mV		100
#define SGM4154X_VRECHRG_OFFSET_mV		100

/* charge status  */
#define SGM4154X_VSYS_STAT		BIT(0)
#define SGM4154X_THERM_STAT		BIT(1)
#define SGM4154X_PG_STAT		BIT(2)
#define SGM4154X_CHG_STAT_MASK	GENMASK(4, 3)
#define SGM4154X_PRECHRG		BIT(3)
#define SGM4154X_FAST_CHRG	    BIT(4)
#define SGM4154X_TERM_CHRG	    (BIT(3)| BIT(4))

/* charge type  */
#define SGM4154X_VBUS_STAT_MASK	GENMASK(7, 5)
#define SGM4154X_NOT_CHRGING	0
#define SGM4154X_USB_SDP		BIT(5)
#define SGM4154X_USB_CDP		BIT(6)
#define SGM4154X_USB_DCP		(BIT(5) | BIT(6))
#define SGM4154X_UNKNOWN	    (BIT(7) | BIT(5))
#define SGM4154X_NON_STANDARD	(BIT(7) | BIT(6))
#define SGM4154X_OTG_MODE	    (BIT(7) | BIT(6) | BIT(5))

/* TEMP Status  */
#define SGM4154X_TEMP_MASK	    GENMASK(2, 0)
#define SGM4154X_TEMP_NORMAL	BIT(0)
#define SGM4154X_TEMP_WARM	    BIT(1)
#define SGM4154X_TEMP_COOL	    (BIT(0) | BIT(1))
#define SGM4154X_TEMP_COLD	    (BIT(0) | BIT(3))
#define SGM4154X_TEMP_HOT	    (BIT(2) | BIT(3))

/* precharge current  */
#define SGM4154X_PRECHRG_CUR_MASK		GENMASK(7, 4)
#define SGM4154X_PRECHRG_CURRENT_STEP_uA		60000
#define SGM4154X_PRECHRG_I_MIN_uA		60000
#define SGM4154X_PRECHRG_I_MAX_uA		780000
#define SGM4154X_PRECHRG_I_DEF_uA		180000

/* termination current  */
#define SGM4154X_TERMCHRG_CUR_MASK		GENMASK(3, 0)
#define SGM4154X_TERMCHRG_CURRENT_STEP_uA	60000
#define SGM4154X_TERMCHRG_I_MIN_uA		60000
#define SGM4154X_TERMCHRG_I_MAX_uA		960000
#define SGM4154X_TERMCHRG_I_DEF_uA		180000

/* charge current  */
#define SGM4154X_ICHRG_I_MASK		GENMASK(5, 0)
#define SGM4154X_ICHRG_I_MIN_uA			0
#define SGM4154X_ICHRG_I_STEP_uA	    60000
#define SGM4154X_ICHRG_I_MAX_uA			3780000
#define SGM4154X_ICHRG_I_DEF_uA			2000000

/* charge voltage  */
#define SGM4154X_VREG_V_MASK		GENMASK(7, 3)
#define SGM4154X_VREG_V_MAX_uV	    4624000
#define SGM4154X_VREG_V_MIN_uV	    3856000
#define SGM4154X_VREG_V_DEF_uV	    4450000
#define SGM4154X_VREG_V_STEP_uV	    32000

/* iindpm current  */
#define SGM4154X_IINDPM_I_MASK		GENMASK(4, 0)
#define SGM4154X_IINDPM_I_MIN_uA	100000
#define SGM4154X_IINDPM_I_MAX_uA	3800000
#define SGM4154X_IINDPM_STEP_uA	    100000
#define SGM4154X_IINDPM_DEF_uA	    2000000

/* vindpm voltage  */
#define SGM4154X_VINDPM_V_MASK      GENMASK(3, 0)
#define SGM4154X_VINDPM_V_MIN_uV    3900000
#define SGM4154X_VINDPM_V_MAX_uV    12000000
#define SGM4154X_VINDPM_STEP_uV     100000
#define SGM4154X_VINDPM_DEF_uV	    SGM4154X_VINDPM_V_MIN_uV
#define SGM4154X_VINDPM_OS_MASK     GENMASK(1, 0)

/* DP DM SEL  */
#define SGM4154X_DP_VSEL_MASK       GENMASK(4, 3)
#define SGM4154X_DP_VSEL_SHIFT      3
#define SGM4154X_DM_VSEL_MASK       GENMASK(2, 1)
#define SGM4154X_DM_VSEL_SHIFT		1

#define SGM4154X_DPDM_VSEL_HIZ      0x0
#define SGM4154X_DPDM_VSEL_0V       0x1
#define SGM4154X_DPDM_VSEL_0P6V     0x2
#define SGM4154X_DPDM_VSEL_3P3V     0x3

/* PUMPX SET  */
#define SGM4154X_EN_PUMPX           BIT(7)
#define SGM4154X_PUMPX_UP           BIT(6)
#define SGM4154X_PUMPX_DN           BIT(5)

struct sgm4154x_init_data {
	u32 ichg;	/* charge current		*/
	u32 ilim;	/* input current		*/
	u32 vreg;	/* regulation voltage		*/
	u32 iterm;	/* termination current		*/
	u32 iprechg;	/* precharge current		*/
	u32 vlim;	/* minimum system voltage limit */
	u32 max_ichg;
	u32 max_vreg;
};

struct sgm4154x_state {
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

enum detect_stage_type {
	DETECT_TYPE_UNKNOW,
	DETECT_TYPE_CONNECT,
	DETECT_TYPE_CHECK,
	DETECT_TYPE_CHECK_DONE,
	DETECT_TYPE_DISCONNECT,
	DETECT_TYPE_HVDCP,
	DETECT_TYPE_HVDCP_CHECK,
};

struct sgm4154x_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;	
	struct power_supply *usb;
	struct power_supply *ac;
	struct mutex lock;
	struct mutex i2c_rw_lock;

	struct notifier_block usb_nb;
	struct work_struct usb_work;
	unsigned long usb_event;
	struct regmap *regmap;
	struct iio_channel *vbus_iio_channel;

	/* pinctrl parameters */
	const char		*pinctrl_state_name;
	struct pinctrl		*irq_pinctrl;
	int irq_gpio;

	bool attach;
	bool tcpc_attach;
	bool ignore_detect;
	bool vbus_gd;
	enum charger_type chg_type;
	struct mutex detect_lock;
	atomic_t chg_type_check_cnt;
	atomic_t vbus_check_cnt;
	atomic_t input_check_cnt;
	//////////////////////////////////////////////////
	struct task_struct *detect_task;
	wait_queue_head_t waitq;
	enum detect_stage_type detect_stage;
	/* QC2.0 add */
	bool enable_hvdcp_check;
	ktime_t hvdcp_check_begin_time;
	int hvdcp_detect_retry_count;
	//////////////////////////////////////////////////

	const char *chg_dev_name;

	char model_name[I2C_NAME_SIZE];
	int device_id;

	struct sgm4154x_init_data init_data;
	struct sgm4154x_state state;
	struct charger_device *chg_dev;
	bool enable_otg_regulator;
	struct regulator_dev *otg_rdev;

	// struct delayed_work charge_detect_delayed_work;

	struct notifier_block pm_nb;

	struct wakeup_source *charger_wakelock;
	struct delayed_work charger_change_work;
};

void sgm4154x_charger_shutdown(struct i2c_client *client);
int sgm4154x_charger_remove(struct i2c_client *client);
int sgm4154x_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id);
#endif /* _SGM4154X_CHARGER_H */
