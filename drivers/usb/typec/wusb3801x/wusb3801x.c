/************************************************************************
 *
 *  WILLSEMI TypeC Chipset Driver for Linux & Android.
 *
 *
 * ######################################################################
 *
 *  Author: lei.huang (lhuang@sh-willsemi.com)
 *
 * Copyright (c) 2019, WillSemi Inc. All rights reserved.
 *
 ************************************************************************/

/************************************************************************
 *  Include files
 ************************************************************************/
#define pr_fmt(fmt)	"[cne-charge][wusb3801x] %s: " fmt, __func__

#define __TEST_CC_PATCH__
//#define __WUSB3801_ADD_PR_SWITCH__
#define __WUSB3801_ADD_ADAPTER__
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_wakeup.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/usb/typec.h>
#include <linux/workqueue.h>
#include <linux/extcon-provider.h>
#include <linux/pinctrl/consumer.h>
#if defined(__WUSB3801_ADD_ADAPTER__)
#include "adapter_class.h"
#endif

#ifdef IS_ERR_VALUE
#undef IS_ERR_VALUE
#endif
#define IS_ERR_VALUE(x) (x < 0)

/******************************************************************************
 * 
 * Bit operations if we don't want to include #include <linux/bitops.h>
 * 
 ******************************************************************************/
#undef  __CONST_FFS
#define __CONST_FFS(_x) \
        ((_x) & 0x0F ? ((_x) & 0x03 ? ((_x) & 0x01 ? 0 : 1) :\
                                      ((_x) & 0x04 ? 2 : 3)) :\
                       ((_x) & 0x30 ? ((_x) & 0x10 ? 4 : 5) :\
                                      ((_x) & 0x40 ? 6 : 7)))

#undef  FFS
#define FFS(_x) \
        ((_x) ? __CONST_FFS(_x) : 0)

#undef  BITS
#define BITS(_end, _start) \
        ((BIT(_end) - BIT(_start)) + BIT(_end))

#undef  __BITS_GET
#define __BITS_GET(_byte, _mask, _shift) \
        (((_byte) & (_mask)) >> (_shift))

#undef  BITS_GET
#define BITS_GET(_byte, _bit) \
        __BITS_GET(_byte, _bit, FFS(_bit))

#undef  __BITS_SET
#define __BITS_SET(_byte, _mask, _shift, _val) \
        (((_byte) & ~(_mask)) | (((_val) << (_shift)) & (_mask)))

#undef  BITS_SET
#define BITS_SET(_byte, _bit, _val) \
        __BITS_SET(_byte, _bit, FFS(_bit), _val)

#undef  BITS_MATCH
#define BITS_MATCH(_byte, _bit) \
        (((_byte) & (_bit)) == (_bit))

/******************************************************************************
 * 
 * Register Map
 * 
 ******************************************************************************/
#define WUSB3801_REG_VERSION_ID         0x01
#define WUSB3801_REG_CONTROL0           0x02
#define WUSB3801_REG_INTERRUPT          0x03
#define WUSB3801_REG_STATUS             0x04
#define WUSB3801_REG_CONTROL1           0x05
#define WUSB3801_REG_TEST0              0x06
#define WUSB3801_REG_TEST_01            0x07
#define WUSB3801_REG_TEST_02            0x08
#define WUSB3801_REG_TEST_03            0x09
#define WUSB3801_REG_TEST_04            0x0A
#define WUSB3801_REG_TEST_05            0x0B
#define WUSB3801_REG_TEST_06            0x0C
#define WUSB3801_REG_TEST_07            0x0D
#define WUSB3801_REG_TEST_08            0x0E
#define WUSB3801_REG_TEST_09            0x0F
#define WUSB3801_REG_TEST_0A            0x10
#define WUSB3801_REG_TEST_0B            0x11
#define WUSB3801_REG_TEST_0C            0x12
#define WUSB3801_REG_TEST_0D            0x13
#define WUSB3801_REG_TEST_0E            0x14
#define WUSB3801_REG_TEST_0F            0x15
#define WUSB3801_REG_TEST_10            0x16
#define WUSB3801_REG_TEST_11            0x17
#define WUSB3801_REG_TEST_12            0x18

#define WUSB3801_SLAVE_ADDR0            0xc0
#define WUSB3801_SLAVE_ADDR1            0xd0

#define WUSB3801_DRP_ACC                (BIT_REG_CTRL0_RLE_DRP)
#define WUSB3801_DRP                    (BIT_REG_CTRL0_RLE_DRP | BIT_REG_CTRL0_DIS_ACC)
#define WUSB3801_SNK_ACC                (BIT_REG_CTRL0_RLE_SNK)
#define WUSB3801_SNK                    (BIT_REG_CTRL0_RLE_SNK | BIT_REG_CTRL0_DIS_ACC)
#define WUSB3801_SRC_ACC                (BIT_REG_CTRL0_RLE_SRC)
#define WUSB3801_SRC                    (BIT_REG_CTRL0_RLE_SRC | BIT_REG_CTRL0_DIS_ACC)
#define WUSB3801_DRP_PREFER_SRC_ACC     (WUSB3801_DRP_ACC | BIT_REG_CTRL0_TRY_SRC)
#define WUSB3801_DRP_PREFER_SRC         (WUSB3801_DRP     | BIT_REG_CTRL0_TRY_SRC)
#define WUSB3801_DRP_PREFER_SNK_ACC     (WUSB3801_DRP_ACC | BIT_REG_CTRL0_TRY_SNK)
#define WUSB3801_DRP_PREFER_SNK         (WUSB3801_DRP     | BIT_REG_CTRL0_TRY_SNK)
#define WUSB3801_INIT_MODE              (WUSB3801_DRP_PREFER_SNK_ACC)
#define WUSB3801_VENDOR_ID              0x06

/******************************************************************************
 * 
 * Register BIT Mask
 * 
 ******************************************************************************/
/* (02H) control register */
#define BIT_REG_CTRL0_DIS_ACC           (0x01 << 7)
#define BIT_REG_CTRL0_TRY_SRC           (0x02 << 5)
#define BIT_REG_CTRL0_TRY_SNK           (0x01 << 5)
#define BIT_REG_CTRL0_CUR_DEF           (0x00 << 3)
#define BIT_REG_CTRL0_CUR_1P5           (0x01 << 3)
#define BIT_REG_CTRL0_CUR_3P0           (0x02 << 3)
#define BIT_REG_CTRL0_RLE_SNK           (0x00 << 1)
#define BIT_REG_CTRL0_RLE_SRC           (0x01 << 1)
#define BIT_REG_CTRL0_RLE_DRP           (0x02 << 1)
#define BIT_REG_CTRL0_INT_MSK           (0x01 << 0)

/* (04H) cc status : charging current as SNK */
#define BIT_REG_STATUS_VBUS             (0x01 << 7)
#define BIT_REG_STATUS_STANDBY          (0x00 << 5)
#define BIT_REG_STATUS_CUR_DEF          (0x01 << 5)
#define BIT_REG_STATUS_CUR_MID          (0x02 << 5)
#define BIT_REG_STATUS_CUR_HIGH         (0x03 << 5)

/* (04H) cc status : Plugged Port Status */
#define BIT_REG_STATUS_ATC_STB          (0x00 << 1)
#define BIT_REG_STATUS_ATC_SNK          (0x01 << 1)
#define BIT_REG_STATUS_ATC_SRC          (0x02 << 1)
#define BIT_REG_STATUS_ATC_ACC          (0x03 << 1)
#define BIT_REG_STATUS_ATC_DACC         (0x04 << 1)

/* (04H) cc status : Plug Orientation */
#define BIT_REG_STATUS_PLR_STB          (0x00 << 0)
#define BIT_REG_STATUS_PLR_CC1          (0x01 << 0)
#define BIT_REG_STATUS_PLR_CC2          (0x02 << 0)
#define BIT_REG_STATUS_PLR_BOTH         (0x03 << 0)

#define BIT_REG_CTRL1_SW02_DIN          (0x01 << 4)
#define BIT_REG_CTRL1_SW02_EN           (0x01 << 3)
#define BIT_REG_CTRL1_SW01_DIN          (0x01 << 2)
#define BIT_REG_CTRL1_SW01_EN           (0x01 << 1)
#define BIT_REG_CTRL1_SM_RST            (0x01 << 0)

#define BIT_REG_TEST02_FORCE_ERR_RCY    (0x01)

#define WUSB3801_WAIT_VBUS               0x40
/*Fixed duty cycle period. 40ms:40ms*/
#define WUSB3801_TGL_40MS                0
#define WUSB3801_HOST_DEFAULT            0
#define WUSB3801_HOST_1500MA             1
#define WUSB3801_HOST_3000MA             2
#define WUSB3801_INT_ENABLE              0x00
#define WUSB3801_INT_DISABLE             0x01
#define WUSB3801_DISABLED                0x0A
#define WUSB3801_ERR_REC                 0x01
#define WUSB3801_VBUS_OK                 0x80

/* (04H) cc status : Plug Orientation */
#define WUSB3801_SNK_0MA                (0x00 << 5)
#define WUSB3801_SNK_DEFAULT            (0x01 << 5)
#define WUSB3801_SNK_1500MA             (0x02 << 5)
#define WUSB3801_SNK_3000MA             (0x03 << 5)
#define WUSB3801_ATTACH                  0x1C

