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

#ifndef __CNE_CHARGER_INTF_H__
#define __CNE_CHARGER_INTF_H__

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/alarmtimer.h>
#include <linux/power_supply.h>
#include "charger_type.h"
#include "cne_charger.h"
#include "charger_class.h"

struct charger_manager;
struct charger_data;

#include "cne_pdc_intf.h"
#include "adapter_class.h"

#define CHARGING_INTERVAL 10
#define CHARGING_FULL_INTERVAL 20
#define CHARGING_HVDCP_INTERVAL 5

#define CHRLOG_ERROR_LEVEL   1
#define CHRLOG_DEBUG_LEVEL   2

#define ADAPTER_DPDM_HIZ    0
#define ADAPTER_5V          5
#define ADAPTER_9V          9
#define ADAPTER_12V         12
#define ADAPTER_V_TO_UV     1000000
#define ADAPTER_V_TO_MV     1000
#define ADAPTER_MV_TO_UV    1000
#define ADAPTER_5V_INTERVAL_LOW 4500
#define ADAPTER_5V_INTERVAL_HIGH 5500
#define ADAPTER_9V_INTERVAL_LOW 7500
#define ADAPTER_9V_INTERVAL_HIGH 10500

#define CHARGER_DEV_ICHG_STEP              60000    // 60mA
#define CHARGER_DEV_IINDPM_STEP            100000   // 100mA
#define CHARGER_CURRENT_ADJUST_STEP        300000   // 300mA

extern int chr_get_debug_level(void);

#define chr_err(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

#define chr_info(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice_ratelimited(fmt, ##args);		\
	}							\
} while (0)

#define chr_debug(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_DEBUG_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

#define CHR_CC		(0x0001)
#define CHR_TOPOFF	(0x0002)
#define CHR_TUNING	(0x0003)
#define CHR_POSTCC	(0x0004)
#define CHR_BATFULL	(0x0005)
#define CHR_ERROR	(0x0006)
#define CHR_HVDCP	(0x000A)
#define CHR_PDC		(0x000B)

/* charging abnormal status */
#define CHG_VBUS_OV_STATUS	(1 << 0)
#define CHG_BAT_OT_STATUS	(1 << 1)
#define CHG_OC_STATUS		(1 << 2)
#define CHG_BAT_OV_STATUS	(1 << 3)
#define CHG_ST_TMO_STATUS	(1 << 4)
#define CHG_BAT_LT_STATUS	(1 << 5)
#define CHG_TYPEC_WD_STATUS	(1 << 6)

/* charger_algorithm notify charger_dev */
enum {
	EVENT_EOC,
	EVENT_RECHARGE,
};

/* charger_dev notify charger_manager */
enum {
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_EOC,
	CHARGER_DEV_NOTIFY_RECHG,
	CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT,
	CHARGER_DEV_NOTIFY_VBATOVP_ALARM,
	CHARGER_DEV_NOTIFY_VBUSOVP_ALARM,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_VOUTOVP,
	CHARGER_DEV_NOTIFY_VDROVP,
};

/*
 * Software JEITA
 * T0: 0 degree Celsius
 * T1: 10 degree Celsius
 * T2: 15 degree Celsius
 * T3: 45 degree Celsius
 * T4: 50 degree Celsius
 */
enum sw_jeita_state_enum {
	TEMP_BELOW_T0 = 0,
	TEMP_T0_TO_T1,
	TEMP_T1_TO_T2,
	TEMP_T2_TO_T3,
	TEMP_T3_TO_T4,
	TEMP_ABOVE_T4
};

struct sw_jeita_data {
	int sm;
	int pre_sm;
	int cv;
	int cur;
	bool charging;
	bool error_recovery_flag;
};

struct chg_time_param {
	int chg_time;
	int ibat_max;
};

struct chg_temp_compensation {
	int temp_comp;
	int ibat_min;
};

/* battery thermal protection */
enum bat_temp_state_enum {
	BAT_TEMP_LOW = 0,
	BAT_TEMP_NORMAL,
	BAT_TEMP_HIGH
};

struct battery_thermal_protection_data {
	int sm;
	bool enable_min_charge_temp;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
};

struct charger_custom_data {
	int battery_cv;	/* uv */
	int max_charger_voltage;
	int max_charger_voltage_setting;
	int min_charger_voltage;

	int usb_charger_current_suspend;
	int usb_charger_current_unconfigured;
	int usb_charger_current_configured;
	int usb_charger_current;
	int ac_charger_current;
	int ac_charger_input_current;
	int non_std_ac_charger_current;
	int charging_host_charger_current;
	int apple_1_0a_charger_current;
	int apple_2_1a_charger_current;
	int ta_ac_charger_current;
	int pd_charger_current;
	int pd_charger_input_current;

