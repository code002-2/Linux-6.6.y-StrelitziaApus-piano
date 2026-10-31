// SPDX-License-Identifier: GPL-2.0
/*
 * mca_qcom_panel.c
 *
 * MCA panel event bridge for the Qualcomm panel event notifier.
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
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
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <drm/drm_panel.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_panel.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_panel"
#endif

struct mca_panel_dev {
	struct device *dev;
#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	struct delayed_work panel_notify_register_work;
	void *notifier_cookie;
#endif

	int screen_state;
	int hbm_state;
};

static struct mca_panel_dev *g_panel;

int mca_panel_get_screen_state(void)
{
	if (!g_panel)
		return 0;
	return g_panel->screen_state;
}
EXPORT_SYMBOL(mca_panel_get_screen_state);

int mca_panel_get_hbm_state(void)
{
	if (!g_panel)
		return 0;
	return g_panel->hbm_state;
}
EXPORT_SYMBOL(mca_panel_get_hbm_state);

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
static void
mca_panel_event_notifier_callback(enum panel_event_notifier_tag tag,
				  struct panel_event_notification *notification,
				  void *data)
{
	struct mca_panel_dev *panel_dev = data;

	(void)tag;

	if (!notification) {
		mca_log_debug("Invalid panel notification\n");
		return;
	}

	mca_log_info("panel event received, type: %d\n", notification->notif_type);
	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_UNBLANK:
		panel_dev->screen_state =
			notification->notif_type == DRM_PANEL_EVENT_BLANK ? 0 : 1;
		mca_event_block_notify(MCA_EVENT_TYPE_PANEL,
				       MCA_EVENT_PANEL_SCREEN_STATE_CHANGE,
				       &panel_dev->screen_state);
		break;
	case DRM_PANEL_EVENT_HBM_ON:
	case DRM_PANEL_EVENT_HBM_OFF:
		panel_dev->hbm_state =
			notification->notif_type == DRM_PANEL_EVENT_HBM_ON ? 1 : 0;
		mca_event_block_notify(MCA_EVENT_TYPE_PANEL,
				       MCA_EVENT_PANEL_HBM_STATE_CHANGE,
				       &panel_dev->hbm_state);
		break;
	default:
		mca_log_debug("Ignore panel event: %d\n", notification->notif_type);
		break;
	}
}

static int mca_panel_register_panel_notifier(struct mca_panel_dev *panel_dev)
{
	struct device_node *node;
	struct device_node *pnode;
	struct drm_panel *panel = NULL;
	void *cookie = NULL;
	int i, count;
	int rc = -ENODEV;

	node = of_find_node_by_name(NULL, "charge-screen");
	if (!node) {
		of_node_put(node);
		mca_log_err("ERROR: Cannot find node with panel!");
		return -ENODEV;
	}

	count = of_count_phandle_with_args(node, "panel", NULL);
	if (count <= 0) {
		of_node_put(node);
		return -ENODEV;
	}

	for (i = 0; i < count; i++) {
		pnode = of_parse_phandle(node, "panel", i);
		panel = of_drm_find_panel(pnode);
		if (!IS_ERR_OR_NULL(panel))
			break;

		rc = PTR_ERR(panel);
		if (!rc)
			rc = -ENODEV;
		mca_log_err("Failed to find active panel, rc=%d\n", rc);
		panel = NULL;
		of_node_put(pnode);
	}

	if (pnode && panel) {
		cookie = panel_event_notifier_register(
			PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_BATTERY_CHARGER, panel,
			mca_panel_event_notifier_callback, (void *)panel_dev);

		if (IS_ERR_OR_NULL(cookie)) {
			rc = IS_ERR(cookie) ? PTR_ERR(cookie) : -EINVAL;
			mca_log_err(
				"Failed to register panel event notifier, rc=%d\n",
				rc);
			of_node_put(node);
			of_node_put(pnode);
			return rc;
		}

		panel_dev->notifier_cookie = cookie;
		of_node_put(node);
		of_node_put(pnode);
		mca_log_info("register panel notifier successful\n");
		return 0;
	}

	of_node_put(node);
	return -ENODEV;
}

static void mca_panel_register_panel_notifier_work(struct work_struct *work)
{
	struct mca_panel_dev *panel_dev = container_of(
		work, struct mca_panel_dev, panel_notify_register_work.work);
	static int retry_count = 3;

	mca_panel_register_panel_notifier(panel_dev);

	if (retry_count-- && !panel_dev->notifier_cookie) {
		mca_log_err("retry register panel notifier, retry_count = %d\n",
			    retry_count);
		schedule_delayed_work(&panel_dev->panel_notify_register_work,
				      msecs_to_jiffies(5000));
	}
}
#endif

static int mca_panel_probe(struct platform_device *pdev)
{
	struct mca_panel_dev *panel_dev;
	static int probe_cnt;

	mca_log_info("probe_cnt = %d\n", ++probe_cnt);

	panel_dev = devm_kzalloc(&pdev->dev, sizeof(*panel_dev), GFP_KERNEL);
	if (!panel_dev) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}

	panel_dev->dev = &pdev->dev;
	panel_dev->screen_state = 0;
	panel_dev->hbm_state = 0;
	platform_set_drvdata(pdev, panel_dev);
	g_panel = panel_dev;

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	INIT_DELAYED_WORK(&panel_dev->panel_notify_register_work,
			  mca_panel_register_panel_notifier_work);
	schedule_delayed_work(&panel_dev->panel_notify_register_work,
			      msecs_to_jiffies(5000));
#endif

	mca_log_err("probe OK");
	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,mca_panel" },
	{},
};

static int mca_panel_remove(struct platform_device *pdev)
{
	struct mca_panel_dev *panel_dev = platform_get_drvdata(pdev);

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	cancel_delayed_work_sync(&panel_dev->panel_notify_register_work);
	if (!IS_ERR(panel_dev->notifier_cookie))
		panel_event_notifier_unregister(panel_dev->notifier_cookie);
#endif

	if (g_panel == panel_dev)
		g_panel = NULL;

	devm_kfree(&pdev->dev, panel_dev);
	return 0;
}

static void mca_panel_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver mca_panel_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_panel",
		.of_match_table = match_table,
	},
	.probe = mca_panel_probe,
	.remove = mca_panel_remove,
	.shutdown = mca_panel_shutdown,
};

static int __init mca_panel_init(void)
{
	return platform_driver_register(&mca_panel_driver);
}
module_init(mca_panel_init);

static void __exit mca_panel_exit(void)
{
	platform_driver_unregister(&mca_panel_driver);
}
module_exit(mca_panel_exit);

MODULE_DESCRIPTION("mca get panel event");
MODULE_AUTHOR("muxinyi1@xiaomi.com");
MODULE_LICENSE("GPL v2");
