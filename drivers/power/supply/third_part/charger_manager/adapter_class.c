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

#define pr_fmt(fmt)	"[cne-charge][adapter_class] %s: " fmt, __func__
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "adapter_class.h"

static struct class *adapter_class;
ATOMIC_NOTIFIER_HEAD(adapter_class_notifier);

static ssize_t adapter_show_name(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct adapter_device *adapter_dev = to_adapter_device(dev);

	return snprintf(buf, 20, "%s\n",
		       adapter_dev->props.alias_name ?
		       adapter_dev->props.alias_name : "anonymous");
}

#if 0
static int adapter_suspend(struct device *dev, pm_message_t state)
{
	struct adapter_device *adapter_dev = to_adapter_device(dev);

	if (adapter_dev->ops->suspend)
		return adapter_dev->ops->suspend(adapter_dev, state);

	return 0;
}

static int adapter_resume(struct device *dev)
{
	struct adapter_device *adapter_dev = to_adapter_device(dev);

	if (adapter_dev->ops->resume)
		return adapter_dev->ops->resume(adapter_dev);

	return 0;
}
#endif

static void adapter_device_release(struct device *dev)
{
	struct adapter_device *adapter_dev = to_adapter_device(dev);

	kfree(adapter_dev);
}

int adapter_dev_get_property(struct adapter_device *adapter_dev,
	enum adapter_property sta)
{
	if (adapter_dev != NULL && adapter_dev->ops != NULL &&
	    adapter_dev->ops->get_property)
		return adapter_dev->ops->get_property(adapter_dev, sta);

	return -ENOTSUPP;
}
EXPORT_SYMBOL(adapter_dev_get_property);

int adapter_dev_get_status(struct adapter_device *adapter_dev,
	struct adapter_status *sta)
{
	if (adapter_dev != NULL && adapter_dev->ops != NULL &&
	    adapter_dev->ops->get_status)
		return adapter_dev->ops->get_status(adapter_dev, sta);

	return -ENOTSUPP;
}
EXPORT_SYMBOL(adapter_dev_get_status);

int adapter_dev_get_output(struct adapter_device *adapter_dev, int *mV, int *mA)
{
	if (adapter_dev != NULL && adapter_dev->ops != NULL &&
	    adapter_dev->ops->get_output)
		return adapter_dev->ops->get_output(adapter_dev, mV, mA);

	return -ENOTSUPP;
}
EXPORT_SYMBOL(adapter_dev_get_output);

int adapter_dev_set_cap(struct adapter_device *adapter_dev,
	enum adapter_cap_type type,
	int mV, int mA)
{
	if (adapter_dev != NULL && adapter_dev->ops != NULL &&
	    adapter_dev->ops->set_cap)
		return adapter_dev->ops->set_cap(adapter_dev, type, mV, mA);

	return -ENOTSUPP;
}
EXPORT_SYMBOL(adapter_dev_set_cap);


int adapter_dev_get_cap(struct adapter_device *adapter_dev,
	enum adapter_cap_type type,
	struct adapter_power_cap *cap)
{
	if (adapter_dev != NULL && adapter_dev->ops != NULL &&
		adapter_dev->ops->get_cap)
		return adapter_dev->ops->get_cap(adapter_dev, type, cap);

	return -ENOTSUPP;
}
EXPORT_SYMBOL(adapter_dev_get_cap);


static DEVICE_ATTR(name, 0444, adapter_show_name, NULL);

static struct attribute *adapter_class_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group adapter_group = {
	.attrs = adapter_class_attrs,
};

static const struct attribute_group *adapter_groups[] = {
	&adapter_group,
	NULL,
};

int register_adapter_device_notifier(struct adapter_device *adapter_dev,
				struct notifier_block *nb)
{
	int ret;

	ret = srcu_notifier_chain_register(&adapter_dev->evt_nh, nb);
	return ret;
}
EXPORT_SYMBOL(register_adapter_device_notifier);

int unregister_adapter_device_notifier(struct adapter_device *adapter_dev,
				struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&adapter_dev->evt_nh, nb);
}
EXPORT_SYMBOL(unregister_adapter_device_notifier);

int adapter_class_reg_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&adapter_class_notifier, nb);
}
EXPORT_SYMBOL(adapter_class_reg_notifier);

int adapter_class_unreg_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&adapter_class_notifier, nb);
}
EXPORT_SYMBOL(adapter_class_unreg_notifier);

static void adapter_class_changed_work(struct work_struct *work)
{
	unsigned long flags;
	struct adapter_device *adp_dev = container_of(work, struct adapter_device,
						changed_work);

	spin_lock_irqsave(&adp_dev->changed_lock, flags);

	if (likely(adp_dev->changed)) {
		adp_dev->changed = false;
		spin_unlock_irqrestore(&adp_dev->changed_lock, flags);
		atomic_notifier_call_chain(&adapter_class_notifier,
			ADAPTER_EVENT_PROP_CHANGED, adp_dev);
		spin_lock_irqsave(&adp_dev->changed_lock, flags);
	}

	spin_unlock_irqrestore(&adp_dev->changed_lock, flags);
}

