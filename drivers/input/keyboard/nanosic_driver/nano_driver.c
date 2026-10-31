/** ***************************************************************************
 * @file nano_driver.c
 *
 * @brief provided interface of initialza and release nanosic driver .
 *
 * <em>Copyright (C) 2010, Nanosic, Inc.  All rights reserved.</em>
 * Author : Bin.yuan bin.yuan@nanosic.com 
 * */

/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_panel.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include "nano_macro.h"

static bool initial = false;
struct nano_i2c_client *gI2c_client = NULL;
static struct drm_panel *active_panel;
static struct regulator *vdd_reg;
static struct regulator *dvdd_reg;

/* the 803x runs from a 3.3 V and a 1.8 V supply where the board has them */
static int Nanosic_regulator_init(struct device *dev)
{
	const char *name;
	int ret;

	if (of_property_read_string(dev->of_node, "nanosic,vdd-name", &name))
		return 0;

	vdd_reg = devm_regulator_get(dev, name);
	if (IS_ERR(vdd_reg))
		return PTR_ERR(vdd_reg);

	ret = of_property_read_string(dev->of_node, "nanosic,dvdd-name", &name);
	if (ret)
		return ret;

	dvdd_reg = devm_regulator_get(dev, name);
	if (IS_ERR(dvdd_reg))
		return PTR_ERR(dvdd_reg);

	ret = regulator_set_voltage(vdd_reg, 3300000, 3300000);
	if (ret)
		return ret;

	return regulator_set_voltage(dvdd_reg, 1800000, 1800000);
}

static int Nanosic_switch_regulator(bool on)
{
	int ret;

	if (IS_ERR_OR_NULL(vdd_reg) || IS_ERR_OR_NULL(dvdd_reg))
		return 0;

	if (!on) {
		regulator_disable(dvdd_reg);
		return regulator_disable(vdd_reg);
	}

	ret = regulator_enable(vdd_reg);
	if (ret)
		return ret;

	ret = regulator_enable(dvdd_reg);
	if (ret)
		regulator_disable(vdd_reg);

	return ret;
}

/* the keyboard follows the state of the panel it is attached to */
static int keyboard_check_panel(struct device_node *np)
{
	struct device_node *node;
	struct drm_panel *panel;
	int i, count;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return -ENODEV;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			active_panel = panel;
			return 0;
		}
	}

	return PTR_ERR(panel);
}

