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

#include "hl7019d.h"
#include "bq25601.h"

const unsigned int VBAT_CVTH[] = {
	3504000, 3520000, 3536000, 3552000,
	3568000, 3584000, 3600000, 3616000,
	3632000, 3648000, 3664000, 3680000,
	3696000, 3712000, 3728000, 3744000,
	3760000, 3776000, 3792000, 3808000,
	3824000, 3840000, 3856000, 3872000,
	3888000, 3904000, 3920000, 3936000,
	3952000, 3968000, 3984000, 4000000,
	4016000, 4032000, 4048000, 4064000,
	4080000, 4096000, 4112000, 4128000,
	4144000, 4160000, 4176000, 4192000,
	4208000, 4224000, 4240000, 4256000,
	4272000, 4288000, 4304000, 4320000,
	4336000, 4352000, 4368000, 4386000,
	4400000, 4416000, 4432000, 4448000,
	4464000, 4480000, 4496000, 4512000
};

const unsigned int VINDPM_NORMAL_CVTH[] = {
	3880000, 3970000, 4030000, 4120000,
	4200000, 4290000, 4350000, 4440000,
	4520000, 4610000, 4670000, 4760000,
	4840000, 4930000, 4990000, 5080000
};

const unsigned int VINDPM_HIGH_CVTH[] = {
	8320000, 8500000, 8640000, 8820000,
	9010000, 9190000, 9330000, 9510000,
	9690000, 9870000, 10010000, 10190000,
	10380000, 10560000, 10700000, 10880000
};

const unsigned int SYS_MIN_CVTH[] = {
	3000000, 3100000, 3200000, 3300000,
	3400000, 3500000, 3600000, 3700000
};

const unsigned int BOOSTV_CVTH[] = {
	4550000, 4614000, 4678000, 4742000,
	4806000, 4870000, 4934000, 4998000,
	5062000, 5126000, 5190000, 5254000,
	5318000, 5382000, 5446000, 5510000
};

const unsigned int CSTH[] = {
	512000, 576000, 640000, 704000,
	768000, 832000, 896000, 960000,
	1024000, 1088000, 1152000, 1216000,
	1280000, 1344000, 1408000, 1472000,
	1536000, 1600000, 1664000, 1728000,
	1792000, 1856000, 1920000, 1984000,
	2048000, 2112000, 2176000, 2240000,
	2304000, 2368000, 2432000, 2496000, 
	2560000, 2624000, 2688000, 2752000,
	2816000, 2880000, 2944000, 3008000,
	3072000, 3200000, 3264000, 3328000, 
	3392000, 3456000, 3520000, 3584000,
	3648000, 3712000, 3776000, 3840000,
	3904000, 3968000, 4032000, 4096000, 
	4160000, 4224000, 4288000, 4352000,
	4416000, 4480000, 4544000
};

/*hl7019d REG00 IINLIM[5:0]*/
const unsigned int INPUT_CSTH[] = {
	100000, 150000, 500000, 900000,
	1000000, 1500000, 2000000, 3000000
};

const unsigned int IPRE_CSTH[] = {
	128000, 256000, 384000, 512000,
	640000, 768000, 896000, 1024000,
	1152000, 1280000, 1408000, 1536000,
	1664000, 1792000, 1920000, 2048000
};

const unsigned int ITERM_CSTH[] = {
	128000, 256000, 384000, 512000,
	640000, 768000, 896000, 1024000,
	1152000, 1280000, 1408000, 1536000,
	1664000, 1792000, 1920000, 2048000
};

unsigned int charging_value_to_parameter(const unsigned int *parameter, const unsigned int array_size,
					const unsigned int val)
{
	if (val < array_size)
		return parameter[val];
	pr_err("Can't find the parameter\n");
	return parameter[0];
}

unsigned int charging_parameter_to_value(const unsigned int *parameter, const unsigned int array_size,
					const unsigned int val)
{
	unsigned int i;

	pr_info("array_size = %d\n", array_size);

	for (i = 0; i < array_size; i++) {
		if (val == *(parameter + i))
			return i;
	}

	pr_info("NO register value match\n");
	/* TODO: ASSERT(0);	// not find the value */
	return 0;
}