//#define WUSB3801_TYPE_PWR_ACC           (0x00 << 2) /*Ra/Rd treated as Open*/
/* (04H) cc status : Plugged Port Status */
#define WUSB3801_TYPE_INVALID           (0x00)
#define WUSB3801_TYPE_SNK               (0x01 << 2)
#define WUSB3801_TYPE_SRC               (0x02 << 2)
#define WUSB3801_TYPE_AUD_ACC           (0x03 << 2)
#define WUSB3801_TYPE_DBG_ACC           (0x04 << 2)

#define WUSB3801_INT_DETACH              (0x01 << 1)
#define WUSB3801_INT_ATTACH              (0x01 << 0)

#define WUSB3801_REV20                   0x02

/* Masks for Read-Modified-Write operations*/
#define WUSB3801_HOST_CUR_MASK           0x18  /*Host current for IIC*/
#define WUSB3801_INT_MASK                0x01
#define WUSB3801_BCLVL_MASK              0x60
#define WUSB3801_TYPE_MASK               0x1C
#define WUSB3801_CC_DIR_MASK             0x3
#define WUSB3801_CC_DIR_SHIFT            0x0
#define WUSB3801_MODE_MASK               0xE6  /*Roles relevant bits*/
#define WUSB3801_INT_STS_MASK            0x03
#define WUSB3801_FORCE_ERR_RCY_MASK      0x80  /*Force Error recovery*/
#define WUSB3801_ROLE_MASK               0x06
#define WUSB3801_VENDOR_ID_MASK          0x07
#define WUSB3801_VERSION_ID_MASK         0xF8
#define WUSB3801_VENDOR_SUB_ID_MASK         0xA0
#define WUSB3801_POLARITY_CC_MASK        0x03
#define WUSB3801_CC_STS_MASK            0x03

/* WUSB3801 STATES MACHINES */
#define WUSB3801_STATE_DISABLED             0x00
#define WUSB3801_STATE_ERROR_RECOVERY       0x01
#define WUSB3801_STATE_UNATTACHED_SNK       0x02
#define WUSB3801_STATE_UNATTACHED_SRC       0x03
#define WUSB3801_STATE_ATTACHWAIT_SNK       0x04
#define WUSB3801_STATE_ATTACHWAIT_SRC       0x05
#define WUSB3801_STATE_ATTACHED_SNK         0x06
#define WUSB3801_STATE_ATTACHED_SRC         0x07
#define WUSB3801_STATE_AUDIO_ACCESSORY      0x08
#define WUSB3801_STATE_DEBUG_ACCESSORY      0x09
#define WUSB3801_STATE_TRY_SNK              0x0A
#define WUSB3801_STATE_TRYWAIT_SRC          0x0B
#define WUSB3801_STATE_TRY_SRC              0x0C
#define WUSB3801_STATE_TRYWAIT_SNK          0x0D
#define WUSB3801_CC2_CONNECTED 1
#define WUSB3801_CC1_CONNECTED 0

/******************************************************************************
 * 
 * Macros and constants
 * 
 ******************************************************************************/
#define WUSB3801_WAKE_LOCK_TIMEOUT 1000	// wake lock timeout in ms
#define ROLE_SWITCH_TIMEOUT 1500 		// 1.5 Seconds timeout for force detection
#define DEBOUNCE_TIME_OUT 	50

static struct i2c_client *wusb3801_client = NULL;

/*Private data*/
typedef struct wusb3801_data
{
	uint32_t  int_gpio;
	uint8_t  init_mode;
	uint8_t  dfp_power;
	uint8_t  dttime;
}wusb3801_data_t;

struct wusb3801_wakeup_source {
        struct wakeup_source    *source;
        unsigned long           enabled_bitmap;
        spinlock_t              ws_lock;
};

typedef struct wusb3801_chip
{
	struct      i2c_client *client;
	struct      wusb3801_data *pdata;
	struct      workqueue_struct  *cc_wq;
	int         irq_gpio;
	int         ufp_power;
#ifdef 	__TEST_CC_PATCH__
	//uint8_t     reg4_val;
	uint8_t     cc_test_flag;
	uint8_t     cc_sts;
#endif	/* __TEST_CC_PATCH__ */
	uint8_t     mode;
	uint8_t     dev_id;
	uint8_t     dev_sub_id;
	uint8_t     type;
	uint8_t     state;
	uint8_t     bc_lvl;
	uint8_t     dfp_power;
	uint8_t     dttime;
	uint8_t     attached;
	uint8_t     init_state;
	uint8_t     defer_init;	//work round the additional interrupt caused by mode change
	int         try_attcnt;
	struct      work_struct dwork;
	struct wusb3801_wakeup_source    wusb3801_ws;
	struct      mutex mlock;
	struct pinctrl 			*wusb3801_pinctrl;
	struct pinctrl_state 	*gpio_state_active;
	struct pinctrl_state 	*gpio_state_suspend;
	struct typec_port *port;
	struct typec_capability typec_cap;
	struct typec_partner *partner;
#if defined(__WUSB3801_ADD_ADAPTER__)
	struct power_supply *charger_psy;
	const char *adapter_dev_name;
	struct adapter_device *adapter_dev;
#endif
} wusb3801_chip_t;

#define wusb3801_update_state(chip, st)                      \
	if(chip) {                                                 \
		chip->state = st;                                        \
		dev_info(&chip->client->dev, "%s: %s\n", __func__, #st); \
		wake_up_interruptible(&mode_switch);                     \
	}

#define STR(s)    #s
#define STRV(s)   STR(s)

//static int wusb3801_power_set_icurrent_max(struct wusb3801_chip *chip,
//						int icurrent);
static int wusb3801_reset_device(struct wusb3801_chip *chip);
static void wusb3801_detach(struct wusb3801_chip *chip);
//static unsigned wusb3801_is_vbus_on(struct wusb3801_chip *chip);
DECLARE_WAIT_QUEUE_HEAD(mode_switch);

/*************************************************************************
 * 
 * Refisters read and write
 * 
 ************************************************************************/
static int wusb3801_write_masked_byte(struct i2c_client *client,
					uint8_t addr, uint8_t mask, uint8_t val)
{
	int rc;
	if (!mask){
		/* no actual access */
		rc = -EINVAL;
		goto out;
	}
	rc = i2c_smbus_read_byte_data(client, addr);
	if (!IS_ERR_VALUE(rc)){
		rc = i2c_smbus_write_byte_data(client,
			addr, BITS_SET((uint8_t)rc, mask, val));
	}
out:
	return rc;
}

static int wusb3801_read_device_id(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_VERSION_ID);
	if (IS_ERR_VALUE(rc))
		return rc;
	dev_info(cdev, "VendorID register: 0x%02x\n", rc );
	if((rc & WUSB3801_VENDOR_ID_MASK) != WUSB3801_VENDOR_ID){
		return -EINVAL;
	}
	chip->dev_id = rc;
	dev_info(cdev, "Vendor id: 0x%02x, Version id: 0x%02x\n", rc & WUSB3801_VENDOR_ID_MASK,
	                                                         (rc & WUSB3801_VERSION_ID_MASK) >> 3);
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST_01);
	if (IS_ERR_VALUE(rc))
		return rc;
	chip->dev_sub_id = rc & WUSB3801_VENDOR_SUB_ID_MASK;
	dev_info(cdev, "VendorSUBID register: 0x%02x\n", rc & WUSB3801_VENDOR_SUB_ID_MASK );
	return 0;
}

static int wusb3801_update_status(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc;

	/* Get control0 register */
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_CONTROL0);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: fail to read mode\n", __func__);
		return rc;
	}

	chip->mode      =  rc & WUSB3801_MODE_MASK;
	chip->dfp_power =  BITS_GET(rc, WUSB3801_HOST_CUR_MASK);
	chip->dttime    =  WUSB3801_TGL_40MS;

	return 0;
}

static int wusb3801_check_modes(uint8_t mode)
{
	switch(mode){
		case WUSB3801_DRP_ACC            :
		case WUSB3801_DRP                :
		case WUSB3801_SNK_ACC            :
		case WUSB3801_SNK                :
		case WUSB3801_SRC_ACC            :
		case WUSB3801_SRC                :
		case WUSB3801_DRP_PREFER_SRC_ACC :
		case WUSB3801_DRP_PREFER_SRC     :
		case WUSB3801_DRP_PREFER_SNK_ACC :
		case WUSB3801_DRP_PREFER_SNK     :
			return 0;
			break;
		default:
			break;
	}
	return -EINVAL;
}

/******************************************************************************
 * 
 * Support PSY
 * 
 ******************************************************************************/
#if defined(__WUSB3801_ADD_ADAPTER__)
static int wusb3801_notify_typec_init_done(struct wusb3801_chip *chip)
{
	int ret = 0;
	union power_supply_propval propval;

	pr_err("set init done!\n");
	/* Get chg type det power supply */
	if (!chip->charger_psy) {
		chip->charger_psy = power_supply_get_by_name("charger");
		if (!chip->charger_psy) {
			pr_err("charger psy get fail\n");
			return -EINVAL;
		}
	}

	propval.intval = 1;
	ret = power_supply_set_property(chip->charger_psy,
				POWER_SUPPLY_PROP_PRESENT, &propval);
	if (ret < 0)
		pr_err("Set psy present fail(%d)\n", ret);
	else
		pr_info("Typec init done!\n");

	return ret;
}

