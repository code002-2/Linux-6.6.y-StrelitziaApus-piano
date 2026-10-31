// SPDX-License-Identifier: GPL-2.0
/*
 * mca_bmd.c
 *
 * battery missing detection driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_wireless_class.h>
#include "inc/mca_bmd.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_bmd"
#endif

#define BTB_OPEN_MAX_VOL 1900
#define BTB_OPEN_MIN_VOL 1700

#define MONITOR_HEARTBEAT_TIMER_MS 500
#define DELAY_REPORT_BMD_STS_MS 2500
#define REQUEST_HW_RESOURCE_RETRY_MS 25
#define REQUEST_HW_RESOURCE_RETRY_MAX 3

#define MCA_BMD_DUAL_FG 1

struct bmd_scheme_data {
	int scheme;
	struct iio_channel *channel;
	int gpio;
	bool cfg_failed;
};

struct mca_bmd_dev {
	struct device *dev;
	struct bmd_scheme_data bmd_scheme[MAX_BTB];
	struct delayed_work monitor_bmd_work;
	struct delayed_work request_hw_resource_work;
	struct delayed_work delay_report_bmd_sts_work;
	bool btb_online[MAX_BTB];
	bool batt_missing;
	int fake_batt;
	int fg_type;
};

enum bmd_scheme {
	ADC_SCHEME,
	GPIO_SCHEME,
	INT_SCHEME,
	IIC_SCHEME,
	MAX_SCHEME,
};

static int g_request_hw_resource_retry_count;

static bool mca_bmd_get_btb_status(struct mca_bmd_dev *info, int index)
{
	int ret;
	int data = 0;
	bool bmd_online = false;

	if (!info) {
		mca_log_err("null pointer\n");
		return true;
	}

	if (info->bmd_scheme[index].cfg_failed) {
		mca_log_info("cfg failed\n");
		return false;
	}

	switch (info->bmd_scheme[index].scheme) {
	case ADC_SCHEME:
		ret = iio_read_channel_processed(
			info->bmd_scheme[index].channel, &data);
		if (ret < 0) {
			mca_log_err(
				"Error in reading btb_adc_voltage channel\n");
			bmd_online = false;
		} else if (data > BTB_OPEN_MIN_VOL && data < BTB_OPEN_MAX_VOL) {
			bmd_online = false;
		} else {
			bmd_online = true;
		}
		break;
	case GPIO_SCHEME:
		data = gpio_get_value(info->bmd_scheme[index].gpio);
		bmd_online = !data;
		break;
	case IIC_SCHEME:
		if (info->fg_type == MCA_BMD_DUAL_FG)
			ret = strategy_class_fg_dual_is_chip_ok(index);
		else
			ret = strategy_class_fg_is_chip_ok();
		bmd_online = ret > 0;
		break;
	default:
		bmd_online = false;
		break;
	}

	return bmd_online;
}

static int mca_bmd_get_status(int type, void *value, void *data)
{
	struct mca_bmd_dev *info = data;
	u32 *status = value;

	if (type != STRATEGY_STATUS_TYPE_BMD_BATT_MISSING || !status || !info)
		return -1;

	*status = info->batt_missing;
	return 0;
}

/*
 * A battery read over i2c is only known once the fuel gauge strategy is
 * up. Asking before that reads as both connectors open, which would flag
 * the battery as missing and fake.
 */
static bool mca_bmd_fg_pending(struct mca_bmd_dev *info)
{
	int i;

	for (i = 0; i < MAX_BTB; i++)
		if (info->bmd_scheme[i].scheme == IIC_SCHEME)
			return strategy_class_fg_ops_is_init_ok() <= 0;

	return false;
}