static unsigned int bmt_find_closest_level(const unsigned int *pList, unsigned int number,
					 unsigned int level)
{
	unsigned int i;
	unsigned int max_value_in_last_element;

	if (pList[0] < pList[1])
		max_value_in_last_element = 1;
	else
		max_value_in_last_element = 0;

	if (max_value_in_last_element == 1) {
		for (i = (number - 1); i != 0; i--) {	/* max value in the last element */
			if (pList[i] <= level) {
				pr_info("zzf_%d<=%d, i=%d\n", pList[i], level, i);
				return pList[i];
			}
		}
		pr_err("Can't find closest level\n");
		return pList[0];
		/* return 000; */
	} else {
		for (i = 0; i < number; i++) {	/* max value in the first element */
			if (pList[i] <= level)
				return pList[i];
		}
		pr_err("Can't find closest level\n");
		return pList[number - 1];
		/* return 000; */
	}
}

/* CON0 */
void hl7019d_set_en_hiz(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON0),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON0_EN_HIZ_MASK),
				(unsigned char)(HL7019D_CON0_EN_HIZ_SHIFT)
				);
}

void hl7019d_set_vindpm(unsigned int val)
{
	if(val > 15) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON0),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON0_VINDPM_MASK),
				(unsigned char)(HL7019D_CON0_VINDPM_SHIFT)
				);
}

void hl7019d_set_iinlim(unsigned int val)
{
	if(val > 7) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON0),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON0_IINLIM_MASK),
				(unsigned char)(HL7019D_CON0_IINLIM_SHIFT)
				);
}

/* CON1 */
void hl7019d_set_register_reset(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON1),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON1_REG_RESET_MASK),
				(unsigned char)(HL7019D_CON1_REG_RESET_SHIFT)
				);
}

void hl7019d_set_i2cwatchdog_timer_reset(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON1),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON1_I2C_WDT_RESET_MASK),
				(unsigned char)(HL7019D_CON1_I2C_WDT_RESET_SHIFT)
				);
}

void hl7019d_set_chg_config(unsigned int val)
{
	if(val > 3) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON1),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON1_CHG_CONFIG_MASK),
				(unsigned char)(HL7019D_CON1_CHG_CONFIG_SHIFT)
				);
}

void hl7019d_set_sys_min(unsigned int val)
{
	if(val > 7) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON1),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON1_SYS_MIN_MASK),
				(unsigned char)(HL7019D_CON1_SYS_MIN_SHIFT)
				);
}

void hl7019d_set_boost_lim(unsigned int val)
{
	if(val > 7) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON1),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON1_BOOST_LIM_MASK),
				(unsigned char)(HL7019D_CON1_BOOST_LIM_SHIFT)
				);
}

/* CON2 */
void hl7019d_set_ichg(unsigned int val)
{
	if(val > 62) { //hhl modify
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON2),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON2_ICHG_MASK),
				(unsigned char)(HL7019D_CON2_ICHG_SHIFT)
				);
}

void hl7019d_set_bcold(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON2),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON2_BCLOD_MASK),
				(unsigned char)(HL7019D_CON2_BCLOD_SHIFT)
				);
}

void hl7019d_set_force_20pct(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON2),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON2_FORCE_20PCT_MASK),
				(unsigned char)(HL7019D_CON2_FORCE_20PCT_SHIFT)
				);
}

/* CON3 */
void hl7019d_set_iprechg(unsigned int val)
{
	if(val > 15) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON3),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON3_IPRECHG_MASK),
				(unsigned char)(HL7019D_CON3_IPRECHG_SHIFT)
				);
}

void hl7019d_set_iterm(unsigned int val)
{
	if(val > 15) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON3),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON3_ITERM_MASK),
				(unsigned char)(HL7019D_CON3_ITERM_SHIFT)
				);
}

/* CON4 */
void hl7019d_set_vreg(unsigned int val)
{
	if(val > 63) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON4),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON4_VREG_MASK),
				(unsigned char)(HL7019D_CON4_VREG_SHIFT)
				);
}

void hl7019d_set_batlowv(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}
	
	bq25601_config_interface((unsigned char)(HL7019D_CON4),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON4_BATLOWV_MASK),
				(unsigned char)(HL7019D_CON4_BATLOWV_SHIFT)
				);
}

void hl7019d_set_vrechg(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON4),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON4_VRECHG_MASK),
				(unsigned char)(HL7019D_CON4_VRECHG_SHIFT)
				);
}

/* CON5 */
void hl7019d_set_en_term(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON5),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON5_EN_TERM_MASK),
				(unsigned char)(HL7019D_CON5_EN_TERM_SHIFT)
				);
}