static int wusb3801_notify_typec_cc_changed(struct wusb3801_chip *chip, int notify)
{
	int ret = 0;
	union power_supply_propval propval;

	pr_err("set CC change is %d\n", notify);
	/* Get chg type det power supply */
	if (!chip->charger_psy) {
		chip->charger_psy = power_supply_get_by_name("charger");
		if (!chip->charger_psy) {
			pr_err("charger psy get fail\n");
			return -EINVAL;
		}
	}

	propval.intval = notify;
	ret = power_supply_set_property(chip->charger_psy,
				POWER_SUPPLY_PROP_USB_TYPE, &propval);
	if (ret < 0)
		pr_err("psy online fail(%d)\n", ret);
	else
		pr_info("Type CC mode = %d\n", notify);

	return ret;
}

static int wusb3801_get_prop_typec_cc_orientation(struct wusb3801_chip *chip) {
	int rc = 0;

	mutex_lock(&chip->mlock);
	rc = i2c_smbus_read_byte_data(wusb3801_client, WUSB3801_REG_STATUS);
	mutex_unlock(&chip->mlock);
	if (IS_ERR_VALUE(rc)) {
		pr_err("%s: failed to read\n", __func__);
		return rc;
	}

	rc  =  BITS_GET(rc, WUSB3801_CC_STS_MASK);
#ifdef __TEST_CC_PATCH__
	if(rc == 0 && chip->cc_sts != 0xFF)
		rc = chip->cc_sts;
	else
		rc -= 1;
#endif

	return rc;
}

static int wusb3801_typec_get_property(struct adapter_device *dev, enum adapter_property sta)
{
	struct wusb3801_chip *chip;
	int ret;

	chip = (struct wusb3801_chip *)adapter_dev_get_drvdata(dev);
	if (chip == NULL)
		return -1;

	switch (sta) {
	case TYPEC_RP_LEVEL:
		{
			ret = 2;
		}
		break;

	case TYPE_CC_MODE:
		{
			if (chip->type & WUSB3801_TYPE_INVALID)
				ret = 0;
			else if (chip->type & WUSB3801_TYPE_SRC)
				ret = 1;
			else if (chip->type & WUSB3801_TYPE_SNK)
				ret = 2;
			else
				ret = 3;
			break;
		}
		break;

	case TYPE_CC_ORIENTATION:
		{
			ret = wusb3801_get_prop_typec_cc_orientation(chip);
		}
		break;

	default:
		{
			ret = -1;
		}
		break;
	}

	return ret;
}

static struct adapter_ops wusb3801_adapter_ops = {
	.get_property = wusb3801_typec_get_property,
};
#endif

/*************************************************************************
 * 
 * device feature function
 * 
 ************************************************************************/
static void wusb3801_wakeup_src_init(struct wusb3801_chip *chip)
{
	spin_lock_init(&chip->wusb3801_ws.ws_lock);
	//wakeup_source_init(&chip->wusb3801_ws.source, "wusb3801");
	chip->wusb3801_ws.source = wakeup_source_register(&chip->client->dev, "wusb3801");
}

/*
static int wusb3801_reset_chip_to_error_recovery(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	rc = i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_CONTROL1, BIT_REG_CTRL1_SM_RST);

	if (IS_ERR_VALUE(rc))
	{
		dev_err(cdev, "failed to write manual(%d)\n", rc);
	}

	return rc;
}
*/

static int wusb3801_set_chip_state(struct wusb3801_chip *chip, uint8_t state)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if(state > WUSB3801_STATE_UNATTACHED_SRC)
		return -EINVAL;

	rc = i2c_smbus_write_byte_data(chip->client,
			   WUSB3801_REG_CONTROL1,
			   (state == WUSB3801_STATE_DISABLED) ? \
			             WUSB3801_DISABLED :        \
			             0);

	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "failed to write state machine(%d)\n", rc);
	}

	chip->init_state = state;

	return rc;
}

static int wusb3801_set_mode(struct wusb3801_chip *chip, uint8_t mode)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (mode != chip->mode) {
		/*
		rc = wusb3801_write_masked_byte(chip->client,
				WUSB3801_REG_CONTROL0,
				WUSB3801_MODE_MASK,
				mode);
				*/
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_CONTROL0);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: fail to read mode\n", __func__);
		return rc;
	}
	rc &= ~WUSB3801_MODE_MASK;
	//rc |= mode;
	rc |= (mode | WUSB3801_INT_MASK);//Disable the chip interrupt
  rc = i2c_smbus_write_byte_data(chip->client,
			   WUSB3801_REG_CONTROL0, rc);

	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "failed to write mode(%d)\n", rc);
		return rc;
	}

	//Clear the chip interrupt
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_INTERRUPT);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: fail to clear chip interrupt\n", __func__);
		return rc;
	}

	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_CONTROL0);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: fail to read chip interrupt\n", __func__);
		return rc;
	}
	rc &= ~WUSB3801_INT_MASK;//enable the chip interrupt
	rc = i2c_smbus_write_byte_data(chip->client,
			   WUSB3801_REG_CONTROL0, rc);

	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "failed to enable chip interrupt(%d)\n", rc);
		return rc;
	}

	chip->mode = mode;
  }

 	dev_dbg(cdev, "%s: mode (0x%02x) (0x%02x)\n", __func__, chip->mode , mode);

	return rc;
}

static int wusb3801_set_dfp_power(struct wusb3801_chip *chip, uint8_t hcurrent)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (hcurrent == chip->dfp_power) {
		dev_dbg(cdev, "vaule is not updated(%d)\n",
					hcurrent);
		return rc;
	}

	rc = wusb3801_write_masked_byte(chip->client,
					WUSB3801_REG_CONTROL0,
					WUSB3801_HOST_CUR_MASK,
					hcurrent);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "failed to write current(%d)\n", rc);
		return rc;
	}

	chip->dfp_power = hcurrent;

	dev_dbg(cdev, "%s: host current(%d)\n", __func__, hcurrent);

	return rc;
}

static int wusb3801_init_force_dfp_power(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	rc = wusb3801_write_masked_byte(chip->client,
					WUSB3801_REG_CONTROL0,
					WUSB3801_HOST_CUR_MASK,
					WUSB3801_HOST_3000MA);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "failed to write current\n");
		return rc;
	}

	chip->dfp_power = WUSB3801_HOST_3000MA;

	dev_dbg(cdev, "%s: host current (%d)\n", __func__, rc);

	return rc;
}

static int wusb3801_set_toggle_time(struct wusb3801_chip *chip, uint8_t toggle_time)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	if (toggle_time != WUSB3801_TGL_40MS) {
		dev_err(cdev, "toggle_time(%d) is unavailable\n", toggle_time);
		return -EINVAL;
	}

	chip->dttime = WUSB3801_TGL_40MS;

	dev_dbg(cdev, "%s: Fixed toggle time (%d)\n", __func__, chip->dttime);

	return rc;
}

static int wusb3801_init_reg(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;

	/* change current */
	rc = wusb3801_init_force_dfp_power(chip);
	if (IS_ERR_VALUE(rc))
		dev_err(cdev, "%s: failed to force dfp power\n",
				__func__);


	/* change mode */
	rc = wusb3801_set_mode(chip, chip->pdata->init_mode);
	if (IS_ERR_VALUE(rc))
		dev_err(cdev, "%s: failed to set mode\n",
				__func__);

  rc = wusb3801_set_chip_state(chip,
				WUSB3801_STATE_ERROR_RECOVERY);

	if (IS_ERR_VALUE(rc))
		dev_err(cdev, "%s: Reset state failed.\n",
				__func__);

	return rc;
}

static int wusb3801_reset_device(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;
	/*
	rc = i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_CONTROL1, BIT_REG_CTRL1_SM_RST);

	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "reset fails\n");
		return rc;
	}
	*/

	rc = wusb3801_update_status(chip);
	if (IS_ERR_VALUE(rc))
		dev_err(cdev, "fail to read status\n");

	rc = wusb3801_init_reg(chip);
	if (IS_ERR_VALUE(rc))
		dev_err(cdev, "fail to init reg\n");

	wusb3801_detach(chip);

	/* clear global interrupt mask */
	rc = wusb3801_write_masked_byte(chip->client,
				WUSB3801_REG_CONTROL0,
				WUSB3801_INT_MASK,
				WUSB3801_INT_ENABLE);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: fail to init\n", __func__);
		return rc;
	}

	dev_info(cdev, "mode[0x%02x], host_cur[0x%02x], dttime[0x%02x]\n",
			chip->mode, chip->dfp_power, chip->dttime);

	return rc;
}

/************************************************************************
 *
 * create device attr files
 *
 ************************************************************************/