	/* hvdcp */
	int hvdcp_charger_current;
	int hvdcp_charger_input_current;
	int hvdcp_polling_interval;
	int hvdcp_exit_soc;
	int hvdcp_exit_temp_h;
	int hvdcp_exit_temp_l;
	int hvdcp_exit_curr_ma;

	/* dynamic mivr */
	int min_charger_voltage_1;
	int min_charger_voltage_2;
	int max_dmivr_charger_current;

	/* recharge */
	bool enable_software_control_recharge;
	int recharge_voltage_threshold;
	int recharge_soc_threshold;

	/* sw jeita */
	int jeita_temp_above_t4_cv;
	int jeita_temp_t3_to_t4_cv;
	int jeita_temp_t2_to_t3_cv;
	int jeita_temp_t1_to_t2_cv;
	int jeita_temp_t0_to_t1_cv;
	int jeita_temp_below_t0_cv;
	int jeita_temp_above_t4_cur;
	int jeita_temp_t3_to_t4_cur;
	int jeita_temp_t2_to_t3_cur;
	int jeita_temp_t1_to_t2_cur;
	int jeita_temp_t0_to_t1_cur;
	int jeita_temp_below_t0_cur;
	int temp_t4_thres;
	int temp_t4_thres_minus_x_degree;
	int temp_t3_thres;
	int temp_t3_thres_minus_x_degree;
	int temp_t2_thres;
	int temp_t2_thres_plus_x_degree;
	int temp_t1_thres;
	int temp_t1_thres_plus_x_degree;
	int temp_t0_thres;
	int temp_t0_thres_plus_x_degree;

	/* battery temperature protection */
	int cne_temperature_recharge_support;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;

	/* dual charger */
	u32 chg1_ta_ac_charger_current;
	u32 chg2_ta_ac_charger_current;
	int slave_mivr_diff;
	u32 dual_polling_ieoc;

	/* slave charger */
	int chg2_eff;
	bool parallel_vbus;

	/* cable measurement impedance */
	int cable_imp_threshold;
	int vbat_cable_imp_threshold;

	/* bif */
	int bif_threshold1;	/* uv */
	int bif_threshold2;	/* uv */
	int bif_cv_under_threshold2;	/* uv */

	/* power path */
	bool power_path_support;

	int max_charging_time; /* second */

	int bc12_charger;
	int emergency_shutdown_vbat;

	/* pd */
	int pd_vbus_upper_bound;
	int pd_vbus_low_bound;
	int pd_ichg_level_threshold;
	int pd_stop_battery_soc;

	int vsys_watt;
	int ibus_err;
};

struct charger_data {
	int force_charging_current;
	int thermal_input_current_limit;
	int thermal_charging_current_limit;
	int user_input_current_limit;
	int user_charging_current_limit;
	int input_current_limit;
	int charging_current_limit;
	int disable_charging_count;
	int input_current_limit_by_aicl;
	int junction_temp_min;
	int junction_temp_max;
};

struct charger_manager {
	bool init_done;
	const char *algorithm_name;
	struct platform_device *pdev;
	void	*algorithm_data;
	int usb_state;
	bool usb_unlimited;
	bool disable_charger;

	int  capacity_ctrl;
	bool is_demo_mode;

	/* iio func add */
	unsigned int		nchannels;
	struct iio_chan_spec	*iio_chan_ids;
	int	pd_active;
	bool pd_hard_reset;
	int pd_voltage_min;
	int pd_voltage_max;
	int pd_current_max;
	int pd_usb_suspend_supported;

	struct power_supply	*manager_psy;
	struct power_supply_desc manager_desc;
	struct power_supply_config manager_cfg;
	struct power_supply	*charger_psy;
	struct power_supply	*batt_psy;
	struct power_supply	*usb_psy;
	struct power_supply	*ac_psy;

	struct notifier_block chg_cls_nb;
	struct notifier_block adp_cls_nb;

	struct charger_device *chg1_dev;
	struct notifier_block chg1_nb;
	struct charger_data chg1_data;
	struct charger_consumer *chg1_consumer;

	struct charger_device *chg2_dev;
	struct notifier_block chg2_nb;
	struct charger_data chg2_data;
	struct charger_consumer *chg2_consumer;

	struct adapter_device *pd_adapter;
	struct adapter_device *typec_adapter;

	enum charger_type chr_type;
	bool can_charging;
	int cable_out_cnt;

	int (*do_algorithm)(struct charger_manager *cm);
	int (*plug_in)(struct charger_manager *cm);
	int (*plug_out)(struct charger_manager *cm);
	int (*do_charging)(struct charger_manager *cm, bool en);
	int (*do_event)(struct notifier_block *nb, unsigned long ev, void *v);
	int (*change_current_setting)(struct charger_manager *cm);
	int (*kick_watchdog)(struct charger_manager *cm);

	/* notify charger user */
	struct srcu_notifier_head evt_nh;
	/* receive from battery */
	struct notifier_block psy_nb;