void hl7019d_set_term_stat(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON5),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON5_TERM_STAT_MASK),
				(unsigned char)(HL7019D_CON5_TERM_STAT_SHIFT)
				);
}

void hl7019d_set_i2cwatchdog(unsigned int val)
{
	if(val > 3) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON5),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON5_WATCHDOG_MASK),
				(unsigned char)(HL7019D_CON5_WATCHDOG_SHIFT)
				);
}

void hl7019d_set_en_safty_timer(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON5),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON5_EN_SAFE_TIMER_MASK),
				(unsigned char)(HL7019D_CON5_EN_SAFE_TIMER_SHIFT)
				);
}

void hl7019d_set_charge_timer(unsigned int val)
{
	if(val > 3) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON5),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON5_CHG_TIMER_MASK),
				(unsigned char)(HL7019D_CON5_CHG_TIMER_SHIFT)
				);
}

/* CON6 */
void hl7019d_set_boostv(unsigned int val)
{
	if(val > 15) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON6),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON6_BOOSTV_MASK),
				(unsigned char)(HL7019D_CON6_BOOSTV_SHIFT)
				);
}

void hl7019d_set_bhot(unsigned int val)
{
	if(val > 3) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON6),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON6_BHOT_MASK),
				(unsigned char)(HL7019D_CON6_BHOT_SHIFT)
				);
}

void hl7019d_set_treg(unsigned int val)
{
	if(val > 3) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON6),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON6_TREG_MASK),
				(unsigned char)(HL7019D_CON6_TREG_SHIFT)
				);
}

/* CON7 */
void hl7019d_set_dpdm_en(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON7),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON7_DPDM_EN_MASK),
				(unsigned char)(HL7019D_CON7_DPDM_EN_SHIFT)
				);
}

void hl7019d_set_tmr2x_en(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON7),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON7_TMR2X_EN_MASK),
				(unsigned char)(HL7019D_CON7_TMR2X_EN_SHIFT)
				);
}

void hl7019d_set_ppfet_disable(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON7),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON7_PPFET_DISABLE_MASK),
				(unsigned char)(HL7019D_CON7_PPFET_DISABLE_MASK)
				);
}

void hl7019d_set_chrgfault_int_mask(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON7),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON7_CHRG_FAULT_INT_MASK_MASK),
				(unsigned char)(HL7019D_CON7_CHRG_FAULT_INT_MASK_SHIFT)
				);
}

void hl7019d_set_batfault_int_mask(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CON7),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CON7_BAT_FAULT_INT_MASK_MASK),
				(unsigned char)(HL7019D_CON7_BAT_FAULT_INT_MASK_SHIFT)
				);
}

/* CON8 */
static unsigned int hl7019d_get_vin_status(void)
{
	unsigned int ret;
	unsigned char val;

	ret = bq25601_read_interface((unsigned char)(HL7019D_CON8),
			&val,
			(unsigned char)(HL7019D_CON8_VIN_STAT_MASK),
			(unsigned char)(HL7019D_CON8_VIN_STAT_SHIFT)
			);

	return val;
}

static void hl7019d_vin_status_dump(void)
{
	unsigned int vin_status;

	vin_status = hl7019d_get_vin_status();

	switch(vin_status)
	{
		case 0:
			pr_err("no input or dpm detection incomplete.\n");
			break;
		case 1:
			pr_err("USB host inserted.\n");
			break;
		case 2:
			pr_err("Adapter inserted.\n");
			break;
		case 3:
			pr_err("OTG device inserted.\n");
			break;
		default:
			pr_err("wrong vin status.\n");
			break;
	}
}

static unsigned int hl7019d_get_chrg_status(void)
{
	unsigned int ret;
	unsigned char val;

	ret = bq25601_read_interface((unsigned char)(HL7019D_CON8),
			&val,
			(unsigned char)(HL7019D_CON8_CHRG_STAT_MASK),
			(unsigned char)(HL7019D_CON8_CHRG_STAT_SHIFT)
			);

	return val;
}

static void hl7019d_chrg_status_dump(void)
{
	unsigned int chrg_status;

	chrg_status = hl7019d_get_chrg_status();

	switch(chrg_status)
	{
		case 0:
			pr_err("not charging.\n");
			break;
		case 1:
			pr_err("precharging mode.\n");
			break;
		case 2:
			pr_err("fast charging mode.\n");
			break;
		case 3:
			pr_err("charge termination done.\n");
			break;
		default:
			pr_err("wrong charge status.\n");
			break;
	}
}