static ssize_t fregdump_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int i, rc, ret = 0;

	mutex_lock(&chip->mlock);
	for (i = WUSB3801_REG_VERSION_ID ; i <= WUSB3801_REG_TEST_12; i++)
	{
		rc = i2c_smbus_read_byte_data(chip->client, (uint8_t)i);
		if (IS_ERR_VALUE(rc))
		{
			pr_err("cannot read 0x%02x\n", i);
			rc = 0;
		}
		ret += snprintf(buf + ret, 1024 - ret, "from 0x%02x read 0x%02x\n", (uint8_t)i, rc);
	}
	mutex_unlock(&chip->mlock);

	return ret;
}
DEVICE_ATTR(fregdump, S_IRUGO, fregdump_show, NULL);

#ifdef __TEST_CC_PATCH__
static ssize_t fcc_status_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int rc, ret = 0;

	mutex_lock(&chip->mlock);
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_STATUS);
	mutex_unlock(&chip->mlock);

	if (IS_ERR_VALUE(rc))
	{
		pr_err("cannot read WUSB3801_REG_STATUS\n");
		rc = 0xFF;
		ret = snprintf(buf, PAGE_SIZE, "cc_sts (%d)\n", rc);
	}
	rc  =  BITS_GET(rc, WUSB3801_CC_STS_MASK);

	if(rc == 0 && chip->cc_sts != 0xFF)
		rc = chip->cc_sts;
	else
		rc -= 1;
	ret = snprintf(buf, PAGE_SIZE, "cc_sts (%d)\n", rc);
	return ret;
}
DEVICE_ATTR(fcc_status, S_IRUGO, fcc_status_show, NULL);
#endif /*  __TEST_CC_PATCH__	 */

static ssize_t ftype_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	switch (chip->type) {
	case WUSB3801_TYPE_SNK:
		ret = snprintf(buf, PAGE_SIZE, "SINK(%d)\n", chip->type);
		break;
	case WUSB3801_TYPE_SRC:
		ret = snprintf(buf, PAGE_SIZE, "SOURCE(%d)\n", chip->type);
		break;
	case WUSB3801_TYPE_DBG_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DEBUGACC(%d)\n", chip->type);
		break;
	case WUSB3801_TYPE_AUD_ACC:
		ret = snprintf(buf, PAGE_SIZE, "AUDIOACC(%d)\n", chip->type);
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "NOTYPE(%d)\n", chip->type);
		break;
	}
	mutex_unlock(&chip->mlock);

	return ret;
}
DEVICE_ATTR(ftype, S_IRUGO , ftype_show, NULL);

static ssize_t fchip_state_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			STRV(WUSB3801_STATE_DISABLED) " - WUSB3801_STATE_DISABLED\n"
			STRV(WUSB3801_STATE_ERROR_RECOVERY) " - WUSB3801_STATE_ERROR_RECOVERY\n"
			STRV(WUSB3801_STATE_UNATTACHED_SNK) " - WUSB3801_STATE_UNATTACHED_SNK\n"
			STRV(WUSB3801_STATE_UNATTACHED_SRC) " - WUSB3801_STATE_UNATTACHED_SRC\n");

}


static ssize_t fchip_state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int state = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &state) == 1) {
		mutex_lock(&chip->mlock);
	  if(((state == WUSB3801_STATE_UNATTACHED_SNK) &&
	    ((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SRC)) || \
	    ((state == WUSB3801_STATE_UNATTACHED_SRC) &&
	    ((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SNK))){
			mutex_unlock(&chip->mlock);
			return -EINVAL;
		}


		rc = wusb3801_set_chip_state(chip, (uint8_t)state);
		if (IS_ERR_VALUE(rc)) {
			mutex_unlock(&chip->mlock);
			return rc;
		}

		wusb3801_detach(chip);
		mutex_unlock(&chip->mlock);
		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(fchip_state, S_IRUGO | S_IWUSR, fchip_state_show, fchip_state_store);

static ssize_t fmode_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	switch (chip->mode) {
	case WUSB3801_DRP_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DRP+ACC(%d)\n", chip->mode);
		break;
	case WUSB3801_DRP:
		ret = snprintf(buf, PAGE_SIZE, "DRP(%d)\n", chip->mode);
		break;
	case WUSB3801_SNK_ACC:
		ret = snprintf(buf, PAGE_SIZE, "SNK+ACC(%d)\n", chip->mode);
		break;
	case WUSB3801_SNK:
		ret = snprintf(buf, PAGE_SIZE, "SNK(%d)\n", chip->mode);
		break;
	case WUSB3801_SRC_ACC:
		ret = snprintf(buf, PAGE_SIZE, "SRC+ACC(%d)\n", chip->mode);
		break;
	case WUSB3801_SRC:
		ret = snprintf(buf, PAGE_SIZE, "SRC(%d)\n", chip->mode);
		break;
	case WUSB3801_DRP_PREFER_SRC_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DRP+ACC+PREFER_SRC(%d)\n", chip->mode);
		break;
	case WUSB3801_DRP_PREFER_SRC:
		ret = snprintf(buf, PAGE_SIZE, "DRP+PREFER_SRC(%d)\n", chip->mode);
		break;
	case WUSB3801_DRP_PREFER_SNK_ACC:
		ret = snprintf(buf, PAGE_SIZE, "DRP+ACC+PREFER_SNK(%d)\n", chip->mode);
		break;
	case WUSB3801_DRP_PREFER_SNK:
		ret = snprintf(buf, PAGE_SIZE, "DRP+PREFER_SNK(%d)\n", chip->mode);
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "UNKNOWN(%d)\n", chip->mode);
		break;
	}
	mutex_unlock(&chip->mlock);

	return ret;
}

static ssize_t fmode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int mode = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &mode) == 1) {
		mutex_lock(&chip->mlock);

		/*
		 * since device trigger to usb happens independent
		 * from charger based on vbus, setting SRC modes
		 * doesn't prevent usb enumeration as device
		 * KNOWN LIMITATION
		 */
		rc = wusb3801_set_mode(chip, (uint8_t)mode);
		if (IS_ERR_VALUE(rc)) {
			mutex_unlock(&chip->mlock);
			return rc;
		}

		rc = wusb3801_set_chip_state(chip,
					WUSB3801_STATE_ERROR_RECOVERY);
		if (IS_ERR_VALUE(rc)) {
			mutex_unlock(&chip->mlock);
			return rc;
		}


		wusb3801_detach(chip);
		mutex_unlock(&chip->mlock);
		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(fmode, S_IRUGO | S_IWUSR, fmode_show, fmode_store);

static ssize_t fdttime_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->dttime);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t fdttime_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int dttime = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &dttime) == 1) {
		mutex_lock(&chip->mlock);
		rc = wusb3801_set_toggle_time(chip, (uint8_t)dttime);
		mutex_unlock(&chip->mlock);
		if (IS_ERR_VALUE(rc))
			return rc;

		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(fdttime, S_IRUGO | S_IWUSR, fdttime_show, fdttime_store);

static ssize_t fhostcur_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->dfp_power);
	mutex_unlock(&chip->mlock);
	return ret;
}

static ssize_t fhostcur_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int buf = 0;
	int rc = 0;

	if (sscanf(buff, "%d", &buf) == 1) {
		mutex_lock(&chip->mlock);
		rc = wusb3801_set_dfp_power(chip, (uint8_t)buf);
		mutex_unlock(&chip->mlock);
		if (IS_ERR_VALUE(rc))
			return rc;

		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(fhostcur, S_IRUGO | S_IWUSR, fhostcur_show, fhostcur_store);

static ssize_t fclientcur_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mlock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", chip->ufp_power);
	mutex_unlock(&chip->mlock);
	return ret;
}
DEVICE_ATTR(fclientcur, S_IRUGO, fclientcur_show, NULL);

static ssize_t freset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buff, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	uint32_t reset = 0;
	int rc = 0;

	if (sscanf(buff, "%u", &reset) == 1) {
		mutex_lock(&chip->mlock);
		rc = wusb3801_reset_device(chip);
		mutex_unlock(&chip->mlock);
		if (IS_ERR_VALUE(rc))
			return rc;

		return size;
	}

	return -EINVAL;
}
DEVICE_ATTR(freset, S_IWUSR, NULL, freset_store);

static int wusb3801_create_devices(struct device *cdev)
{
	int ret = 0;

	ret = device_create_file(cdev, &dev_attr_fchip_state);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_fchip_state\n");
		ret = -ENODEV;
		goto err0;
	}

	ret = device_create_file(cdev, &dev_attr_ftype);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_ftype\n");
		ret = -ENODEV;
		goto err1;
	}

	ret = device_create_file(cdev, &dev_attr_fmode);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_fmode\n");
		ret = -ENODEV;
		goto err2;
	}

	ret = device_create_file(cdev, &dev_attr_freset);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_freset\n");
		ret = -ENODEV;
		goto err3;
	}

	ret = device_create_file(cdev, &dev_attr_fdttime);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_fdttime\n");
		ret = -ENODEV;
		goto err4;
	}

	ret = device_create_file(cdev, &dev_attr_fhostcur);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_fhostcur\n");
		ret = -ENODEV;
		goto err5;
	}

	ret = device_create_file(cdev, &dev_attr_fclientcur);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_fclientcur\n");
		ret = -ENODEV;
		goto err6;
	}

	ret = device_create_file(cdev, &dev_attr_fregdump);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_fregdump\n");
		ret = -ENODEV;
		goto err7;
	}