static int nanosic_803_probe(struct i2c_client *client)
{
	int ret = 0;
	struct device_node *of_node;
	int irq_pin = -1;
	u32 irq_flags = 0;
	int reset_pin = -1;
	int status_pin = -1;
	int vdd_pin = -1;
	int sleep_pin = -1;

	//    struct regulator *vddh_reg=NULL;
	struct nano_i2c_client *I2client = NULL;

	if (IS_ERR_OR_NULL(client)) {
		dbgprint(ERROR_LEVEL,
			 "nanosic_803_probe client IS_ERR_OR_NULL\n");
		return -1;
	}

	dbgprint(ALERT_LEVEL, "probe adapter nr %d, addr 0x%x\n",
		 client->adapter->nr, client->addr);

	of_node = client->dev.of_node;
	if (!of_node) {
		dbgprint(ERROR_LEVEL, "nanosic_803_probe of_node == 0\n");
		return -1;
	}

	ret = keyboard_check_panel(of_node);
	if (ret) {
		dbgprint(ERROR_LEVEL, "no active panel: %d\n", ret);
		return ret == -EPROBE_DEFER ? ret : -ENODEV;
	}

	ret = Nanosic_regulator_init(&client->dev);
	if (ret) {
		dbgprint(ERROR_LEVEL, "failed to get the regulators: %d\n", ret);
		return ret;
	}

	ret = Nanosic_switch_regulator(true);
	if (ret) {
		dbgprint(ERROR_LEVEL, "failed to enable the regulators: %d\n", ret);
		return ret;
	}

	irq_pin = of_get_named_gpio(of_node, "irq_pin", 0);
	dbgprint(ALERT_LEVEL, "irq_pin=%d,irq_flags=%d ret=%d\n", irq_pin,
		 irq_flags, ret);
	reset_pin = of_get_named_gpio(of_node, "reset_pin", 0);
	dbgprint(ALERT_LEVEL, "reset_pin=%d\n", reset_pin);
	status_pin = of_get_named_gpio(of_node, "status_pin", 0);
	dbgprint(ALERT_LEVEL, "status_pin=%d\n", status_pin);
	vdd_pin = of_get_named_gpio(of_node, "vdd_pin", 0);
	dbgprint(ALERT_LEVEL, "vdd_pin=%d\n", vdd_pin);
	sleep_pin = of_get_named_gpio(of_node, "sleep_pin", 0);
	dbgprint(ALERT_LEVEL, "sleep_pin=%d\n", sleep_pin);

	gpio_hall_n_pin = of_get_named_gpio(of_node, "hall_n_pin", 0);
	gpio_hall_s_pin = of_get_named_gpio(of_node, "hall_s_pin", 0);
	dbgprint(ALERT_LEVEL, "hall_n_pin=%d hall_s_pin:%d \n", gpio_hall_n_pin,
		 gpio_hall_s_pin);
	/*    vddh_reg = regulator_get(client->dev, "vddh-supply");
    if(IS_ERR_OR_NULL(vddh_reg))
        dbgprint(ERROR_LEVEL,"Failed to get vddh_reg\n");
*/
	/*initialize chardev module*/
	ret = Nanosic_chardev_register();
	if (ret < 0)
		goto _err1;

	/*initialize input module*/
	ret = Nanosic_input_register();
	if (ret < 0)
		goto _err2;

	ret = Nanosic_GPIO_register(vdd_pin, reset_pin, status_pin, irq_pin,
				    sleep_pin);
	if (ret < 0)
		dbgprint(ERROR_LEVEL, "GPIO register ERROR!\n\n");

#if 0
    ret = Nanosic_i2c_detect(client,NULL);
    if(ret < 0){
        dbgprint(ERROR_LEVEL,"I2C communication ERROR!\n\n");
        return -1;
    }
#endif
	/*initialize i2c module*/
	I2client = Nanosic_i2c_register(irq_pin, irq_flags, client->adapter->nr,
					client->addr);
	if (IS_ERR_OR_NULL(I2client))
		goto _err3;

	i2c_set_clientdata(client, I2client);
	gI2c_client = I2client;
	gI2c_client->dev = &(client->dev);
	Nanosic_edge_filter_init(of_node);
	device_init_wakeup(&client->dev, true);

	ret = xiaomi_keyboard_init(I2client);
	if (ret < 0)
		goto _err_keyboard;
	/*initialize timer for test*/

	ret = Nanosic_cache_init();
	if (ret < 0) {
		dbgprint(ERROR_LEVEL, "Nanosic cache init ERROR!\n");
		goto _err4;
	}

	initial = true;
	/* create the input devices if the keyboard is already attached */
	Nanosic_i2c_request_hall_status(I2client);
	dbgprint(ALERT_LEVEL, "probe nanosic driver\n");

	return 0;

_err4:
	Nanosic_cache_release();
_err_keyboard:
	xiaomi_keyboard_remove();
_err3:
	/* the wakeup interrupt uses gI2c_client, so free it first */
	Nanosic_GPIO_release();
	if (gI2c_client) {
		gI2c_client = NULL;
		Nanosic_i2c_release(I2client);
	}
	Nanosic_input_release();
_err2:
	Nanosic_chardev_release();
_err1:
	Nanosic_switch_regulator(false);
	return -1;
}

static void nanosic_803_remove(struct i2c_client *client)
{
	struct nano_i2c_client *I2client = NULL;

	dbgprint(ALERT_LEVEL, "remove\n");
	if (initial == false)
		return;

	I2client = i2c_get_clientdata(client);

	/* Stop panel work before releasing its input and I2C devices. */
	xiaomi_keyboard_remove();

	/*release chardev module*/
	Nanosic_chardev_release();

	/*release input module*/
	Nanosic_input_release();

	/* The wakeup interrupt uses gI2c_client. */
	Nanosic_GPIO_release();
	gI2c_client = NULL;
	Nanosic_i2c_release(I2client);
	device_init_wakeup(&client->dev, false);

	Nanosic_switch_regulator(false);

	Nanosic_cache_release();

	initial = false;

	dbgprint(ALERT_LEVEL, "remove nanosic driver\n");
}

