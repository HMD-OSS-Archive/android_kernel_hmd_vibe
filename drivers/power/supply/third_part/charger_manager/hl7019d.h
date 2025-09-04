/*
 * Copyright (C) 2016 Chino-e Inc.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef _HL7019D_SW_H_
#define _HL7019D_SW_H_

#include "charger_class.h"
#include <linux/i2c.h>

#define HL7019D_CON0		0x00
#define HL7019D_CON1		0x01
#define HL7019D_CON2		0x02
#define HL7019D_CON3		0x03
#define HL7019D_CON4		0x04
#define HL7019D_CON5		0x05
#define HL7019D_CON6		0x06
#define HL7019D_CON7		0x07
#define HL7019D_CON8		0x08
#define HL7019D_CON9		0x09
#define HL7019D_CONA		0x0A
#define HL7019D_CONB		0x0B
#define HL7019D_CONC		0x0C
#define HL7019D_COND		0x0D
#define HL7019D_REG_NUM		14

/* CON0 */
#define HL7019D_CON0_EN_HIZ_MASK	0x01
#define HL7019D_CON0_EN_HIZ_SHIFT	7
#define HL7019D_CON0_VINDPM_MASK	0x0F
#define HL7019D_CON0_VINDPM_SHIFT	3
#define HL7019D_CON0_IINLIM_MASK	0x07
#define HL7019D_CON0_IINLIM_SHIFT	0

/* CON1 */
#define HL7019D_CON1_REG_RESET_MASK			0x01
#define HL7019D_CON1_REG_RESET_SHIFT		7
#define HL7019D_CON1_I2C_WDT_RESET_MASK		0x01
#define HL7019D_CON1_I2C_WDT_RESET_SHIFT	6
#define HL7019D_CON1_CHG_CONFIG_MASK		0x03
#define HL7019D_CON1_CHG_CONFIG_SHIFT		4
#define HL7019D_CON1_SYS_MIN_MASK			0x07
#define HL7019D_CON1_SYS_MIN_SHIFT			1
#define HL7019D_CON1_BOOST_LIM_MASK			0x01
#define HL7019D_CON1_BOOST_LIM_SHIFT		0

/* CON2 */
#define HL7019D_CON2_ICHG_MASK			0x3F
#define HL7019D_CON2_ICHG_SHIFT			2
#define HL7019D_CON2_BCLOD_MASK			0x01
#define HL7019D_CON2_BCLOD_SHIFT		1
#define HL7019D_CON2_FORCE_20PCT_MASK	0x01
#define HL7019D_CON2_FORCE_20PCT_SHIFT	0

/* CON3 */
#define HL7019D_CON3_IPRECHG_MASK	0x0F
#define HL7019D_CON3_IPRECHG_SHIFT	4
#define HL7019D_CON3_ITERM_MASK		0x0F
#define HL7019D_CON3_ITERM_SHIFT	0

/* CON4 */
#define HL7019D_CON4_VREG_MASK		0x3F
#define HL7019D_CON4_VREG_SHIFT		2
#define HL7019D_CON4_BATLOWV_MASK	0x01
#define HL7019D_CON4_BATLOWV_SHIFT	1
#define HL7019D_CON4_VRECHG_MASK	0x01
#define HL7019D_CON4_VRECHG_SHIFT	0

/* CON5 */
#define HL7019D_CON5_EN_TERM_MASK			0x01
#define HL7019D_CON5_EN_TERM_SHIFT			7
#define HL7019D_CON5_TERM_STAT_MASK			0x01
#define HL7019D_CON5_TERM_STAT_SHIFT		6
#define HL7019D_CON5_WATCHDOG_MASK			0x03
#define HL7019D_CON5_WATCHDOG_SHIFT			4
#define HL7019D_CON5_EN_SAFE_TIMER_MASK		0x01
#define HL7019D_CON5_EN_SAFE_TIMER_SHIFT	3
#define HL7019D_CON5_CHG_TIMER_MASK			0x03
#define HL7019D_CON5_CHG_TIMER_SHIFT		1
#define HL7019D_CON5_Reserved_MASK			0x01
#define HL7019D_CON5_Reserved_SHIFT			0

/* CON6 */
#define HL7019D_CON6_BOOSTV_MASK	0x0F
#define HL7019D_CON6_BOOSTV_SHIFT	4
#define HL7019D_CON6_BHOT_MASK		0x03
#define HL7019D_CON6_BHOT_SHIFT		2
#define HL7019D_CON6_TREG_MASK		0x03
#define HL7019D_CON6_TREG_SHIFT		0

/* CON7 */
#define HL7019D_CON7_DPDM_EN_MASK				0x01
#define HL7019D_CON7_DPDM_EN_SHIFT				7
#define HL7019D_CON7_TMR2X_EN_MASK				0x01
#define HL7019D_CON7_TMR2X_EN_SHIFT				6
#define HL7019D_CON7_PPFET_DISABLE_MASK			0x01
#define HL7019D_CON7_PPFET_DISABLE_SHIFT		5
//three bits were reserved
#define HL7019D_CON7_CHRG_FAULT_INT_MASK_MASK	0x01
#define HL7019D_CON7_CHRG_FAULT_INT_MASK_SHIFT	1
#define HL7019D_CON7_BAT_FAULT_INT_MASK_MASK	0x01
#define HL7019D_CON7_BAT_FAULT_INT_MASK_SHIFT	0