	/* common info */
	int battery_temp;

	/* sw jeita */
	bool enable_sw_jeita;
	struct sw_jeita_data sw_jeita;

	/* thermal */
	int			system_temp_level;
	int			*thermal_mitigation;
	int			thermal_levels;
	struct chg_time_param	*time_param;
	int			time_param_len;
	struct chg_temp_compensation *temp_comp;
	int         temp_comp_len;

	/* dynamic_cv */
	bool enable_dynamic_cv;

	bool enable_hiz;
	bool cmd_discharging;
	bool safety_timeout;
	bool vbusov_stat;
	bool aging_enable;
	unsigned int cap_low;
	unsigned int cap_high;

	/* battery warning */
	unsigned int notify_code;
	unsigned int notify_test_mode;

	/* battery thermal protection */
	struct battery_thermal_protection_data thermal;

	/* dtsi custom data */
	struct charger_custom_data data;

	bool enable_sw_safety_timer;
	bool sw_safety_timer_setting;

	/* High voltage charging */
	bool enable_hv_charging;

	/* type-C*/
	bool enable_type_c;

	/* water detection */
	bool water_detected;

	/* qc2.0 */
	bool leave_qc20;
	bool is_dual_charger_enable;
	bool is_doing_hvdcp_check;
	int  hvdcp_err_count;
	int  hvdcp_input_currnet;
	int  hvdcp_charging_current;

	/* pd */
	bool leave_pdc;
	struct cne_pdc pdc;
	bool disable_pd_dual;

	int pd_type;
	bool pd_reset;

	/* thread related */
	struct hrtimer charger_kthread_timer;

	/* alarm timer */
	struct alarm charger_timer;
	struct timespec64 endtime;
	bool is_suspend;

	struct wakeup_source *charger_wakelock;
	struct mutex charger_lock;
	struct mutex charger_pd_lock;
	struct mutex cable_out_lock;
	spinlock_t slock;
	unsigned int polling_interval;
	bool charger_thread_timeout;
	wait_queue_head_t  wait_que;
	bool charger_thread_polling;

	/* kpoc */
	atomic_t enable_kpoc_shdn;

	/* ATM */
	bool atm_enabled;

	/* power off mode */
	bool power_off_charging;

	/* dynamic mivr */
	bool enable_dynamic_mivr;

	/* extcon for VBUS / ID notification to USB for uUSB */
	struct extcon_dev	*extcon;

	/*daemon related*/
	struct sock *daemo_nl_sk;
	u_int g_scd_pid;
	// struct scd_cmd_param_t_1 sc_data;

	// fixes the kernel BUG warning
	struct work_struct	battery_info_change_work;

};

/* charger related module interface */
int charger_manager_notifier(struct charger_manager *info, int event);
int cne_switch_charging_init(struct charger_manager *info);
int cne_switch_charging_init2(struct charger_manager *info);
int cne_dual_switch_charging_init(struct charger_manager *info);
int cne_linear_charging_init(struct charger_manager *info);
void _wake_up_charger(struct charger_manager *info);
int cne_get_dynamic_cv(struct charger_manager *info, unsigned int *cv);
bool is_dual_charger_supported(struct charger_manager *info);
int charger_enable_vbus_ovp(struct charger_manager *pinfo, bool enable);
bool is_typec_adapter(struct charger_manager *info);

void notify_adapter_event(enum adapter_type type, enum adapter_event evt,
	void *val);


/* FIXME */
enum usb_state_enum {
	USB_SUSPEND = 0,
	USB_UNCONFIGURED,
	USB_CONFIGURED
};

bool __attribute__((weak)) is_usb_rdy(void)
{
	pr_info("%s is not defined\n", __func__);
	return false;
}

/* procfs */
#define PROC_FOPS_RW(name)						\
static int cne_chg_##name##_open(struct inode *node, struct file *file)	\
{									\
	return single_open(file, cne_chg_##name##_show, PDE_DATA(node));\
}									\
static const struct proc_ops cne_chg_##name##_fops = {		\
	.proc_open = cne_chg_##name##_open,					\
	.proc_read = seq_read,						\
	.proc_lseek = seq_lseek,						\
	.proc_release = single_release,					\
	.proc_write = cne_chg_##name##_write,				\
}

#define PROC_FOPS_RO(name)						\
static int cne_chg_##name##_open(struct inode *node, struct file *file)	\
{									\
	return single_open(file, cne_chg_##name##_show, PDE_DATA(node));\
}									\
static const struct proc_ops cne_chg_##name##_fops = {		\
	.proc_open = cne_chg_##name##_open,					\
	.proc_read = seq_read,						\
	.proc_lseek = seq_lseek,						\
	.proc_release = single_release,					\
}

#endif /* __CNE_CHARGER_INTF_H__ */