static void mca_bmd_monitor_workfunc(struct work_struct *work)
{
	struct mca_bmd_dev *info =
		container_of(work, struct mca_bmd_dev, monitor_bmd_work.work);
	bool batt_missing = true;
	int fake_batt;
	int report;

	if (mca_bmd_fg_pending(info)) {
		schedule_delayed_work(&info->monitor_bmd_work,
				      msecs_to_jiffies(MONITOR_HEARTBEAT_TIMER_MS));
		return;
	}

	info->btb_online[MASTER_BTB] = mca_bmd_get_btb_status(info, MASTER_BTB);
	info->btb_online[SLAVE_BTB] = mca_bmd_get_btb_status(info, SLAVE_BTB);
	mca_log_info("btb_online[0]: %d, btb_online[1]: %d\n",
		     info->btb_online[MASTER_BTB], info->btb_online[SLAVE_BTB]);

	if (info->btb_online[MASTER_BTB])
		batt_missing = !info->btb_online[SLAVE_BTB];

	if (info->batt_missing != batt_missing) {
		info->batt_missing = batt_missing;
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
				       MCA_EVENT_BATT_BTB_CHANGE,
				       &info->batt_missing);
		if (!batt_missing) {
			if (info->fg_type != MCA_BMD_DUAL_FG)
				mca_charge_mievent_set_state(
					MIEVENT_STATE_END,
					CHARGE_DFX_BATTERY_MISSING);
			else
				mca_charge_mievent_set_state(
					MIEVENT_STATE_END,
					CHARGE_DFX_DUAL_BATTERY_MISSING);
		} else if (info->fg_type != MCA_BMD_DUAL_FG) {
			mca_charge_mievent_report(CHARGE_DFX_BATTERY_MISSING,
						  NULL, 0);
		} else {
			report = info->btb_online[MASTER_BTB];
			mca_charge_mievent_report(
				CHARGE_DFX_DUAL_BATTERY_MISSING, &report, 1);
		}
	}

	/* detect fake battery */
	if (info->fg_type == MCA_BMD_DUAL_FG) {
		if (info->btb_online[MASTER_BTB] && info->btb_online[SLAVE_BTB])
			fake_batt = 0;
		else
			fake_batt = 1;
	} else {
		if (!info->btb_online[MASTER_BTB] &&
		    !info->btb_online[SLAVE_BTB])
			fake_batt = 1;
		else
			fake_batt = 0;
	}

	if (info->fake_batt != fake_batt) {
		info->fake_batt = fake_batt;
		mca_log_err("fake_batt = %d\n", fake_batt);
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_FAKE_POWER,
				       &info->fake_batt);
	}

	schedule_delayed_work(&info->monitor_bmd_work,
			      msecs_to_jiffies(MONITOR_HEARTBEAT_TIMER_MS));
}

static void mca_bmd_delay_report_bmd_sts_work(struct work_struct *work)
{
	struct mca_bmd_dev *info = container_of(work, struct mca_bmd_dev,
						delay_report_bmd_sts_work.work);
	bool batt_missing = true;
	int fake_batt;

	if (mca_bmd_fg_pending(info)) {
		schedule_delayed_work(&info->delay_report_bmd_sts_work,
				      msecs_to_jiffies(MONITOR_HEARTBEAT_TIMER_MS));
		return;
	}

	info->btb_online[MASTER_BTB] = mca_bmd_get_btb_status(info, MASTER_BTB);
	info->btb_online[SLAVE_BTB] = mca_bmd_get_btb_status(info, SLAVE_BTB);
	mca_log_err("init notify:btb_online[0]: %d, btb_online[1]: %d\n",
		    info->btb_online[MASTER_BTB], info->btb_online[SLAVE_BTB]);

	if (info->btb_online[MASTER_BTB])
		batt_missing = !info->btb_online[SLAVE_BTB];
	mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
			       MCA_EVENT_BATT_BTB_CHANGE, &batt_missing);

	if (info->fg_type == MCA_BMD_DUAL_FG) {
		if (!info->btb_online[MASTER_BTB] ||
		    !info->btb_online[SLAVE_BTB])
			fake_batt = 1;
		else
			fake_batt = 0;
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_FAKE_POWER,
				       &fake_batt);
	}
}