#ifdef __TEST_CC_PATCH__
	ret = device_create_file(cdev, &dev_attr_fcc_status);
	if (ret < 0) {
		dev_err(cdev, "failed to create dev_attr_fcc_status\n");
		ret = -ENODEV;
		goto err8;
	}

	return ret;

err8:
	device_remove_file(cdev, &dev_attr_fregdump);
#endif /* __TEST_CC_PATCH__ */

err7:
	device_remove_file(cdev, &dev_attr_fclientcur);
err6:
	device_remove_file(cdev, &dev_attr_fhostcur);
err5:
	device_remove_file(cdev, &dev_attr_fdttime);
err4:
	device_remove_file(cdev, &dev_attr_freset);
err3:
	device_remove_file(cdev, &dev_attr_fmode);
err2:
	device_remove_file(cdev, &dev_attr_ftype);
err1:
	device_remove_file(cdev, &dev_attr_fchip_state);
err0:
	return ret;
}

static void wusb3801_destory_device(struct device *cdev)
{
	device_remove_file(cdev, &dev_attr_fchip_state);
	device_remove_file(cdev, &dev_attr_ftype);
	device_remove_file(cdev, &dev_attr_fmode);
	device_remove_file(cdev, &dev_attr_freset);
	device_remove_file(cdev, &dev_attr_fdttime);
	device_remove_file(cdev, &dev_attr_fhostcur);
	device_remove_file(cdev, &dev_attr_fclientcur);
	device_remove_file(cdev, &dev_attr_fregdump);
#ifdef __TEST_CC_PATCH__
	device_remove_file(cdev, &dev_attr_fcc_status);
#endif /* __TEST_CC_PATCH__ */
}

/************************************************************************
 *
 * create proc files for typec CC dir
 *
 ************************************************************************/
static int cc_dir_read(struct seq_file *m, void *v)
{
	int rc = 0;
	//uint8_t dir = 0;
	struct wusb3801_chip *chip = i2c_get_clientdata(wusb3801_client);

	mutex_lock(&chip->mlock);
	rc = i2c_smbus_read_byte_data(wusb3801_client,
			WUSB3801_REG_STATUS);
	mutex_unlock(&chip->mlock);

	if (IS_ERR_VALUE(rc)) {
		pr_err("%s: failed to read\n", __func__);
		return rc;
	}

#ifdef __TEST_CC_PATCH__
	rc  =  BITS_GET(rc, WUSB3801_CC_STS_MASK);
	if(rc == 0 && chip->cc_sts != 0xFF)
		rc = chip->cc_sts;
	else
		rc -= 1;
#endif

	/* 00: Standby;
	 * 01: CC1 connected;
	 * 10: CC2 connected;
	 * 11: CC1 and CC2 connected;
	 */
	if (rc != 0xFF)
		rc += 1;
	if (!chip->attached)
		rc = 0;
	//dir = (rc & WUSB3801_CC_DIR_MASK) >> WUSB3801_CC_DIR_SHIFT;
	seq_printf(m, "%d\n", rc);
	pr_err("%s: cc_dir=%d\n", __func__,rc);
	return 0;
}

static int cc_dir_open(struct inode *inode, struct file *file)
{
	return single_open(file, cc_dir_read, NULL);
}

static const struct proc_ops cc_dir_proc_fops = {
	.proc_open = cc_dir_open,
	.proc_read = seq_read,
};

static int wusb3801_create_proc_info(struct device *cdev)
{
	struct proc_dir_entry *pe;
	struct proc_dir_entry *proc_dir = NULL;

	proc_dir = proc_mkdir("type_cc", NULL);
	if (!proc_dir) {
		dev_err(cdev, "[%s]: mkdir /proc/type_cc failed\n", __func__);
		return -ENOMEM;
	}

	pe = proc_create("cc_dir", S_IRUGO | S_IWUSR, proc_dir, &cc_dir_proc_fops);
	if (!pe) {
		dev_err(cdev, "[%s]: mkdir /proc/type_cc/cc_dir failed\n", __func__);
		return -ENOMEM;
	}

	return 0;
}

#if defined(__WUSB3801_ADD_PR_SWITCH__)
/******************************************************************************
 * 
 * support switch ufp/dfp from Android OS
 * 
 ******************************************************************************/
static int wusb3801_force_dr_mode(struct wusb3801_chip *chip, uint8_t mode,
			uint8_t target_state)
{
	int rc = 0;
	long timeout;

	mutex_lock(&chip->mlock);
	dev_info(&chip->client->dev, "%s: enter wusb3801_set_mode mode = 0x%02x\n",
				__func__, mode);
	rc = wusb3801_set_mode(chip, (uint8_t)mode);
	if (IS_ERR_VALUE(rc)) {
		if (IS_ERR_VALUE(wusb3801_reset_device(chip)))
			dev_err(&chip->client->dev, "%s: failed to reset\n", __func__);
		mutex_unlock(&chip->mlock);
		return rc;
	}

	dev_info(&chip->client->dev, "%s: enter wusb3801_set_chip_state WUSB3801_STATE_ERROR_RECOVERY\n",
				__func__);
	rc = wusb3801_set_chip_state(chip, WUSB3801_STATE_ERROR_RECOVERY);
	if (IS_ERR_VALUE(rc)) {
		if (IS_ERR_VALUE(wusb3801_reset_device(chip)))
			dev_err(&chip->client->dev, "%s: failed to reset\n", __func__);
		mutex_unlock(&chip->mlock);
		return rc;
	}

	wusb3801_detach(chip);
	mutex_unlock(&chip->mlock);

	timeout = wait_event_interruptible_timeout(mode_switch,
			chip->state == target_state,
			msecs_to_jiffies(ROLE_SWITCH_TIMEOUT));
	if (timeout > 0)
	{
		if(chip->state == target_state)
		    return 0;
	}

	mutex_lock(&chip->mlock);
	if (IS_ERR_VALUE(wusb3801_set_mode(chip, chip->pdata->init_mode)))
		dev_err(&chip->client->dev, "%s: failed to set init mode\n", __func__);
	mutex_unlock(&chip->mlock);
	dev_info(&chip->client->dev, "%s: exit wusb3801_set_mode mode = 0x%02x failed!\n",
				__func__, mode);

	return -EIO;
}

int wusb3801_typec_port_type_set(const struct typec_capability *cap,
					enum typec_port_type type)
{
	struct wusb3801_chip *chip = container_of(cap, struct wusb3801_chip, typec_cap);
	int ret;
	uint8_t mode;
	uint8_t target_state;

	if (type == TYPEC_PORT_SRC) {
		mode = WUSB3801_DRP_PREFER_SRC;
		target_state = WUSB3801_STATE_ATTACHED_SRC;
	} else if (type == TYPEC_PORT_SNK) {
		mode = WUSB3801_DRP_PREFER_SNK;
		target_state = WUSB3801_STATE_ATTACHED_SNK;
	} else {
		dev_err(&chip->client->dev, "%s: Trying to set invalid mode\n", __func__);
		return -EINVAL;
	}

	ret = wusb3801_force_dr_mode(chip, mode, target_state);

	return ret;
}
#endif /*__WUSB3801_ADD_PR_SWITCH__*/

/******************************************************************************
 * 
 * Process sink/src attach/detach
 * 
 ******************************************************************************/
/*static int wusb3801_power_set_icurrent_max(struct wusb3801_chip *chip,
						int icurrent)
{
	const union power_supply_propval ret = {icurrent,};

	chip->ufp_power = icurrent;
	if (chip->usb_psy->desc->set_property)
		return chip->usb_psy->desc->set_property(chip->usb_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, &ret);

	return 0;
}
*/