static unsigned int hl7019d_get_dpm_status(void)
{
	unsigned int ret;
	unsigned char val;

	ret = bq25601_read_interface((unsigned char)(HL7019D_CON8),
			&val,
			(unsigned char)(HL7019D_CON8_DPM_STAT_MASK),
			(unsigned char)(HL7019D_CON8_DPM_STAT_SHIFT)
			);

	return val;
}

static void hl7019d_dpm_status_dump(void)
{
	unsigned int dpm_status;

	dpm_status = hl7019d_get_dpm_status();

	if(0x0 == dpm_status)
		pr_err("not in dpm.\n");
	else
		pr_err("in vindpm or ilimdpm.\n");
}

static unsigned int hl7019d_get_pg_status(void)
{
	unsigned int ret;
	unsigned char val;

	ret = bq25601_read_interface((unsigned char)(HL7019D_CON8),
			&val,
			(unsigned char)(HL7019D_CON8_PG_STAT_MASK),
			(unsigned char)(HL7019D_CON8_PG_STAT_SHIFT)
			);

	return val;
}

static void hl7019d_pg_status_dump(void)
{
	unsigned int pg_status;

	pg_status = hl7019d_get_pg_status();

	if(0x0 == pg_status)
		pr_err("power is not good.\n");
	else
		pr_err("power is good.\n");
}

static unsigned int hl7019d_get_therm_status(void)
{
	unsigned int ret;
	unsigned char val;

	ret = bq25601_read_interface((unsigned char)(HL7019D_CON8),
			&val,
			(unsigned char)(HL7019D_CON8_THERM_STAT_MASK),
			(unsigned char)(HL7019D_CON8_THERM_STAT_SHIFT)
			);

	return val;
}

static void hl7019d_therm_status_dump(void)
{
	unsigned int therm_status;

	therm_status = hl7019d_get_therm_status();

	if(0x0 == therm_status)
		pr_info("ic's thermal status is in normal.\n");
	else
		pr_info("ic is in thermal regulation.\n");
}

static unsigned int hl7019d_get_vsys_status(void)
{
	unsigned int ret;
	unsigned char val;

	ret = bq25601_read_interface((unsigned char)(HL7019D_CON8),
			&val,
			(unsigned char)(HL7019D_CON8_VSYS_STAT_MASK),
			(unsigned char)(HL7019D_CON8_VSYS_STAT_SHIFT)
			);

	return val;
}

static void hl7019d_vsys_status_dump(void)
{
	unsigned int vsys_status;

	vsys_status = hl7019d_get_vsys_status();

	if(0x0 == vsys_status)
		pr_info("ic is not in vsysmin regulation(BAT > VSYSMIN).\n");
	else
		pr_info("ic is in vsysmin regulation(BAT < VSYSMIN).\n");
}

static void hl7019d_charger_system_status(void)
{
	hl7019d_vin_status_dump();
	hl7019d_chrg_status_dump();
	hl7019d_dpm_status_dump();
	hl7019d_pg_status_dump();
	hl7019d_therm_status_dump();
	hl7019d_vsys_status_dump();
}

/* CON9 */
static void hl7019d_watchdog_fault_dump(unsigned char reg)
{
	unsigned int wtd_fault;
	unsigned char fault = reg;

	fault &= (HL7019D_CON9_WATCHDOG_FAULT_MASK << HL7019D_CON9_WATCHDOG_FAULT_SHIFT);
	wtd_fault = (fault >> HL7019D_CON9_WATCHDOG_FAULT_SHIFT);

	if(0x0 == wtd_fault)
		pr_info("i2c watchdog is normal.\n");
	else
		pr_err("i2c watchdog timer is expirate.\n");
}

static void hl7019d_otg_fault_dump(unsigned char reg)
{
	unsigned int otg_fault;
	unsigned char fault = reg;

	fault &= (HL7019D_CON9_OTG_FAULT_MASK << HL7019D_CON9_OTG_FAULT_SHIFT);
	otg_fault = (fault >> HL7019D_CON9_OTG_FAULT_SHIFT);

	if(0x0 == otg_fault)
		pr_info("the OTG function is fine.\n");
	else
		pr_err("the OTG function is error.\n");
}