static void mca_bmd_request_hw_resource_work(struct work_struct *work)
{
	struct mca_bmd_dev *info = container_of(work, struct mca_bmd_dev,
						request_hw_resource_work.work);
	struct device_node *node = info->dev->of_node;
	bool need_retry = false;
	int ret;
	int i;

	for (i = 0; i < MAX_BTB; i++) {
		if (!info->bmd_scheme[i].cfg_failed ||
		    info->bmd_scheme[i].scheme != GPIO_SCHEME)
			continue;

		info->bmd_scheme[i].gpio =
			of_get_named_gpio(node, "btb_gpio", 0);
		if (info->bmd_scheme[i].gpio < 0) {
			mca_log_err("failed to gpio is invalid %d\n",
				    info->bmd_scheme[i].gpio);
			info->bmd_scheme[i].cfg_failed = true;
			need_retry = true;
			continue;
		}

		ret = gpio_request(info->bmd_scheme[i].gpio, "btb_gpio");
		if (ret) {
			info->bmd_scheme[i].cfg_failed = true;
			need_retry = true;
			mca_log_err("unable to request btb_gpio gpio [%d]\n",
				    info->bmd_scheme[i].gpio);
			continue;
		}

		info->bmd_scheme[i].cfg_failed = false;
		ret = gpio_direction_input(info->bmd_scheme[i].gpio);
		if (ret)
			mca_log_err("unable to set direction btb_gpio [%d]\n",
				    info->bmd_scheme[i].gpio);
	}

	if (need_retry &&
	    g_request_hw_resource_retry_count < REQUEST_HW_RESOURCE_RETRY_MAX) {
		g_request_hw_resource_retry_count++;
		schedule_delayed_work(
			&info->request_hw_resource_work,
			msecs_to_jiffies(REQUEST_HW_RESOURCE_RETRY_MS));
	}
	mca_log_err("retry bmd resource request %d\n",
		    g_request_hw_resource_retry_count);
}

static int mca_bmd_process_event(int event, int value, void *data)
{
	struct mca_bmd_dev *info = data;

	if (!info) {
		mca_log_err("%s: info is null", __func__);
		return -1;
	}

	switch (event) {
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
		cancel_delayed_work_sync(&info->monitor_bmd_work);
		break;
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		cancel_delayed_work_sync(&info->monitor_bmd_work);
		schedule_delayed_work(&info->monitor_bmd_work, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int mca_bmd_parse_dt(struct mca_bmd_dev *info)
{
	struct device_node *node = info->dev->of_node;
	int ret;
	int val[MAX_BTB] = { 0 };
	int i;

	if (!node) {
		mca_log_err("device tree info missing\n");
		return -1;
	}

	ret = mca_parse_dts_u32_array(node, "btb_bmd_scheme", val, MAX_BTB);
	if (ret < 0) {
		mca_log_err("parse btb_bmd_scheme failed\n");
		return ret;
	}

	for (i = 0; i < MAX_BTB; i++) {
		info->bmd_scheme[i].scheme = val[i];
		switch (info->bmd_scheme[i].scheme) {
		case ADC_SCHEME:
			info->bmd_scheme[i].channel =
				devm_iio_channel_get(info->dev, "btb_adc");
			info->bmd_scheme[i].cfg_failed = false;
			break;
		case GPIO_SCHEME:
			info->bmd_scheme[i].gpio =
				of_get_named_gpio(node, "btb_gpio", 0);
			if (info->bmd_scheme[i].gpio < 0) {
				mca_log_err("failed to gpio is invalid %d\n",
					    info->bmd_scheme[i].gpio);
				info->bmd_scheme[i].cfg_failed = true;
				break;
			}
			ret = gpio_request(info->bmd_scheme[i].gpio,
					   "btb_gpio");
			if (ret) {
				info->bmd_scheme[i].cfg_failed = true;
				mca_log_err(
					"unable to request btb_gpio gpio [%d]\n",
					info->bmd_scheme[i].gpio);
				break;
			}
			info->bmd_scheme[i].cfg_failed = false;
			ret = gpio_direction_input(info->bmd_scheme[i].gpio);
			if (ret)
				mca_log_err(
					"unable to set direction btb_gpio [%d]\n",
					info->bmd_scheme[i].gpio);
			break;
		case INT_SCHEME:
		case IIC_SCHEME:
			info->bmd_scheme[i].cfg_failed = false;
			break;
		default:
			break;
		}
	}

	mca_parse_dts_u32(node, "fg_type", &info->fg_type, 0);
	return 0;
}

enum btb_attr_list {
	BTB_PROP_MASTER,
	BTB_PROP_SLAVE,
	BTB_PROP_MISSING,
};

static ssize_t btb_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buf);

struct mca_sysfs_attr_info btb_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(btb_sysfs, 0440, BTB_PROP_MASTER, btb_master_status),
	mca_sysfs_attr_ro(btb_sysfs, 0440, BTB_PROP_SLAVE, btb_slave_status),
	mca_sysfs_attr_ro(btb_sysfs, 0440, BTB_PROP_MISSING,
			  btb_missing_status),
};

#define BTB_SYSFS_ATTRS_SIZE ARRAY_SIZE(btb_sysfs_field_tbl)

static struct attribute *btb_sysfs_attrs[BTB_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group btb_sysfs_attr_group = {
	.attrs = btb_sysfs_attrs,
};

static ssize_t btb_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	struct mca_bmd_dev *info = dev_get_drvdata(dev);
	bool btb_online = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name, btb_sysfs_field_tbl,
					  BTB_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case BTB_PROP_MASTER:
		btb_online = mca_bmd_get_btb_status(info, MASTER_BTB);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", btb_online);
		break;
	case BTB_PROP_SLAVE:
		btb_online = mca_bmd_get_btb_status(info, SLAVE_BTB);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", btb_online);
		break;
	case BTB_PROP_MISSING:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->batt_missing);
		break;
	default:
		break;
	}

	return count;
}