static void wusb3801_bclvl_changed(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc, limit, hcurrent;
	uint8_t status, type;

	rc = i2c_smbus_read_byte_data(chip->client,
				WUSB3801_REG_STATUS);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: failed to read\n", __func__);
		if(IS_ERR_VALUE(wusb3801_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	status = (rc & WUSB3801_TYPE_MASK) ? 1 : 0;
	type = status  ? rc & WUSB3801_TYPE_MASK : WUSB3801_TYPE_INVALID;
	hcurrent = rc & WUSB3801_BCLVL_MASK;
	dev_dbg(cdev, "%s: sts[0x%02x], type[0x%02x]\n",__func__, status, type);

	if (type == WUSB3801_TYPE_SNK) {
		/* do nothing */
	}

	else if(type == WUSB3801_TYPE_SRC){
		limit = (chip->bc_lvl == WUSB3801_SNK_3000MA ? 3000 :
			(chip->bc_lvl == WUSB3801_SNK_1500MA ? 1500 : 0));
		dev_dbg(cdev, "%s: limit = %d\n",__func__, limit);
		//wusb3801_power_set_icurrent_max(chip, limit);
	}
	else{
		/* do nothing */
	}
}

static void wusb3801_src_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	struct typec_partner_desc desc;

   if((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SRC){
		dev_err(cdev, "not support in source mode\n");
		if(IS_ERR_VALUE(wusb3801_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}
	wusb3801_update_state(chip, WUSB3801_STATE_ATTACHED_SNK);

	/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC source has detected.
	*/
	if (chip->port) {
		typec_set_pwr_role(chip->port, TYPEC_SINK);
		typec_set_data_role(chip->port, TYPEC_DEVICE);
		desc.usb_pd = 0;
		desc.accessory = TYPEC_ACCESSORY_NONE;
		desc.identity = NULL;
		chip->partner = typec_register_partner(chip->port, &desc);
		if (IS_ERR(chip->partner)) {
				pr_err("%s : Type-C partner register failed!\n", __func__);
		}
	}
	chip->type = WUSB3801_TYPE_SRC;
}

static void wusb3801_snk_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	struct typec_partner_desc desc;

	dev_info(cdev, "%s \n",__func__);
	if((chip->mode & WUSB3801_ROLE_MASK) == BIT_REG_CTRL0_RLE_SNK){
		dev_err(cdev, "not support in sink mode\n");
		if(IS_ERR_VALUE(wusb3801_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	wusb3801_set_dfp_power(chip, chip->pdata->dfp_power);
	wusb3801_update_state(chip, WUSB3801_STATE_ATTACHED_SRC);
	if (chip->port) {
		typec_set_pwr_role(chip->port, TYPEC_SOURCE);
		typec_set_data_role(chip->port, TYPEC_HOST);
		/* Later USB host mode*/
		desc.usb_pd = 0;
		desc.accessory = TYPEC_ACCESSORY_NONE;
		desc.identity = NULL;
		chip->partner = typec_register_partner(chip->port, &desc);
		if (IS_ERR(chip->partner)) {
				pr_err("%s : Type-C partner register failed!\n", __func__);
		}
	}
	chip->type = WUSB3801_TYPE_SNK;
}

static void wusb3801_dbg_acc_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	dev_info(cdev, "%s \n",__func__);
	if (chip->mode & BIT_REG_CTRL0_DIS_ACC) {
		dev_err(cdev, "not support accessory mode\n");
		if(IS_ERR_VALUE(wusb3801_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC Debug accessory has detected.
	*/
	wusb3801_update_state(chip, WUSB3801_STATE_DEBUG_ACCESSORY);
}

static void wusb3801_aud_acc_detected(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	dev_info(cdev, "%s \n",__func__);
	if (chip->mode & BIT_REG_CTRL0_DIS_ACC) {
		dev_err(cdev, "not support accessory mode\n");
		if(IS_ERR_VALUE(wusb3801_reset_device(chip)))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC Audio accessory has detected.
	*/
	wusb3801_update_state(chip, WUSB3801_STATE_AUDIO_ACCESSORY);
}

static void wusb3801_aud_acc_detach(struct wusb3801_chip *chip)
{
		//struct device *cdev = &chip->client->dev;
/**
	*  TODO: Add you code here if you want doing something special during
	*        TYPEC Audio accessory pull out.
	*/
}

static void wusb3801_detach(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;

	dev_err(cdev, "%s: type[0x%02x] chipstate[0x%02x]\n",
			__func__, chip->type, chip->state);

	switch (chip->state) {
	case WUSB3801_STATE_ATTACHED_SRC:
		wusb3801_init_force_dfp_power(chip);
		break;
	case WUSB3801_STATE_ATTACHED_SNK:
		break;
	case WUSB3801_STATE_DEBUG_ACCESSORY:
		break;
	case WUSB3801_STATE_AUDIO_ACCESSORY:
		wusb3801_aud_acc_detach(chip);
		break;
	case WUSB3801_STATE_DISABLED:
	case WUSB3801_STATE_ERROR_RECOVERY:
		break;
	default:
		dev_err(cdev, "%s: Invaild chipstate[0x%02x]\n",
				__func__, chip->state);
		break;
	}

	chip->type = WUSB3801_TYPE_INVALID;
	chip->bc_lvl = WUSB3801_SNK_0MA;
	chip->ufp_power  = 0;
	chip->try_attcnt = 0;
	chip->attached   = 0;

	//Excute the defered init mode into drp+try.snk when detached
	if(chip->defer_init == 1) {
		dev_err(cdev, "%s: reset to init mode\n", __func__);
		if (IS_ERR_VALUE(wusb3801_set_mode(chip, chip->pdata->init_mode)))
			dev_err(cdev, "%s: failed to set init mode\n", __func__);
		chip->defer_init = 0;
	}
	wusb3801_update_state(chip, WUSB3801_STATE_ERROR_RECOVERY);
	if (chip->port) {
		typec_set_pwr_role(chip->port, TYPEC_SINK);
		typec_set_data_role(chip->port, TYPEC_DEVICE);
		if (!IS_ERR(chip->partner))
			typec_unregister_partner(chip->partner);
		chip->partner = NULL;
	}
}

#ifdef __TEST_CC_PATCH__
static int test_cc_patch(struct wusb3801_chip *chip)
{
	int rc;
	int rc_reg_08;
	int i =0;
	struct device *cdev = &chip->client->dev;
	dev_err(cdev, "%s \n",__func__);
	//mutex_lock(&chip->mlock);

	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_02, 0x82);
	msleep(100);
	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_09, 0xC0);
	msleep(100);
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST0);
	msleep(10);
	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_09, 0x00);
	msleep(10);
	i2c_smbus_write_byte_data(chip->client,
			WUSB3801_REG_TEST_02, 0x80);
//huanglei add for reg 0x08 write zero fail begin
    do{
		msleep(100);
        i2c_smbus_write_byte_data(chip->client,
        WUSB3801_REG_TEST_02, 0x00);
	    msleep(100);
	    rc_reg_08 = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST_02);
		if (rc_reg_08 < 0) {
            dev_err(cdev, "%s: WUSB3801_REG_TEST_02 failed to read\n", __func__);
        }
		i++;
	}while(rc_reg_08 !=0 && i < 5);
//end
//huanglei add for reg 0x0F write zero fail begin
    do{
	    msleep(100);
        i2c_smbus_write_byte_data(chip->client,
        WUSB3801_REG_TEST_09, 0x00);
	    msleep(100);
	    rc_reg_08 = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST_09);
		if (rc_reg_08 < 0) {
            dev_err(cdev, "%s: WUSB3801_REG_TEST_02 failed to read\n", __func__);
        }
		i++;
	}while(rc_reg_08 !=0 && i < 5);
//end
	dev_err(cdev, "%s rc = [0x%02x] \n",__func__, rc);
    return BITS_GET(rc, 0x40);
}
#endif /* __TEST_CC_PATCH__ */

/*
static unsigned wusb3801_is_waiting_for_vbus(struct wusb3801_chip *chip)
{
  struct device *cdev = &chip->client->dev;
	int rc;
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST_0A);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: failed to read test0a\n", __func__);
		return false;
	}
	return !!(rc & WUSB3801_WAIT_VBUS);
}
*/

static void wusb3801_attach(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	int rc = 0;
	uint8_t status, type;

	/* get status */
	rc = i2c_smbus_read_byte_data(chip->client,
			WUSB3801_REG_STATUS);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return;
	}

#ifdef __TEST_CC_PATCH__
	if(chip->dev_sub_id != 0xA0){
		if(chip->cc_test_flag == 0 &&  BITS_GET(rc, WUSB3801_CC_STS_MASK) == 0 ){
			//chip->reg4_val = rc;
			chip->cc_sts = test_cc_patch(chip);
			chip->cc_test_flag = 1;
			dev_err(cdev, "%s: cc_sts[0x%02x]\n", __func__, chip->cc_sts);
			return;
		}
		if(chip->cc_test_flag == 1){
			chip->cc_test_flag = 0;
			if(chip->cc_sts == WUSB3801_CC2_CONNECTED)
				rc = rc | 0x02;
			else if(chip->cc_sts == WUSB3801_CC1_CONNECTED)
				rc = rc | 0x01;
			dev_err(cdev, "%s: cc_test_patch rc[0x%02x]\n", __func__, rc);
		}
	}
#endif	/* __TEST_CC_PATCH__ */

	status = (rc & WUSB3801_ATTACH) ? true : false;
	type = status ? \
			rc & WUSB3801_TYPE_MASK : WUSB3801_TYPE_INVALID;
	dev_info(cdev, "sts[0x%02x], type[0x%02x]\n", status, type);

	if (chip->state != WUSB3801_STATE_ERROR_RECOVERY) {
		rc = wusb3801_set_chip_state(chip,
				WUSB3801_STATE_ERROR_RECOVERY);
		if (IS_ERR_VALUE(rc))
			dev_err(cdev, "%s: failed to set error recovery\n",
					__func__);
		wusb3801_detach(chip);
		dev_err(cdev, "%s: Invaild chipstate[0x%02x]\n",
				__func__, chip->state);
		return;
	}

	chip->bc_lvl = rc & WUSB3801_BCLVL_MASK;
	if(chip->bc_lvl == 0){
		chip->bc_lvl = WUSB3801_SNK_DEFAULT;
		dev_err(cdev, "%s: chip current is 0, set default\n",
				__func__);
	}
	switch (type) {
	case WUSB3801_TYPE_SRC:
		wusb3801_src_detected(chip);
		break;
	case WUSB3801_TYPE_SNK:
		wusb3801_snk_detected(chip);
		break;
	case WUSB3801_TYPE_DBG_ACC:
		wusb3801_dbg_acc_detected(chip);
		chip->type = type;
		break;
	case WUSB3801_TYPE_AUD_ACC:
		wusb3801_aud_acc_detected(chip);
		chip->type = type;
		break;
	case WUSB3801_TYPE_INVALID:
		wusb3801_detach(chip);
		dev_err(cdev, "%s: Invaild type[0x%02x]\n", __func__, type);
		break;
	default:
		rc = wusb3801_set_chip_state(chip,
				WUSB3801_STATE_ERROR_RECOVERY);
		if (IS_ERR_VALUE(rc))
			dev_err(cdev, "%s: failed to set error recovery\n",
					__func__);

		wusb3801_detach(chip);
		dev_err(cdev, "%s: Unknwon type[0x%02x]\n", __func__, type);
		break;
	}
  chip->attached = true;
}

static void wusb3801_work_handler(struct work_struct *work)
{
	struct wusb3801_chip *chip =
			container_of(work, struct wusb3801_chip, dwork);
	struct device *cdev = &chip->client->dev;
	int rc;
	uint8_t int_sts;

	dev_info(cdev,"%s \n",__func__);

	mutex_lock(&chip->mlock);
	/* get interrupt */
	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_INTERRUPT);
	mutex_unlock(&chip->mlock);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: failed to read interrupt\n", __func__);
		goto work_unlock;
	}
	int_sts = rc & WUSB3801_INT_STS_MASK;

	rc = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_STATUS);
	if (IS_ERR_VALUE(rc)) {
		dev_err(cdev, "%s: failed to read reg status\n", __func__);
		goto work_unlock;
	}
	dev_info(cdev,"%s WUSB3801_REG_STATUS : 0x%02x\n", __func__, rc);

	dev_info(cdev, "%s: int_sts[0x%02x]\n", __func__, int_sts);
	// process detach
	if (int_sts & WUSB3801_INT_DETACH){
		wusb3801_detach(chip);
#if defined(__WUSB3801_ADD_ADAPTER__)
		wusb3801_notify_typec_cc_changed(chip, 0);
#endif
	}

	// process attach(RP lvl and attach)
	if (int_sts & WUSB3801_INT_ATTACH){
		if(chip->attached){
			wusb3801_bclvl_changed(chip);
		}
		else{
			wusb3801_attach(chip);
#if defined(__WUSB3801_ADD_ADAPTER__)
		if (chip->type & WUSB3801_TYPE_SRC)
			wusb3801_notify_typec_cc_changed(chip, 1);
		else if (chip->type & WUSB3801_TYPE_SNK)
			wusb3801_notify_typec_cc_changed(chip, 2);
		else
			wusb3801_notify_typec_cc_changed(chip, 3);
#endif
		}
	}

	dev_err(&chip->client->dev, "%s: type:%d\n", __func__, chip->type);