static void hl7019d_chrg_fault_dump(unsigned char reg)
{
	unsigned int chrg_fault;
	unsigned char fault = reg;

	fault &= (HL7019D_CON9_CHRG_FAULT_MASK << HL7019D_CON9_CHRG_FAULT_SHIFT);
	chrg_fault = (fault >> HL7019D_CON9_CHRG_FAULT_SHIFT);

	switch(chrg_fault)
	{
		case 0:
			pr_info("the ic charging status is normal.\n");
			break;
		case 1:
			pr_info("the ic's input is fault.\n");
			break;
		case 2:
			pr_info("the ic's thermal is shutdown.\n");
			break;
		case 3:
			pr_info("the ic's charge safety timer is expirate.\n");
			break;
		default:
			pr_info("the ic's charge fault status is unkown.\n");
			break;
	}
}

static void hl7019d_bat_fault_dump(unsigned char reg)
{
	unsigned int bat_fault;
	unsigned char fault = reg;

	fault &= (HL7019D_CON9_BAT_FAULT_MASK << HL7019D_CON9_BAT_FAULT_SHIFT);
	bat_fault = (fault >> HL7019D_CON9_BAT_FAULT_SHIFT);

	if(0x0 == bat_fault)
		pr_info("battery is normal.\n");
	else
		pr_err("battery is in OVP.\n");
}

static void hl7019d_ntc_fault_dump(unsigned char reg)
{
	unsigned int ntc_fault;
	unsigned char fault = reg;

	fault &= (HL7019D_CON9_NTC_FAULT_MASK << HL7019D_CON9_NTC_FAULT_SHIFT);
	ntc_fault = (fault >> HL7019D_CON9_NTC_FAULT_SHIFT);

	switch(ntc_fault)
	{
		case 0:
			pr_info("the ic's body temperature is normal.\n");
			break;
		case 5:
			pr_info("the ic's body temperature is cold.\n");
			break;
		case 6:
			pr_info("the ic's body temperature is hot.\n");
			break;
		default:
			pr_info("the ic's body temperature is unknow.\n");
			break;
	}
}

static void hl7019d_charger_fault_status(void)
{
	unsigned char reg;
	int ret = 0;

	ret = bq25601_read_interface((unsigned char)(HL7019D_CON9), &reg,
			(unsigned char)HL7019D_CON9_ALL_FAULT_MASK,
			(unsigned char)HL7019D_CON9_ALL_FAULT_SHIFT);

	if (ret < 0) {
		pr_err("read FAULT REG fail\n");
		return;
	}

	pr_info("fault register: 0x%02x\n", reg);
	hl7019d_watchdog_fault_dump(reg);
	hl7019d_otg_fault_dump(reg);
	hl7019d_chrg_fault_dump(reg);
	hl7019d_bat_fault_dump(reg);
	hl7019d_ntc_fault_dump(reg);
}

/* CONA */
//vender info register

/* CONB */
void hl7019d_set_dis_sr_inchg(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CONB),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CONB_DIS_SR_INCHG_MASK),
				(unsigned char)(HL7019D_CONB_DIS_SR_INCHG_SHIFT)
				);
}

void hl7019d_set_tship(unsigned int val)
{
	if(val > 7) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CONB),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CONB_TSHIP_MASK),
				(unsigned char)(HL7019D_CONB_TSHIP_SHIFT)
				);
}

/* CONC */
void hl7019d_set_bat_comp(unsigned int val)
{
	if(val > 7) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CONC),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CONC_BAT_COMP_MASK),
				(unsigned char)(HL7019D_CONC_BAT_COMP_SHIFT)
				);
}

void hl7019d_set_bat_vclamp(unsigned int val)
{
	if(val > 7) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CONC),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CONC_BAT_VCLAMP_MASK),
				(unsigned char)(HL7019D_CONC_BAT_VCLAMP_SHIFT)
				);
}

void hl7019d_set_boost_9v_en(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_CONC),
				(unsigned char)(val),
				(unsigned char)(HL7019D_CONC_BOOST_9V_EN_MASK),
				(unsigned char)(HL7019D_CONC_BOOST_9V_EN_SHIFT)
				);
}

/* COND */
void hl7019d_set_disable_ts(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_COND),
				(unsigned char)(val),
				(unsigned char)(HL7019D_COND_DISABLE_TS_MASK),
				(unsigned char)(HL7019D_COND_DISABLE_TS_SHIFT)
				);
}

void hl7019d_set_vindpm_offset(unsigned int val)
{
	if(val > 1) {
		pr_err("parameter error.\n");
		return;
	}

	bq25601_config_interface((unsigned char)(HL7019D_COND),
				(unsigned char)(val),
				(unsigned char)(HL7019D_COND_VINDPM_OFFSET_MASK),
				(unsigned char)(HL7019D_COND_VINDPM_OFFSET_SHIFT)
				);
}
/* chager IC fundation functiong */
/*-------------------------------------------------------------------*/