static void adapter_class_changed(struct adapter_device *adapter_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter_dev->changed_lock, flags);
	adapter_dev->changed = true;
	spin_unlock_irqrestore(&adapter_dev->changed_lock, flags);

	schedule_work(&adapter_dev->changed_work);
}

/**
 * adapter_device_register - create and register a new object of
 *   adapter_device class.
 * @name: the name of the new object
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use.
 * The methods may retrieve it by using adapter_get_data(adapter_dev).
 * @ops: the charger operations structure.
 *
 * Creates and registers new charger device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct adapter_device *adapter_device_register(const char *name,
		struct device *parent, void *devdata,
		const struct adapter_ops *ops,
		const struct adapter_properties *props)
{
	struct adapter_device *adapter_dev = NULL;
	static struct lock_class_key key;
	struct srcu_notifier_head *head = NULL;
	int rc;
	char *adapter_name = NULL;

	pr_notice("%s: name=%s\n", __func__, name);
	adapter_dev = kzalloc(sizeof(*adapter_dev), GFP_KERNEL);
	if (!adapter_dev)
		return ERR_PTR(-ENOMEM);

	head = &adapter_dev->evt_nh;
	srcu_init_notifier_head(head);
	/* Rename srcu's lock to avoid LockProve warning */
	lockdep_init_map(&(&head->srcu)->dep_map, name, &key, 0);
	mutex_init(&adapter_dev->ops_lock);
	INIT_WORK(&adapter_dev->changed_work, adapter_class_changed_work);
	adapter_dev->dev.class = adapter_class;
	adapter_dev->dev.parent = parent;
	adapter_dev->dev.release = adapter_device_release;
	adapter_name = kasprintf(GFP_KERNEL, "%s", name);
	dev_set_name(&adapter_dev->dev, adapter_name);
	dev_set_drvdata(&adapter_dev->dev, devdata);
	kfree(adapter_name);

	/* Copy properties */
	if (props) {
		memcpy(&adapter_dev->props, props,
		       sizeof(struct adapter_properties));
	}
	rc = device_register(&adapter_dev->dev);
	if (rc) {
		kfree(adapter_dev);
		return ERR_PTR(rc);
	}
	adapter_dev->ops = ops;

	adapter_class_changed(adapter_dev);

	return adapter_dev;
}
EXPORT_SYMBOL(adapter_device_register);

/**
 * adapter_device_unregister - unregisters a switching charger device
 * object.
 * @adapter_dev: the switching charger device object to be unregistered
 * and freed.
 *
 * Unregisters a previously registered via adapter_device_register object.
 */
void adapter_device_unregister(struct adapter_device *adapter_dev)
{
	if (!adapter_dev)
		return;

	mutex_lock(&adapter_dev->ops_lock);
	adapter_dev->ops = NULL;
	mutex_unlock(&adapter_dev->ops_lock);
	device_unregister(&adapter_dev->dev);
}
EXPORT_SYMBOL(adapter_device_unregister);


static int adapter_match_device_by_name(struct device *dev,
	const void *data)
{
	const char *name = data;

	return strcmp(dev_name(dev), name) == 0;
}

struct adapter_device *get_adapter_by_name(const char *name)
{
	struct device *dev = NULL;

	if (!name)
		return (struct adapter_device *)NULL;
	dev = class_find_device(adapter_class, NULL, name,
				adapter_match_device_by_name);

	return dev ? to_adapter_device(dev) : NULL;

}
EXPORT_SYMBOL(get_adapter_by_name);

static void __exit adapter_class_exit(void)
{
	class_destroy(adapter_class);
}

static int __init adapter_class_init(void)
{
	pr_err("++ adapter_class_init\n");
	adapter_class = class_create(THIS_MODULE, "Charging Adapter");
	if (IS_ERR(adapter_class)) {
		pr_notice("Unable to create Charging Adapter class; errno = %ld\n",
			PTR_ERR(adapter_class));
		return PTR_ERR(adapter_class);
	}
	adapter_class->dev_groups = adapter_groups;
#if 0
	adapter_class->suspend = adapter_suspend;
	adapter_class->resume = adapter_resume;
#endif
	pr_err("-- adapter_class_init\n");
	return 0;
}

subsys_initcall(adapter_class_init);
module_exit(adapter_class_exit);

MODULE_DESCRIPTION("Adapter Class Device");
MODULE_AUTHOR("Chino-e <chinoe@chino-e.com>");
MODULE_LICENSE("GPL");