work_unlock:
	enable_irq(chip->irq_gpio);
}

static irqreturn_t wusb3801_interrupt(int irq, void *data)
{
	struct wusb3801_chip *chip = (struct wusb3801_chip *)data;

	if (!chip) {
		dev_err(&chip->client->dev, "%s : called before init.\n", __func__);
		return IRQ_HANDLED;
	}
	disable_irq_nosync(irq);

	/*
	 * wake_lock_timeout, prevents multiple suspend entries
	 * before charger gets chance to trigger usb core for device
	 */
	__pm_wakeup_event(chip->wusb3801_ws.source, WUSB3801_WAKE_LOCK_TIMEOUT);

	if (!queue_work(chip->cc_wq, &chip->dwork))
	{
		dev_err(&chip->client->dev, "%s: can't alloc work\n", __func__);
		enable_irq(irq);
	}

	return IRQ_HANDLED;
}

/******************************************************************************
 * 
 * gpio and dts init
 * 
 ******************************************************************************/
static int wusb3801_init_gpio(struct wusb3801_chip *chip)
{
	int rc = 0;
	return rc;
}

static void wusb3801_free_gpio(struct wusb3801_chip *chip)
{
	if (gpio_is_valid(chip->pdata->int_gpio))
		gpio_free(chip->pdata->int_gpio);
}

static int wusb3801_parse_dt(struct wusb3801_chip *chip)
{
	struct device *cdev = &chip->client->dev;
	struct device_node *dev_node = cdev->of_node;
	struct wusb3801_data *data = chip->pdata;
	u32 val = 0;
	int rc = 0;
	int ret;
	const char *pinctrl_state_name;
	struct pinctrl *irq_pinctrl;

	dev_err(cdev, "dev_node->name=%s.\n",dev_node->name);
	pinctrl_state_name = of_get_property(dev_node, "pinctrl-names", NULL);
	/* configure irq_pinctrl to enable irqs */
	if (pinctrl_state_name) {
		irq_pinctrl = pinctrl_get_select(cdev, pinctrl_state_name);
		if (IS_ERR(irq_pinctrl)) {
			pr_err("Could not get/set %s pinctrl state rc = %ld\n",
						pinctrl_state_name,
						PTR_ERR(irq_pinctrl));
			return PTR_ERR(irq_pinctrl);
		}
	}

	ret = of_get_named_gpio(dev_node, "wusb3801,irq-gpio", 0);
	if (ret < 0) {
		pr_err("%s: error invalid irq gpio err: %d\n", __func__, ret);
		goto out;
	}
	data->int_gpio = ret;
	dev_err(cdev, "%s: valid irq_gpio number: %d\n", __func__, ret);
	ret = gpio_request(data->int_gpio, "wusb3801 irq");
	if (ret) {
		pr_err("%s: gpio %d request failed: %d\n", __func__, data->int_gpio);
		return ret;
	}
	gpio_direction_input(data->int_gpio);

	rc = of_property_read_u32(dev_node,
				"wusb3801,init-mode", &val);
	data->init_mode = (u8)val;
	dev_err(cdev, "data->init_mode=%d.\n",data->init_mode);
	if (rc || wusb3801_check_modes(data->init_mode)) {
		dev_err(cdev, "init mode is not available and set default\n");
		data->init_mode = WUSB3801_INIT_MODE; //WUSB3801_DRP_ACC;
		rc = 0;
	}

	rc = of_property_read_u32(dev_node,
				"wusb3801,host-current", &val);
	data->dfp_power = (u8)val;
	dev_err(cdev, "data->dfp_power=%d.\n",data->dfp_power);
	if (rc || (data->dfp_power > WUSB3801_HOST_3000MA)) {
		dev_err(cdev, "host current is not available and set default\n");
		data->dfp_power = WUSB3801_HOST_DEFAULT;
		rc = 0;
	}

	rc = of_property_read_u32(dev_node,
				"wusb3801,drp-toggle-time", &val);
	data->dttime = (u8)val;
	if (rc || (data->dttime != WUSB3801_TGL_40MS)) {
		dev_err(cdev, "Fixed drp time and set default (40ms:40ms)\n");
		data->dttime = WUSB3801_TGL_40MS;
		rc = 0;
	}

	dev_dbg(cdev, "init_mode:%d dfp_power:%d toggle_time:%d\n",
			data->init_mode, data->dfp_power, data->dttime);

out:
	return rc;
}

/************************************************************************
 * 
 * probe
 * 
 ************************************************************************/
static int wusb3801_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct wusb3801_chip *chip;
	struct device *cdev = &client->dev;
	int ret = 0;
	int irq;
	struct wusb3801_data *data;

	pr_err("wusb3801_probe start \n");
	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA |
				I2C_FUNC_SMBUS_WORD_DATA)) {
		pr_err("smbus data not supported!\n");
		return -EIO;
	}

	chip = devm_kzalloc(cdev, sizeof(struct wusb3801_chip), GFP_KERNEL);
	if (!chip) {
		pr_err("can't alloc wusb3801_chip\n");
		return -ENOMEM;
	}

	chip->client = client;
	i2c_set_clientdata(client, chip);
	wusb3801_client = client;

	ret = wusb3801_read_device_id(chip);
	if (ret){
		pr_err("wusb3801 doesn't find, try again\n");
		ret = wusb3801_read_device_id(chip);
		if(ret){
			pr_err("wusb3801 doesn't find, stop find\n");
			goto err1;
		}
	}

	data = devm_kzalloc(cdev,
			sizeof(struct wusb3801_data), GFP_KERNEL);
	if (!data) {
		pr_err("can't alloc wusb3801_data\n");
		ret = -ENOMEM;
		goto err1;
	}
	chip->pdata = data;
	ret = wusb3801_parse_dt(chip);
	if (ret) {
		pr_err("can't parse dt\n");
		goto err2;
	}

	ret = wusb3801_init_gpio(chip);
	if (ret) {
		pr_err("fail to init gpio\n");
		goto err2;
	}

	chip->type      = WUSB3801_TYPE_INVALID;
	chip->state     = WUSB3801_STATE_ERROR_RECOVERY;
	chip->attached  = 0;
	chip->bc_lvl    = WUSB3801_SNK_0MA;
	chip->ufp_power = 0;
	chip->defer_init = 0;
#ifdef __TEST_CC_PATCH__
	chip->cc_sts = 0xFF;
	chip->cc_test_flag = 0;
#endif /* __TEST_CC_PATCH__ */

	chip->cc_wq = alloc_ordered_workqueue("wusb3801-wq", WQ_HIGHPRI);
	if (!chip->cc_wq) {
		pr_err("unable to create workqueue wusb3801-wq\n");
		goto err2;
	}
	INIT_WORK(&chip->dwork, wusb3801_work_handler);
	wusb3801_wakeup_src_init(chip);
	mutex_init(&chip->mlock);

	ret = wusb3801_create_devices(cdev);
	if (IS_ERR_VALUE(ret)) {
		pr_err("could not create devices\n");
		goto err3;
	}