/* charger operation's functions */

int hl7019d_enable_charging(struct charger_device *chg_dev, bool en)
{
	unsigned int status = 0;

	if(!chg_dev)
		return -EINVAL;

	if (en) {
		hl7019d_set_chg_config(0x1);
	} else {
		hl7019d_set_chg_config(0x0);
	}

	return status;
}

int hl7019d_get_current(struct charger_device *chg_dev, u32 *ichg)
{
	int status = 0;
	unsigned int array_size;
	unsigned char reg_value;

	if(!chg_dev)
		return -EINVAL;

	array_size = ARRAY_SIZE(CSTH);
	bq25601_read_interface(HL7019D_CON2, &reg_value,
		HL7019D_CON2_ICHG_MASK, HL7019D_CON2_ICHG_SHIFT);	/* charge current */
	*ichg = charging_value_to_parameter(CSTH, array_size, reg_value);

	return status;
}

int hl7019d_set_current(struct charger_device *chg_dev, u32 current_value)
{
	unsigned int status = 0;
	unsigned int set_chr_current;
	unsigned int array_size;
	unsigned char register_value;

	if(!chg_dev)
		return -EINVAL;

	pr_info("charge current setting value: %d.\n", current_value);
	if (current_value <= 500000) {
		hl7019d_set_ichg(0x0);
	} else {
		array_size = ARRAY_SIZE(CSTH);
		set_chr_current = bmt_find_closest_level(CSTH, array_size, current_value);
		pr_info("charge current finally setting value: %d.\n", set_chr_current);
		register_value = charging_parameter_to_value(CSTH, array_size, set_chr_current);
		hl7019d_set_ichg(register_value);
	}

	return status;
}

int hl7019d_get_cv_voltage(struct charger_device *chg_dev, u32 *cv)
{
	int status = 0;
	unsigned int array_size;
	unsigned char reg_value;

	if(!chg_dev)
		return -EINVAL;

	array_size = ARRAY_SIZE(VBAT_CVTH);
	bq25601_read_interface(HL7019D_CON4, &reg_value,
		HL7019D_CON4_VREG_MASK, HL7019D_CON4_VREG_SHIFT);
	*cv = charging_value_to_parameter(VBAT_CVTH, array_size, reg_value);

	return status;
}

int hl7019d_set_cv_voltage(struct charger_device *chg_dev, u32 cv)
{
	int status = 0;
	unsigned short int array_size;
	unsigned int set_cv_voltage;
	unsigned char  register_value;

	if(!chg_dev)
		return -EINVAL;

	pr_err("charge voltage setting value: %d.\n", cv);
	/*static kal_int16 pre_register_value; */
	array_size = ARRAY_SIZE(VBAT_CVTH);
	/*pre_register_value = -1; */
	set_cv_voltage = bmt_find_closest_level(VBAT_CVTH, array_size, cv);

	register_value =
	charging_parameter_to_value(VBAT_CVTH, array_size, set_cv_voltage);
	pr_info("charging_set_cv_voltage register_value=0x%x %d %d\n",
		register_value, cv, set_cv_voltage);
	hl7019d_set_vreg(register_value);

	return status;
}

int hl7019d_get_input_current(struct charger_device *chg_dev, u32 *aicr)
{
	unsigned int status = 0;
	unsigned int array_size;
	unsigned char register_value = 0;
	
	if(!chg_dev)
		return -EINVAL;

	array_size = ARRAY_SIZE(INPUT_CSTH);
	bq25601_read_interface(HL7019D_CON0, &register_value,
		HL7019D_CON0_IINLIM_MASK, HL7019D_CON0_IINLIM_SHIFT);
	*aicr = charging_value_to_parameter(INPUT_CSTH, array_size, register_value);

	pr_info("reg:0x%x, aicr:%d\n", register_value, *aicr);

	return status;
}

int hl7019d_set_input_current(struct charger_device *chg_dev, u32 current_value)
{
	unsigned int status = 0;
	unsigned int set_chr_current;
	unsigned int array_size;
	unsigned char register_value;
    pr_info("endy,%s()++\n",__func__);
	
    pr_info("endy,%s(),input current = %d.\n",__func__,current_value);

	if(!chg_dev)
		return -EINVAL;

	pr_info("charge input current setting value: %d.\n", current_value);
	if (current_value < 100000) {
		register_value = 0x0;
	} else {
		array_size = ARRAY_SIZE(INPUT_CSTH);
		set_chr_current = bmt_find_closest_level(INPUT_CSTH, array_size, current_value);
		pr_info("charge input current finally setting value: %d.\n", set_chr_current);
		register_value = charging_parameter_to_value(INPUT_CSTH, array_size, set_chr_current);
	}

	hl7019d_set_iinlim(register_value);
    pr_info("endy,%s()--\n",__func__);

	return status;
}

