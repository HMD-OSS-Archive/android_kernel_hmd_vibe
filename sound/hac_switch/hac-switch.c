#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

static int hac_switch_gpio = 0;
static int hac_switch_settings_state = 0; // is setting enable
static int hac_switch_receiver_state = 0; // is receiver working

typedef struct hac_switch_data {
	struct device *dev;
	int tx_gpio;
} hac_switch_data_t;

static ssize_t hac_switch_receiver_get(struct device *dev, struct device_attribute *attr, char *buf)
{
	int value = gpio_get_value(hac_switch_gpio);

	printk("%s +%d, gpio=%d, value=%d\n", __func__, __LINE__, hac_switch_gpio, value);

	return scnprintf(buf, PAGE_SIZE, "%i\n", value);
}

static ssize_t hac_switch_receiver_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int receiver_value = 0;

	sscanf(buf, "%d", &receiver_value);

	printk("%s +%d, gpio=%d, buffer=%s, value=%d\n", __func__, __LINE__, hac_switch_gpio, buf, receiver_value);
	if(hac_switch_receiver_state != receiver_value)
		if(hac_switch_settings_state == 1){
			gpio_set_value(hac_switch_gpio, receiver_value);
		}

	hac_switch_receiver_state = receiver_value;
	return count;
}
static DEVICE_ATTR(active, 0660, hac_switch_receiver_get, hac_switch_receiver_set);

static ssize_t hac_switch_settings_get(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("%s +%d, settings=%d\n", __func__, __LINE__, hac_switch_settings_state);

	return scnprintf(buf, PAGE_SIZE, "%i\n", hac_switch_settings_state);
}

static ssize_t hac_switch_settings_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int settings_value = 0;
	sscanf(buf, "%d", &settings_value);
	printk("%s +%d, buffer=%s, settings=%d\n", __func__, __LINE__, buf, settings_value);
	if(hac_switch_settings_state != settings_value)//setting changes
		if(hac_switch_receiver_state == 1)//enable/disable setting during phone call
			gpio_set_value(hac_switch_gpio, settings_value);

	hac_switch_settings_state = settings_value;

	return count;
}
static DEVICE_ATTR(settings, 0660, hac_switch_settings_get, hac_switch_settings_set);

static struct attribute *attributes[] = {
	&dev_attr_active.attr,
	&dev_attr_settings.attr,
	NULL
};

static const struct attribute_group attribute_group = {
	.attrs = attributes,
};

static int hac_switch_request_named_gpio( hac_switch_data_t *hsdata, const char *label, int *gpio)
{
	int rc = -1;
	struct device *dev = hsdata->dev;
	struct device_node *np = dev->of_node;

	rc = of_get_named_gpio(np, label, 0);
	if (rc < 0) {
		dev_err(dev, "failed to get '%s'\n", label);
		return rc;
	}
	*gpio = rc;

	rc = devm_gpio_request(dev, *gpio, label);
	if (rc) {
		dev_err(dev, "failed to request gpio %d\n", *gpio);
		return rc;
	}
	pr_info("%s +%d, request, label=%s, gpio=%d\n", __func__, __LINE__, label, *gpio);

	return 0;
}

static void hac_switch_gpio_free( hac_switch_data_t *hsdata)
{
	if (hsdata && hsdata->tx_gpio) {
		pr_info("%s +%d, free tx, gpio=%d\n", __func__, __LINE__, hsdata->tx_gpio);
		devm_gpio_free( hsdata->dev, hsdata->tx_gpio);
		hsdata->tx_gpio = 0;
	}
	return;
}

static int hac_switch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int rc = 0;
	hac_switch_data_t *hsdata = NULL;

	hsdata = devm_kzalloc(dev, sizeof(hac_switch_data_t), GFP_KERNEL);
	if (!hsdata) {
		dev_err(dev, "allocate memory failed\n");
		rc = -ENOMEM;
		goto failed_exit;
	}

	hsdata->dev = dev;
	platform_set_drvdata(pdev, hsdata);

	rc = hac_switch_request_named_gpio(hsdata, "ontim,hac-en-gpio",&hsdata->tx_gpio);
	if (rc) {
		dev_err(dev, "request gpio failed, rc=%d\n", rc);
		goto failed_exit;
	}

	hac_switch_gpio = hsdata->tx_gpio;

	gpio_direction_output(hac_switch_gpio, 0);
	pr_info("%s +%d, config out, gpio=%d, default=0\n", __func__, __LINE__, hac_switch_gpio);

	rc = sysfs_create_group(&dev->kobj, &attribute_group);
	if (rc) {
		dev_err(dev, "create sysfs failed, rc=%d\n", rc);
		goto failed_exit;
	}

	pr_info("%s +%d, hsdata probe success\n", __func__, __LINE__);

	return 0;

failed_exit:
	if( hsdata) {
		hac_switch_gpio_free(hsdata);
		kfree( hsdata);
		hsdata = NULL;
	}
	return rc;
}

static int hac_switch_remove(struct platform_device *pdev)
{
	hac_switch_data_t *hsdata = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &attribute_group);
	kfree( hsdata);
	hsdata = NULL;

	return 0;
}

static const struct of_device_id hac_switch_of_match[] = {
	{ .compatible = "ontim,hac-switch", },
	{}
};
MODULE_DEVICE_TABLE(of, hac_switch_of_match);

static struct platform_driver hac_switch_driver = {
	.driver = {
		.name   = "hac_switch",
		.owner  = THIS_MODULE,
		.of_match_table = hac_switch_of_match,
	},
	.probe  = hac_switch_probe,
	.remove = hac_switch_remove,
};

static int __init hac_switch_init(void)
{
	int rc = 0;

	rc = platform_driver_register(&hac_switch_driver);
	if (!rc) {
		pr_info("%s +%d, driver init success\n", __func__, __LINE__);
	} else {
		pr_err("%s %d, driver init failed, errno=%d\n", __func__, __LINE__, rc);
	}
	return rc;
}

static void __exit hac_switch_exit(void)
{
	platform_driver_unregister(&hac_switch_driver);
	pr_info("%s +%d, unregister\n", __func__, __LINE__);
}

module_init(hac_switch_init);
module_exit(hac_switch_exit);

MODULE_AUTHOR("bei.sha<bei.sha@chino-e.com>");
MODULE_DESCRIPTION("hac-switch driver");
MODULE_LICENSE("GPL");