static const struct of_device_id nanosic_803_of_match[] = {
	{
		.compatible = "nanosic,803",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nanosic_803_of_match);

static const struct i2c_device_id nanosic_803_i2c_id[] = { { "nanosic,803", 0 },
							   {} };
MODULE_DEVICE_TABLE(i2c, nanosic_803_i2c_id);

static struct i2c_driver nanosic_803_driver = {
	.probe		= nanosic_803_probe,
	.remove		= nanosic_803_remove,
	.driver = {
		.name	= "nanosic,803",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nanosic_803_of_match),
        .pm = &xiaomi_keyboard_pm_ops,
	},
    .id_table       = nanosic_803_i2c_id,
	.detect         = Nanosic_i2c_detect,
};

static __init int nanosic_driver_init(void)
{
	int ret;

	ret = i2c_add_driver(&nanosic_803_driver);

	return ret;
}

static __exit void nanosic_driver_exit(void)
{
	i2c_del_driver(&nanosic_803_driver);
}

static int xiaomi_keyboard_init(struct nano_i2c_client *i2c_client)
{
	//struct xiaomi_keyboard_platdata *pdata;
	int ret = 0;
	dbgprint(ALERT_LEVEL, "xiaomi_keyboard_init: enter\n");
	mdata = kzalloc(sizeof(struct xiaomi_keyboard_data), GFP_KERNEL);
	if (!mdata)
		return -ENOMEM;
	mdata->dev_pm_suspend = false;
	mdata->irq = i2c_client->irqno;
	dbgprint(ALERT_LEVEL, "xiaomi_keyboard_init:irq:%d", mdata->irq);
	mdata->event_wq =
		alloc_workqueue("kb-event-queue",
				WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!mdata->event_wq) {
		dbgprint(
			ALERT_LEVEL,
			"xiaomi_keyboard_init:Can not create work thread for suspend/resume!!");
		return -ENOMEM;
	}
	INIT_WORK(&mdata->early_resume_work, keyboard_early_resume_work);
	INIT_WORK(&mdata->resume_work, keyboard_resume_work);
	INIT_WORK(&mdata->early_suspend_work, keyboard_early_suspend_work);
	INIT_WORK(&mdata->suspend_work, keyboard_suspend_work);

	mdata->notifier_cookie = panel_event_notifier_register(
		PANEL_EVENT_NOTIFICATION_PRIMARY,
		PANEL_EVENT_NOTIFIER_CLIENT_KEYBOARD, active_panel,
		keyboard_panel_notifier_callback, mdata);
	if (IS_ERR_OR_NULL(mdata->notifier_cookie)) {
		ret = mdata->notifier_cookie ? PTR_ERR(mdata->notifier_cookie) : -ENOMEM;
		dbgprint(
			ALERT_LEVEL,
			"xiaomi_keyboard_init:register panel notifier failed. ret=%d\n",
			ret);
		mdata->notifier_cookie = NULL;
		goto err_register_drm_notif_failed;
	}
	dbgprint(ALERT_LEVEL, "xiaomi_keyboard_init: success. \n");
	return ret;

err_register_drm_notif_failed:
	if (mdata->event_wq) {
		destroy_workqueue(mdata->event_wq);
		mdata->event_wq = NULL;
	}
	return ret;
}

/* the screen is about to turn on, wake the 803x and fetch what it queued */
static void keyboard_early_resume_work(struct work_struct *work)
{
	int ret = 0;
	dbgprint(ALERT_LEVEL, "keyboard_early_resume_work: enter\n");
	ret = Nanosic_cache_put();
	if (ret < 0)
		dbgprint(ERROR_LEVEL,
			 "keyboard_early_resume_work: Nanosic_cache_put err:%d\n",
			 ret);
	g_panel_status = true;
	Nanosic_GPIO_sleep(true);
	Nanosic_workQueue_schedule(gI2c_client->worker);
}

static void keyboard_resume_work(struct work_struct *work)
{
	int ret = 0;
	dbgprint(ALERT_LEVEL, "keyboard_resume_work: enter\n");
	ret = Nanosic_screen_state_notify(true);
	if (ret < 0)
		dbgprint(ERROR_LEVEL,
			 "keyboard_resume_work: Nanosic_screen_state_notify err:%d\n",
			 ret);
	ret = Nanosic_Hall_notify(gpio_hall_n_pin, gpio_hall_s_pin);
	if (ret < 0)
		dbgprint(ERROR_LEVEL,
			 "keyboard_resume_work: Nanosic_Hall_notify err:%d\n",
			 ret);
}