static int btb_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(btb_sysfs_attrs, btb_sysfs_field_tbl,
			     BTB_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "bmd", dev,
					   &btb_sysfs_attr_group);
}

static int mca_bmd_probe(struct platform_device *pdev)
{
	struct mca_bmd_dev *info;
	int ret;
	int online = 0;
	int present = 0;
	bool need_retry = false;
	int i;

	mca_log_info("bmd probe begin\n");

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	INIT_DELAYED_WORK(&info->request_hw_resource_work,
			  mca_bmd_request_hw_resource_work);
	INIT_DELAYED_WORK(&info->delay_report_bmd_sts_work,
			  mca_bmd_delay_report_bmd_sts_work);

	ret = mca_bmd_parse_dt(info);
	if (ret) {
		mca_log_err("parse dt faile\n");
		return -1;
	}

	for (i = 0; i < MAX_BTB; i++)
		if (info->bmd_scheme[i].cfg_failed)
			need_retry = true;
	if (need_retry)
		schedule_delayed_work(
			&info->request_hw_resource_work,
			msecs_to_jiffies(REQUEST_HW_RESOURCE_RETRY_MS));

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BMD,
					mca_bmd_process_event, mca_bmd_get_status,
					NULL, info);

	info->batt_missing = 0;
	INIT_DELAYED_WORK(&info->monitor_bmd_work, mca_bmd_monitor_workfunc);

	btb_sysfs_create_group(info->dev);

	schedule_delayed_work(&info->delay_report_bmd_sts_work,
			      msecs_to_jiffies(DELAY_REPORT_BMD_STS_MS));

	platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &present);
	if (online || present)
		schedule_delayed_work(
			&info->monitor_bmd_work,
			msecs_to_jiffies(MONITOR_HEARTBEAT_TIMER_MS));

	mca_log_err("%s success\n", __func__);
	return 0;
}

static int mca_bmd_remove(struct platform_device *pdev)
{
	return 0;
}

static void mca_bmd_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,bmd" },
	{},
};

static struct platform_driver mca_bmd_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_bmd",
		.of_match_table = match_table,
	},
	.probe = mca_bmd_probe,
	.remove = mca_bmd_remove,
	.shutdown = mca_bmd_shutdown,
};

static int __init mca_bmd_init(void)
{
	return platform_driver_register(&mca_bmd_driver);
}
module_init(mca_bmd_init);

static void __exit mca_bmd_exit(void)
{
	platform_driver_unregister(&mca_bmd_driver);
}
module_exit(mca_bmd_exit);

MODULE_DESCRIPTION("battery missing detection");
MODULE_AUTHOR("lvxiaofeng@xiaomi.com");
MODULE_LICENSE("GPL v2");
