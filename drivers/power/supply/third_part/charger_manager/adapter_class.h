/*
 * Copyright (C) 2015 Chino-e Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef LINUX_POWER_ADAPTER_CLASS_H
#define LINUX_POWER_ADAPTER_CLASS_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>

#define ADAPTER_CAP_MAX_NR 10

struct adapter_power_cap {
	uint8_t selected_cap_idx;
	uint8_t nr;
	uint8_t pdp;
	uint8_t pwr_limit[ADAPTER_CAP_MAX_NR];
	int max_mv[ADAPTER_CAP_MAX_NR];
	int min_mv[ADAPTER_CAP_MAX_NR];
	int ma[ADAPTER_CAP_MAX_NR];
	int maxwatt[ADAPTER_CAP_MAX_NR];
	int minwatt[ADAPTER_CAP_MAX_NR];
	uint8_t type[ADAPTER_CAP_MAX_NR];
	int info[ADAPTER_CAP_MAX_NR];
};

enum adapter_class_notifier_events {
	ADAPTER_EVENT_PROP_CHANGED,
};

enum adapter_type {
	CNE_PD_ADAPTER,
};

enum adapter_event {
	CNE_PD_CONNECT_NONE,
	CNE_PD_CONNECT_HARD_RESET,
	CNE_PD_CONNECT_PE_READY_SNK,
	CNE_PD_CONNECT_PE_READY_SNK_PD30,
	CNE_PD_CONNECT_PE_READY_SNK_APDO,
	CNE_PD_CONNECT_TYPEC_ONLY_SNK,
	CNE_TYPEC_WD_STATUS,
	CNE_TYPEC_HRESET_STATUS,
	CNE_TYPEC_VBUS_STATUS,
};

enum adapter_property {
	TYPEC_RP_LEVEL,
	TYPE_CC_MODE,
	TYPE_CC_ORIENTATION,
};

enum adapter_cap_type {
	CNE_PD_APDO_START,
	CNE_PD_APDO_END,
	CNE_PD,
	CNE_PD_APDO,
	CNE_CAP_TYPE_UNKNOWN,
};

enum adapter_return_value {
	CNE_ADAPTER_OK = 0,
	CNE_ADAPTER_NOT_SUPPORT,
	CNE_ADAPTER_TIMEOUT,
	CNE_ADAPTER_REJECT,
	CNE_ADAPTER_ERROR,
	CNE_ADAPTER_ADJUST,
};


struct adapter_status {
	int temperature;
	bool ocp;
	bool otp;
	bool ovp;
};

struct adapter_properties {
	const char *alias_name;
};

struct adapter_device {
	struct adapter_properties props;
	const struct adapter_ops *ops;
	struct mutex ops_lock;
	struct device dev;
	struct srcu_notifier_head evt_nh;
	void	*driver_data;
	struct work_struct changed_work;
	spinlock_t changed_lock;
	bool changed;
};

struct adapter_ops {
	int (*suspend)(struct adapter_device *dev, pm_message_t state);
	int (*resume)(struct adapter_device *dev);
	int (*get_property)(struct adapter_device *dev,
		enum adapter_property pro);
	int (*get_status)(struct adapter_device *dev,
		struct adapter_status *sta);
	int (*set_cap)(struct adapter_device *dev, enum adapter_cap_type type,
		int mV, int mA);
	int (*get_cap)(struct adapter_device *dev, enum adapter_cap_type type,
		struct adapter_power_cap *cap);
	int (*get_output)(struct adapter_device *dev, int *mV, int *mA);

};

static inline void *adapter_dev_get_drvdata(
	const struct adapter_device *adapter_dev)
{
	return adapter_dev->driver_data;
}

static inline void adapter_dev_set_drvdata(
	struct adapter_device *adapter_dev, void *data)
{
	adapter_dev->driver_data = data;
}

struct adapter_device *adapter_device_register(
	const char *name,
	struct device *parent, void *devdata, const struct adapter_ops *ops,
	const struct adapter_properties *props);
void adapter_device_unregister(
	struct adapter_device *adapter_dev);
struct adapter_device *get_adapter_by_name(
	const char *name);

int adapter_class_unreg_notifier(struct notifier_block *nb);
int adapter_class_reg_notifier(struct notifier_block *nb);

#define to_adapter_device(obj) container_of(obj, struct adapter_device, dev)


int adapter_dev_get_property(struct adapter_device *adapter_dev,
	enum adapter_property sta);
int adapter_dev_get_status(struct adapter_device *adapter_dev,
	struct adapter_status *sta);
int adapter_dev_get_output(struct adapter_device *adapter_dev,
	int *mV, int *mA);
int adapter_dev_set_cap(struct adapter_device *adapter_dev,
	enum adapter_cap_type type,
	int mV, int mA);
int adapter_dev_get_cap(struct adapter_device *adapter_dev,
	enum adapter_cap_type type,
	struct adapter_power_cap *cap);


#endif /*LINUX_POWER_ADAPTER_CLASS_H*/