/* CON8 */
#define HL7019D_CON8_VIN_STAT_MASK		0x03
#define HL7019D_CON8_VIN_STAT_SHIFT		6
#define HL7019D_CON8_CHRG_STAT_MASK		0x03
#define HL7019D_CON8_CHRG_STAT_SHIFT	4
#define HL7019D_CON8_DPM_STAT_MASK		0x01
#define HL7019D_CON8_DPM_STAT_SHIFT		3
#define HL7019D_CON8_PG_STAT_MASK		0x01
#define HL7019D_CON8_PG_STAT_SHIFT		2
#define HL7019D_CON8_THERM_STAT_MASK	0x01
#define HL7019D_CON8_THERM_STAT_SHIFT	1
#define HL7019D_CON8_VSYS_STAT_MASK		0x01
#define HL7019D_CON8_VSYS_STAT_SHIFT	0

/* CON9 */
#define HL7019D_CON9_WATCHDOG_FAULT_MASK	0x01
#define HL7019D_CON9_WATCHDOG_FAULT_SHIFT	7
#define HL7019D_CON9_OTG_FAULT_MASK			0x01
#define HL7019D_CON9_OTG_FAULT_SHIFT		6
#define HL7019D_CON9_CHRG_FAULT_MASK		0x03
#define HL7019D_CON9_CHRG_FAULT_SHIFT		4
#define HL7019D_CON9_BAT_FAULT_MASK			0x01
#define HL7019D_CON9_BAT_FAULT_SHIFT		3
// one bit was reserved
#define HL7019D_CON9_NTC_FAULT_MASK			0x07
#define HL7019D_CON9_NTC_FAULT_SHIFT		0

#define HL7019D_CON9_ALL_FAULT_MASK			0xFF
#define HL7019D_CON9_ALL_FAULT_SHIFT		0

/* CONA */
// vender info register

/* CONB */
#define HL7019D_CONB_TSR_MASK				0x03
#define HL7019D_CONB_TSR_SHIFT				6
#define HL7019D_CONB_TRSP_MASK				0x01
#define HL7019D_CONB_TRSP_SHIFT				5
#define HL7019D_CONB_DIS_RECONNECT_MASK		0x01
#define HL7019D_CONB_DIS_RECONNECT_SHIFT	4
#define HL7019D_CONB_DIS_SR_INCHG_MASK		0x01
#define HL7019D_CONB_DIS_SR_INCHG_SHIFT		3
#define HL7019D_CONB_TSHIP_MASK				0x07
#define HL7019D_CONB_TSHIP_SHIFT			0

/* CONC */
#define HL7019D_CONC_BAT_COMP_MASK			0x07
#define HL7019D_CONC_BAT_COMP_SHIFT			5
#define HL7019D_CONC_BAT_VCLAMP_MASK		0x07
#define HL7019D_CONC_BAT_VCLAMP_SHIFT		2
// one bit was reserved
#define HL7019D_CONC_BOOST_9V_EN_MASK		0x01
#define HL7019D_CONC_BOOST_9V_EN_SHIFT		0

/* COND */
// one bit was reserved
#define HL7019D_COND_DISABLE_TS_MASK		0x01
#define HL7019D_COND_DISABLE_TS_SHIFT		6
#define HL7019D_COND_VINDPM_OFFSET_MASK		0x01
#define HL7019D_COND_VINDPM_OFFSET_SHIFT	5
// five bits were reserved

void hl7019d_set_en_hiz(unsigned int val);
int hl7019d_enable_charging(struct charger_device *chg_dev, bool en);
int hl7019d_charger_ic_init(struct charger_device *chg_dev);
int hl7019d_get_current(struct charger_device *chg_dev, u32 *ichg);
int hl7019d_set_current(struct charger_device *chg_dev, u32 current_value);
int hl7019d_set_input_current(struct charger_device *chg_dev, u32 current_value);
int hl7019d_get_input_current(struct charger_device *chg_dev, u32 *aicr);
int hl7019d_set_cv_voltage(struct charger_device *chg_dev, u32 cv);
int hl7019d_reset_watch_dog_timer(struct charger_device *chg_dev);
int hl7019d_set_vindpm_voltage(struct charger_device *chg_dev, u32 vindpm_vol);
int hl7019d_get_vindpm_voltage(struct charger_device *chg_dev, u32 *uV);
int hl7019d_is_enabled(struct charger_device *chg_dev, bool *en);
int hl7019d_enable_safety_timer(struct charger_device *chg_dev, bool en);
int hl7019d_is_safety_timer_enabled(struct charger_device *chg_dev, bool *en);
int hl7019d_get_cv_voltage(struct charger_device *chg_dev, u32 *cv);
int hl7019d_get_termination_curr(struct charger_device *chg_dev, u32 *term_curr);

#endif