#if defined(__WUSB3801_ADD_ADAPTER__)
    chip->adapter_dev_name = "typec_adapter";
	chip->adapter_dev = adapter_device_register(chip->adapter_dev_name,
		cdev, chip, &wusb3801_adapter_ops, NULL);
	if (IS_ERR_OR_NULL(chip->adapter_dev)) {
		pr_err("Register typec_adapter failed\n");
		ret = PTR_ERR(chip->adapter_dev);
		goto err3;
	} else {
		pr_err("Register typec_adapter success!\n");
	}
	adapter_dev_set_drvdata(chip->adapter_dev, chip);
#endif

	ret = wusb3801_create_proc_info(cdev);
	if (IS_ERR_VALUE(ret)) {
		pr_err("could not create proc info\n");
		goto err3;
	}

#ifdef CONFIG_OF
	irq = gpio_to_irq(data->int_gpio);
	if (irq < 0) {
		pr_err("%s: error gpio_to_irq returned %d\n", __func__, irq);
		goto err4;
	} else {
		pr_debug("%s: requesting IRQ %d\n", __func__, irq);
		client->irq = irq;
		chip->irq_gpio = irq;
	}

	dev_info(&client->dev, "request_irq irq_gpio=%d, irqnum=%d\n",chip->irq_gpio, irq);
	ret = request_irq(chip->irq_gpio, wusb3801_interrupt,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"wusb3801_int_irq", chip);
	if (ret != 0) {
		pr_err("request_irq fail, ret %d, irqnum %d!!!\n", ret,
			    chip->irq_gpio);
		ret = -ENXIO;
		goto err4;
	}
#else /* !CONFIG_OF */
	chip->irq_gpio = gpio_to_irq(WUSB3801X_INT_PIN);
	dev_info(cdev, "wusb3801 chip->irq_gpio =%d \n",chip->irq_gpio);
	ret = devm_request_irq(cdev, chip->irq_gpio, wusb3801_interrupt,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"wusb3801_int_irq", chip);
	if (ret) {
		pr_err("failed to reqeust IRQ\n");
		goto err4;
	}
#endif /* !CONFIG_OF */

	ret = wusb3801_reset_device(chip);
	if (ret) {
		pr_err("failed to initialize\n");
		goto err4;
	}

	enable_irq_wake(chip->irq_gpio);
	ret = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_STATUS);
	if (IS_ERR_VALUE(ret)) {
		pr_err("%s: failed to read reg status\n", __func__);
	}
	pr_err("%s WUSB3801_REG_STATUS : 0x%02x\n", __func__, ret);

#ifdef __TEST_CC_PATCH__
	if(BITS_GET(ret, 0x80) == 1 && BITS_GET(ret, 0x60) != 0){
		dev_info(cdev, "wusb3801_probe end with no sm rst\n");
		return 0;
	}
#endif	/* __TEST_CC_PATCH__ */

	i2c_smbus_write_byte_data(chip->client, WUSB3801_REG_TEST_02, 0x00);
	i2c_smbus_write_byte_data(chip->client, WUSB3801_REG_TEST_09, 0x00);

	chip->typec_cap.revision = USB_TYPEC_REV_1_2;
	chip->typec_cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
	chip->typec_cap.type = TYPEC_PORT_DRP;
	chip->typec_cap.data = TYPEC_PORT_DRD;
#if defined(__WUSB3801_ADD_PR_SWITCH__)
	chip->typec_cap.port_type_set = wusb3801_typec_port_type_set;
#endif /*__WUSB3801_ADD_PR_SWITCH__*/
	chip->port = typec_register_port(cdev, &chip->typec_cap);
	if (IS_ERR(chip->port)) {
		dev_err(cdev, "%s: register type-c port failed!\n", __func__);
		chip->port = NULL;
	}

	i2c_smbus_write_byte_data(chip->client, WUSB3801_REG_CONTROL1, BIT_REG_CTRL1_SM_RST);
	ret = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST_02);
	if (IS_ERR_VALUE(ret)) {
		dev_err(cdev, "%s: failed to read reg[08] status\n", __func__);
	} else if (ret != 0) {
		dev_err(cdev, "%s: reg[0x08]=0x%02x.\n", __func__, ret);
		msleep(100);
		i2c_smbus_write_byte_data(chip->client, WUSB3801_REG_TEST_02, 0x00);
	}

	ret = i2c_smbus_read_byte_data(chip->client, WUSB3801_REG_TEST_09);
	if (IS_ERR_VALUE(ret)) {
		dev_err(cdev, "%s: failed to read reg[09]tatus\n", __func__);
	} else if (ret != 0) {
		dev_err(cdev, "%s: reg[0x09]=0x%02x.\n", __func__, ret);
		msleep(100);
		i2c_smbus_write_byte_data(chip->client, WUSB3801_REG_TEST_09, 0x00);
	}

	queue_work(chip->cc_wq, &chip->dwork);
#if defined(__WUSB3801_ADD_ADAPTER__)
	wusb3801_notify_typec_init_done(chip);
#endif
	pr_err("wusb3801_probe end \n");

	return 0;

err4:
	wusb3801_destory_device(cdev);
#if defined(__WUSB3801_ADD_ADAPTER__)
	adapter_device_unregister(chip->adapter_dev);
#endif
err3:
	destroy_workqueue(chip->cc_wq);
	mutex_destroy(&chip->mlock);
	//wakeup_source_trash(&chip->wusb3801_ws.source);
	wakeup_source_unregister(chip->wusb3801_ws.source);
	wusb3801_free_gpio(chip);
err2:
	devm_kfree(cdev, chip->pdata);
err1:
	wusb3801_client = NULL;
	i2c_set_clientdata(client, NULL);
	devm_kfree(cdev, chip);

	return ret;
}

static int wusb3801_remove(struct i2c_client *client)
{
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	struct device *cdev = &client->dev;

	if (!chip) {
		dev_err(cdev, "%s : chip is null\n", __func__);
		return -ENODEV;
	}

	if (chip->irq_gpio > 0)
		devm_free_irq(cdev, chip->irq_gpio, chip);

	wusb3801_destory_device(cdev);
	destroy_workqueue(chip->cc_wq);
	mutex_destroy(&chip->mlock);
	//wakeup_source_trash(&chip->wusb3801_ws.source);
	wakeup_source_unregister(chip->wusb3801_ws.source);
	wusb3801_free_gpio(chip);

	devm_kfree(cdev, chip->pdata);

	i2c_set_clientdata(client, NULL);
	devm_kfree(cdev, chip);

	return 0;
}

static void wusb3801_shutdown(struct i2c_client *client)
{
	struct wusb3801_chip *chip = i2c_get_clientdata(client);
	struct device *cdev = &client->dev;

	if (IS_ERR_VALUE(wusb3801_set_mode(chip, WUSB3801_SNK)) ||
			IS_ERR_VALUE(wusb3801_set_chip_state(chip,
					WUSB3801_STATE_ERROR_RECOVERY)))
		dev_err(cdev, "%s: failed to set sink mode\n", __func__);
}

#ifdef CONFIG_PM
static int wusb3801_suspend(struct device *dev)
{
	return 0;
}

static int wusb3801_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops wusb3801_dev_pm_ops = {
	.suspend = wusb3801_suspend,
	.resume  = wusb3801_resume,
};
#endif

static const struct i2c_device_id wusb3801_id_table[] = {
	{"wusb3801", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, wusb3801_id_table);

#ifdef CONFIG_OF
static struct of_device_id wusb3801_match_table[] = {
	{ .compatible = "mediatek,wusb3801x",},
	{ },
};
#else /*!CONFIG_OF*/
#define wusb3801_match_table NULL
#endif /*!CONFIG_OF*/

static const unsigned short normal_i2c[] = {WUSB3801_SLAVE_ADDR0, WUSB3801_SLAVE_ADDR1, I2C_CLIENT_END};
static struct i2c_driver wusb3801_i2c_driver = {
	.driver = {
		.name = "wusb3801",
		.owner = THIS_MODULE,
		.of_match_table = wusb3801_match_table,
#ifdef CONFIG_PM
		.pm = &wusb3801_dev_pm_ops,
#endif
	},
	.probe = wusb3801_probe,
	.remove = wusb3801_remove,
	.shutdown = wusb3801_shutdown,
	.id_table = wusb3801_id_table,
	.address_list = (const unsigned short *) normal_i2c,
};

static __init int wusb3801_i2c_init(void)
{
	return i2c_add_driver(&wusb3801_i2c_driver);
}

static __exit void wusb3801_i2c_exit(void)
{
	i2c_del_driver(&wusb3801_i2c_driver);
}

module_init(wusb3801_i2c_init);
module_exit(wusb3801_i2c_exit);

MODULE_AUTHOR("lhuang@sh-willsemi.com");
MODULE_DESCRIPTION("WUSB3801x USB Type-C driver for MSM and Qcom/Mediatek/Others Linux/Android Platform");
MODULE_LICENSE("GPL v2");
