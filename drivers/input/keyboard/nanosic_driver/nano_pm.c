// SPDX-License-Identifier: GPL-2.0
/*
 * Power management of the 803x
 *
 * The 803x sleeps while its sleep pin is low. Before every transfer the
 * host raises the pin and waits for the controller to raise its interrupt
 * line, and once the bus has been idle for a while it drops the pin again.
 * A controller that never wakes up has no firmware running, so it is reset
 * and gets its firmware uploaded again.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include "nano_macro.h"

/* how long the 803x stays awake after the last transfer */
#define PM_IDLE_SCREEN_ON_MS 20000
#define PM_IDLE_SCREEN_OFF_MS 1000

/* how long the 803x gets to raise its interrupt line once woken up */
#define PM_WAKEUP_TIMEOUT_MS 100

static void Nanosic_PM_expire(struct timer_list *t)
{
	struct nano_i2c_client *i2c_client = from_timer(i2c_client, t, pm_timer);

	dbgprint(DEBUG_LEVEL, "going to sleep\n");
	Nanosic_GPIO_sleep(false);
	pm_relax(i2c_client->dev);
}

static void Nanosic_PM_hold(struct nano_i2c_client *i2c_client)
{
	unsigned int idle_ms = g_panel_status ? PM_IDLE_SCREEN_ON_MS :
						PM_IDLE_SCREEN_OFF_MS;

	pm_stay_awake(i2c_client->dev);
	mod_timer(&i2c_client->pm_timer, jiffies + msecs_to_jiffies(idle_ms));
}

void Nanosic_PM_try_wakeup(struct nano_i2c_client *i2c_client)
{
	int i;

	mutex_lock(&i2c_client->pm_mutex);

	Nanosic_PM_hold(i2c_client);
	Nanosic_GPIO_sleep(true);

	if (Nanosic_GPIO_irqpin_get() <= 0) {
		dbgprint(DEBUG_LEVEL, "waiting for wakeup\n");
		for (i = 0; i < PM_WAKEUP_TIMEOUT_MS; i++) {
			usleep_range(1000, 1100);
			if (Nanosic_GPIO_irqpin_get() > 0)
				break;
		}

		if (i == PM_WAKEUP_TIMEOUT_MS) {
			dbgprint(ERROR_LEVEL, "803x did not wake up, reloading it\n");
			Nanosic_GPIO_reset();
			Nanosic_firmware_load(i2c_client);
			/* let the firmware start */
			msleep(500);
			Nanosic_PM_hold(i2c_client);
		}

		/* give the i2c controller of the 803x time to come up */
		usleep_range(5000, 5500);
	}

	mutex_unlock(&i2c_client->pm_mutex);
}

void Nanosic_PM_init(struct nano_i2c_client *i2c_client)
{
	mutex_init(&i2c_client->pm_mutex);
	timer_setup(&i2c_client->pm_timer, Nanosic_PM_expire, 0);
}

void Nanosic_PM_free(struct nano_i2c_client *i2c_client)
{
	timer_delete_sync(&i2c_client->pm_timer);
	pm_relax(i2c_client->dev);
}