int hl7019d_get_termination_curr(struct charger_device *chg_dev, u32 *term_curr)
{
	unsigned int status = 0;
	unsigned int array_size;
	unsigned char register_value;

	if(!chg_dev)
		return -EINVAL;

	array_size = ARRAY_SIZE(ITERM_CSTH);
	bq25601_read_interface(HL7019D_CON3, &register_value,
		HL7019D_CON3_ITERM_MASK, HL7019D_CON3_ITERM_SHIFT);
	*term_curr = charging_value_to_parameter(ITERM_CSTH, array_size, register_value);

	return status;
}

int hl7019d_is_enabled(struct charger_device *chg_dev, bool *en)
{
	unsigned int state = 0;
	int ret = 0;

	*en = false;
	state = hl7019d_get_chrg_status();
	/* 0x1: pre-charge 0x2: fast charge */
	if (state == 0x1 || state == 0x2)
		*en = true;

	return ret;
}

int hl7019d_reset_watch_dog_timer(struct charger_device *chg_dev)
{
	unsigned int status = 0;

	if(!chg_dev)
		return -EINVAL;

	/* charger status polling */
	hl7019d_charger_system_status();
	hl7019d_charger_fault_status();

	hl7019d_set_i2cwatchdog_timer_reset(0x1);
	hl7019d_set_i2cwatchdog(0x1);	/* WDT 40s */
	hl7019d_set_en_safty_timer(0x0); //disable safty timer

	return status;
}

int hl7019d_get_vindpm_voltage(struct charger_device *chg_dev, u32 *uV)
{
	unsigned char vindpm_status = 0;
	unsigned char register_value = 0;
	unsigned int array_size;
	int vindpm = 0;
	int ret = 0;

	if(!chg_dev)
		return -EINVAL;

	ret = bq25601_read_interface(HL7019D_COND, &vindpm_status,
		HL7019D_COND_VINDPM_OFFSET_MASK, HL7019D_COND_VINDPM_OFFSET_SHIFT);
	if (ret < 0) {
		pr_err("read REG0D fail\n");
			return ret;
	}

	ret = bq25601_read_interface(HL7019D_CON0, &register_value,
		HL7019D_CON0_VINDPM_MASK, HL7019D_CON0_VINDPM_SHIFT);
	if (ret < 0) {
		pr_err("read REG00 fail\n");
			return ret;
	}

	if(1 == vindpm_status) {
		pr_info("enter high vindpm mode.\n");
		array_size = ARRAY_SIZE(VINDPM_HIGH_CVTH);
		vindpm = charging_value_to_parameter(VINDPM_HIGH_CVTH,
				array_size, register_value);
		*uV = vindpm;
	} else {
		pr_info("enter normal vindpm mode.\n");
		array_size = ARRAY_SIZE(VINDPM_NORMAL_CVTH);
		vindpm = charging_value_to_parameter(VINDPM_NORMAL_CVTH,
				array_size, register_value);
		*uV = vindpm;
	}

	return ret;
}

int hl7019d_set_vindpm_voltage(struct charger_device *chg_dev, u32 vindpm_vol)
{
	unsigned int status = 0;
	unsigned int array_size;
	unsigned int set_vindpm_vol;
	unsigned char register_value;

	pr_info("vindpm voltage setting value: %d.\n", vindpm_vol);

	if(!chg_dev)
		return -EINVAL;

	if (vindpm_vol > 7500000) {
		pr_info("enter high vindpm mode.\n");

		hl7019d_set_vindpm_offset(1);

		if(vindpm_vol < 8300000) {
			hl7019d_set_vindpm(0x0);
		} else {
			array_size = ARRAY_SIZE(VINDPM_HIGH_CVTH);
			set_vindpm_vol = bmt_find_closest_level(VINDPM_HIGH_CVTH, array_size, vindpm_vol);
			pr_info("vindpm voltage finally setting value: %d.\n", set_vindpm_vol);
			register_value = charging_parameter_to_value(VINDPM_HIGH_CVTH, array_size, set_vindpm_vol);
			hl7019d_set_vindpm(register_value);
		}
	} else {
		pr_info("enter normal vindpm mode.\n");

		hl7019d_set_vindpm_offset(0);

		if(vindpm_vol < 3500000) {
			hl7019d_set_vindpm(0x0);
		} else {
			array_size = ARRAY_SIZE(VINDPM_NORMAL_CVTH);
			set_vindpm_vol = bmt_find_closest_level(VINDPM_NORMAL_CVTH, array_size, vindpm_vol);
			pr_info("vindpm voltage finally setting value: %d.\n", set_vindpm_vol);
			register_value = charging_parameter_to_value(VINDPM_NORMAL_CVTH, array_size, set_vindpm_vol);
			hl7019d_set_vindpm(register_value);
		}
	}

	return status;
}