/* the screen is about to turn off, tell the keypad while the 803x is awake */
static void keyboard_early_suspend_work(struct work_struct *work)
{
	int ret;
	dbgprint(ALERT_LEVEL, "keyboard_early_suspend_work: enter\n");
	ret = Nanosic_screen_state_notify(false);
	if (ret < 0)
		dbgprint(ERROR_LEVEL,
			 "keyboard_early_suspend_work: Nanosic_screen_state_notify err:%d\n",
			 ret);
}

static void keyboard_suspend_work(struct work_struct *work)
{
	dbgprint(ALERT_LEVEL, "keyboard_suspend_work: enter\n");
	g_panel_status = false;
	Nanosic_GPIO_sleep(false);
	pm_relax(gI2c_client->dev);
}

static int xiaomi_keyboard_pm_suspend(struct device *dev)
{
	int ret = 0;
	dbgprint(ALERT_LEVEL,
		 "xiaomi_keyboard_pm_suspend: enter, enable_irq_wake\n");
	ret = enable_irq_wake(mdata->irq); //i2c_client->irqno
	if (ret < 0)
		dbgprint(
			ALERT_LEVEL,
			"xiaomi_keyboard_pm_suspend: enter, enable_irq_wake irq failed\n");
	ret = enable_irq_wake(g_wakeup_irqno);
	if (ret < 0)
		dbgprint(
			ALERT_LEVEL,
			"xiaomi_keyboard_pm_suspend: enter, enable_irq_wake wakeup_irqno failed\n");
	mdata->dev_pm_suspend = true;
	return ret;
}

static int xiaomi_keyboard_pm_resume(struct device *dev)
{
	int ret = 0;
	dbgprint(ALERT_LEVEL,
		 "xiaomi_keyboard_pm_resume enter, disable_irq_wake\n");
	ret = disable_irq_wake(mdata->irq); //i2c_client->irqno
	if (ret < 0)
		dbgprint(
			ALERT_LEVEL,
			"xiaomi_keyboard_pm_resume: enter, disable_irq_wake irq failed\n");
	ret = disable_irq_wake(g_wakeup_irqno);
	if (ret < 0)
		dbgprint(
			ALERT_LEVEL,
			"xiaomi_keyboard_pm_resume: enter, disable_irq_wake wakeup_irqno failed\n");

	mdata->dev_pm_suspend = false;
	return ret;
}

static const struct dev_pm_ops xiaomi_keyboard_pm_ops = {
	.suspend = xiaomi_keyboard_pm_suspend,
	.resume = xiaomi_keyboard_pm_resume,
};

static int xiaomi_keyboard_remove(void)
{
	dbgprint(ALERT_LEVEL, "xiaomi_keyboard_remove: enter\n");
	if (!mdata)
		return 0;
	if (mdata->notifier_cookie)
		panel_event_notifier_unregister(mdata->notifier_cookie);
	if (mdata->event_wq)
		destroy_workqueue(mdata->event_wq);
	kfree(mdata);
	mdata = NULL;
	return 0;
}

static void keyboard_panel_notifier_callback(enum panel_event_notifier_tag tag,
					     struct panel_event_notification *notification,
					     void *data)
{
	struct xiaomi_keyboard_data *mdata = data;

	if (!notification || !mdata)
		return;

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_BLANK_LP:
		dbgprint(ALERT_LEVEL, "keyboard suspend, early %d\n",
			 notification->notif_data.early_trigger);
		queue_work(mdata->event_wq,
			   notification->notif_data.early_trigger ?
				   &mdata->early_suspend_work :
				   &mdata->suspend_work);
		break;
	case DRM_PANEL_EVENT_UNBLANK:
		dbgprint(ALERT_LEVEL, "keyboard resume, early %d\n",
			 notification->notif_data.early_trigger);
		queue_work(mdata->event_wq,
			   notification->notif_data.early_trigger ?
				   &mdata->early_resume_work :
				   &mdata->resume_work);
		break;
	default:
		break;
	}
}

late_initcall(nanosic_driver_init);
module_exit(nanosic_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("bin.yuan@nanosic.com");