int hl7019d_is_safety_timer_enabled(struct charger_device *chg_dev, bool *en)
{
	unsigned int status = 0; 
	unsigned char register_value = 0;

	if(!chg_dev)
		return -EINVAL;
	
	bq25601_read_interface(HL7019D_CON5, &register_value,
		HL7019D_CON5_CHG_TIMER_MASK, HL7019D_CON5_CHG_TIMER_SHIFT);

	if(1 == register_value)
		*en = true;
	else
		*en = false;

	return status;
}

int hl7019d_enable_safety_timer(struct charger_device *chg_dev, bool en)
{
	unsigned int status = 0; 

	if(!chg_dev)
		return -EINVAL;

	if(true == en)
		hl7019d_set_en_safty_timer(0x1);
	else
		hl7019d_set_en_safty_timer(0x0);

	return status;
}

/*-------------------------------------------------------------------*/

int hl7019d_charger_ic_init(struct charger_device *chg_dev)
{
	unsigned int status = 0;

	if(!chg_dev)
		return -EINVAL;

	pr_info("++\n");
	// hl7019d_set_en_hiz(0);	 // enable IC
	hl7019d_set_vindpm(0xa);	 //vindpm 4.68V
	hl7019d_set_iinlim(0x0);	 //input current 100mA

	hl7019d_set_sys_min(0x5);  // sys min 3.5V
	hl7019d_set_boost_lim(0x0); //boost limit 1A

	hl7019d_set_bcold(0x0);		//boost cold threshold
	hl7019d_set_force_20pct(0x0);  //set as charge current as registers value

	hl7019d_set_iprechg(0x03);	//precharge current 512mA
	hl7019d_set_iterm(0x0);		//termination current 128mA

	hl7019d_set_vreg(0x2C);		//default charge voltage 4.208V
	hl7019d_set_batlowv(0x1);	//battery low 3.0V
	hl7019d_set_vrechg(0x0);		//recharge delta 100mV

	hl7019d_set_en_term(0x1);	//enable termination
	hl7019d_set_term_stat(0x1);	//match termination
/* Miki <BSP_CHARGER> <lin_zc> <20190509> modify for charger watchdog start */
	hl7019d_set_i2cwatchdog(0x0); //disable i2c watchdog
/* Miki <BSP_CHARGER> <lin_zc> <20190509> modify for charger watchdog end */
	hl7019d_set_en_safty_timer(0x0); //disable safty timer
	hl7019d_set_charge_timer(0x2); //safety charge time upto 12h

	hl7019d_set_boostv(0x7);		//boost voltage upto 4.998V
	hl7019d_set_bhot(0x2);		//boost hot threshold 65 centigrade degree
	hl7019d_set_treg(0x3);		//thermal regulation threadhold upto 120 centigrade degree

	hl7019d_set_dpdm_en(0x0);	//disble D+/D- detection
	hl7019d_set_tmr2x_en(0x1);
	hl7019d_set_ppfet_disable(0x0);	//enable Q4
	hl7019d_set_chrgfault_int_mask(0x0);		//disable charge fault intteruppt
	hl7019d_set_batfault_int_mask(0x0);		//disable battery fault intteruppt

	hl7019d_set_dis_sr_inchg(0x0);
	hl7019d_set_tship(0x0);		//shipping mode active delay

	hl7019d_set_bat_comp(0x0);		//disable battery compensation
	hl7019d_set_bat_vclamp(0x0);		//disable clamp
	hl7019d_set_boost_9v_en(0x0);	//diaable boost 9V

	hl7019d_set_disable_ts(0x1);		//disable ts
	hl7019d_set_vindpm_offset(0x0);	//vindpm in normal value

	return status;
}



