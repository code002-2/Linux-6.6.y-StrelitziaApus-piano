// SPDX-License-Identifier: GPL-2.0
/*
 * Southchip SC96231 pen charger on the MCA wireless class.
 *
 * The SC96231 runs as a wireless transmitter for the stylus that docks on
 * the side of the tablet. Hall sensors next to the dock tell whether the pen
 * is attached, and another set whether it sits misplaced; the reverse charge
 * strategy of the MCA decides from those when to power the transmitter. The
 * chip reports the charge level, BLE MAC and charge state of the pen through
 * its customer registers.
 *
 * The firmware of the chip lives in its MTP, programmed at the factory or by
 * the stock kernel; this driver only reads its version.
 *
 * Written by sidharthify (Sidharth Sharma) <wednisegit@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "sc96231_tx"
#endif

/* customer registers, 32-bit aligned in the SRAM of the chip */
#define SC96231_REG_FW_VER		0x0004
#define SC96231_REG_FW_CHECK		0x001c
#define SC96231_REG_CMD			0x0100
#define SC96231_REG_IRQ_FLAG		0x0108
#define SC96231_REG_IRQ_CLR		0x010c
#define SC96231_REG_VOUT		0x0160
#define SC96231_REG_IOUT		0x0164
#define SC96231_REG_TDIE		0x0178
#define SC96231_REG_SS			0x01a0
#define SC96231_REG_IIC_CHECK		0x01cf
#define SC96231_REG_BATT_LEVEL		0x01d0
#define SC96231_REG_CHARGE_TYPE		0x01d8
#define SC96231_REG_MAC_0_1		0x01de
#define SC96231_REG_MAC_2_5		0x01e0

#define SC96231_CMD_TX_DISABLE		BIT(18)
#define SC96231_CMD_TX_ENABLE		BIT(19)

#define SC96231_IIC_CHECK_VAL		0x55
#define SC96231_PEN_SOC_MASK		GENMASK(6, 0)
#define SC96231_PEN_FULL		BIT(7)
#define SC96231_PEN_SOC_INVALID		255
#define SC96231_FW_CHECK_OK		7

#define SC96231_POWER_ON_DELAY_MS	13000
#define SC96231_HALL_DELAY_MS		1000

#define SC96231_PEN_PLACE_ERR_HALL	2

/* rev charge interrupt flags of the MCA, by status bit of the chip */
static const struct {
	u32 mask;
	int flag;
} sc96231_irq_map[] = {
	{ BIT(21), 1 },		/* ping */
	{ BIT(16), 2 },		/* power transfer */
	{ BIT(19), 3 },		/* CEP timeout */
	{ BIT(15), 4 },		/* power removed */
	{ BIT(18), 6 },		/* transmitter detected */
	{ BIT(17), 9 },		/* FOD */
	{ BIT(14), 12 },	/* receiver detected */
	{ BIT(24), 13 },	/* config packet */
	{ BIT(27), 14 },	/* charge state update */
	{ BIT(25), 15 },	/* BLE connected */
};

enum sc96231_gpio {
	SC96231_GPIO_INT,
	SC96231_GPIO_HALL3,
	SC96231_GPIO_HALL4,
	SC96231_GPIO_HALL3_S,
	SC96231_GPIO_HALL4_S,
	SC96231_GPIO_HALL_PPE_N,
	SC96231_GPIO_HALL_PPE_S,
	SC96231_GPIO_TXON,
	SC96231_GPIO_BOOST,
	SC96231_GPIO_KB_FRONT,
	SC96231_GPIO_KB_BACK,
	SC96231_GPIO_MAX,
};

static const char * const sc96231_gpio_names[SC96231_GPIO_MAX] = {
	[SC96231_GPIO_INT]		= "rx-int",
	[SC96231_GPIO_HALL3]		= "hall3-int",
	[SC96231_GPIO_HALL4]		= "hall4-int",
	[SC96231_GPIO_HALL3_S]		= "hall3-s-int",
	[SC96231_GPIO_HALL4_S]		= "hall4-s-int",
	[SC96231_GPIO_HALL_PPE_N]	= "hall-ppe-int",
	[SC96231_GPIO_HALL_PPE_S]	= "hall-ppe-s-int",
	[SC96231_GPIO_TXON]		= "reverse-txon-gpio",
	[SC96231_GPIO_BOOST]		= "reverse-boost-gpio",
	[SC96231_GPIO_KB_FRONT]		= "keyboard-front-gpio",
	[SC96231_GPIO_KB_BACK]		= "keyboard-back-gpio",
};

static const char * const sc96231_pinctrl_states[] = {
	"wls_int_pull_up", "wls_sw_config", "wls_boost_config",
	"hall3_int_pull_up", "hall4_int_pull_up", "hall_ppe_int_pull_up",
	"hall3_s_int_pull_up", "hall4_s_int_pull_up", "hall_ppe_s_int_pull_up",
	"lp_nen_pull_up",
};

struct sc96231 {
	struct device *dev;
	struct regmap *regmap;
	struct mutex i2c_rw_lock;
	struct mutex irq_lock;
	struct delayed_work irq_work;
	struct delayed_work hall_work;
	struct delayed_work ppe_work;
	struct delayed_work power_on_work;

	int gpios[SC96231_GPIO_MAX];
	int role;
	int project_vendor;
	bool txon_inverted;
	bool power_on_checked;

	/* the boost and the transmitter are powered */
	bool reverse_chg_en;
	int hall3, hall4, hall3_s, hall4_s, hall_ppe_n, hall_ppe_s;
	bool hall3_online, hall4_online;
	int pen_place_err;
	u8 fw_ver[3];
};

static int sc96231_read(struct sc96231 *sc, u16 reg, void *data, int len)
{
	u16 start = round_down(reg, 4);
	int size = round_up(reg + len, 4) - start;
	u8 buf[16];
	int ret;

	if (size > sizeof(buf))
		return -EINVAL;

	mutex_lock(&sc->i2c_rw_lock);
	ret = regmap_raw_read(sc->regmap, start, buf, size);
	mutex_unlock(&sc->i2c_rw_lock);
	if (ret) {
		mca_log_err("failed to read 0x%04x: %d\n", reg, ret);
		return ret;
	}

	memcpy(data, buf + (reg - start), len);
	return 0;
}

static int sc96231_write(struct sc96231 *sc, u16 reg, const void *data,
			 int len)
{
	u16 start = round_down(reg, 4);
	int size = round_up(reg + len, 4) - start;
	u8 buf[16];
	int ret = 0;

	if (size > sizeof(buf))
		return -EINVAL;

	mutex_lock(&sc->i2c_rw_lock);
	if (start != reg || size != len)
		ret = regmap_raw_read(sc->regmap, start, buf, size);
	if (!ret) {
		memcpy(buf + (reg - start), data, len);
		ret = regmap_raw_write(sc->regmap, start, buf, size);
	}
	mutex_unlock(&sc->i2c_rw_lock);
	if (ret)
		mca_log_err("failed to write 0x%04x: %d\n", reg, ret);

	return ret;
}

static int sc96231_check_i2c(struct sc96231 *sc)
{
	u8 val = SC96231_IIC_CHECK_VAL;
	int ret;

	ret = sc96231_write(sc, SC96231_REG_IIC_CHECK, &val, 1);
	if (ret)
		return ret;

	msleep(20);
	ret = sc96231_read(sc, SC96231_REG_IIC_CHECK, &val, 1);
	if (ret)
		return ret;

	return val == SC96231_IIC_CHECK_VAL ? 0 : -EIO;
}

static int sc96231_read_fw_ver(struct sc96231 *sc, u32 *ver)
{
	int ret;

	ret = sc96231_read(sc, SC96231_REG_FW_VER, ver, sizeof(*ver));
	if (ret)
		return ret;

	sc->fw_ver[0] = *ver >> 8;
	sc->fw_ver[1] = *ver;
	sc->fw_ver[2] = *ver >> 16;
	return 0;
}

/* the pen talks to the transmitter only while a hall sees it docked */
static bool sc96231_tx_active(struct sc96231 *sc)
{
	return (sc->hall3_online || sc->hall4_online) && sc->reverse_chg_en;
}

static void sc96231_set_gpio(struct sc96231 *sc, int idx, int val)
{
	int gpio = sc->gpios[idx];

	if (gpio < 0)
		return;

	gpio_direction_output(gpio, val);
	mca_log_info("%s is %d\n", sc96231_gpio_names[idx],
		     gpio_get_value(gpio));
}

static int sc96231_get_gpio(struct sc96231 *sc, int idx, int def)
{
	if (sc->gpios[idx] < 0)
		return def;

	return gpio_get_value(sc->gpios[idx]);
}

static int sc96231_enable_reverse_chg(bool en, void *data)
{
	u32 cmd = en ? SC96231_CMD_TX_ENABLE : SC96231_CMD_TX_DISABLE;
	int ret;

	ret = sc96231_write(data, SC96231_REG_CMD, &cmd, sizeof(cmd));
	mca_log_info("%s the transmitter: %d\n", en ? "enable" : "disable",
		     ret);

	return ret;
}

static int sc96231_ops_check_i2c(void *data)
{
	return sc96231_check_i2c(data);
}

static int sc96231_set_external_boost_enable(bool en, void *data)
{
	struct sc96231 *sc = data;

	mca_log_info("external boost %d\n", en);
	sc->reverse_chg_en = en;

	/* the boost comes up after the switch, and goes down before it */
	if (en) {
		sc96231_set_gpio(sc, SC96231_GPIO_TXON, en);
		sc96231_set_gpio(sc, SC96231_GPIO_BOOST, en);
	} else {
		sc96231_set_gpio(sc, SC96231_GPIO_BOOST, en);
		sc96231_set_gpio(sc, SC96231_GPIO_TXON, en);
	}

	return 0;
}

static int sc96231_set_hboost_enable(bool en, void *data)
{
	struct sc96231 *sc = data;

	mca_log_info("hboost %d\n", en);
	sc->reverse_chg_en = en;
	sc96231_set_gpio(sc, SC96231_GPIO_TXON, en ^ sc->txon_inverted);

	return 0;
}

static int sc96231_get_reverse_chg_en(bool *en, void *data)
{
	struct sc96231 *sc = data;

	*en = (sc->hall3_online || sc->hall4_online) && sc->reverse_chg_en;
	return 0;
}

static int sc96231_get_tx_ss(int *ss, void *data)
{
	struct sc96231 *sc = data;
	u8 val = 0;

	*ss = 0;
	if (sc96231_tx_active(sc) &&
	    !sc96231_read(sc, SC96231_REG_SS, &val, sizeof(val)))
		*ss = val;

	return 0;
}

static int sc96231_get_tx_vout(int *vout, void *data)
{
	struct sc96231 *sc = data;
	u16 val = 0;

	*vout = 0;
	if (sc96231_tx_active(sc) &&
	    !sc96231_read(sc, SC96231_REG_VOUT, &val, sizeof(val)))
		*vout = val;

	return 0;
}

static int sc96231_get_tx_iout(int *iout, void *data)
{
	struct sc96231 *sc = data;
	u16 val = 0;

	*iout = 0;
	if (sc96231_tx_active(sc) &&
	    !sc96231_read(sc, SC96231_REG_IOUT, &val, sizeof(val)))
		*iout = val;

	return 0;
}

static int sc96231_get_tx_tdie(int *tdie, void *data)
{
	struct sc96231 *sc = data;
	s16 val = 0;

	*tdie = 0;
	if (sc96231_tx_active(sc) &&
	    !sc96231_read(sc, SC96231_REG_TDIE, &val, sizeof(val)))
		*tdie = val;

	return 0;
}

static int sc96231_read_batt_level(struct sc96231 *sc, int *soc, bool *full)
{
	u8 val = 0;
	int ret;

	ret = sc96231_read(sc, SC96231_REG_BATT_LEVEL, &val, sizeof(val));
	if (ret)
		return ret;

	*soc = val & SC96231_PEN_SOC_MASK;
	if (*soc > 100)
		*soc = SC96231_PEN_SOC_INVALID;
	*full = !!(val & SC96231_PEN_FULL);

	return 0;
}

static int sc96231_get_pen_soc(int *soc, void *data)
{
	struct sc96231 *sc = data;
	bool full;

	*soc = 0;
	if (sc96231_tx_active(sc) && sc96231_read_batt_level(sc, soc, &full))
		*soc = SC96231_PEN_SOC_INVALID;

	return 0;
}

static int sc96231_get_pen_full_flag(bool *full, void *data)
{
	struct sc96231 *sc = data;
	int soc;

	*full = false;
	if (sc96231_tx_active(sc) && sc96231_read_batt_level(sc, &soc, full))
		*full = false;

	return 0;
}

/* six bytes, as the pen reports them */
static int sc96231_get_pen_mac(u8 *mac, void *data)
{
	struct sc96231 *sc = data;
	u8 lo[2], hi[4];

	memset(mac, 0, 6);
	if (!sc96231_tx_active(sc))
		return 0;

	if (sc96231_read(sc, SC96231_REG_MAC_0_1, lo, sizeof(lo)) ||
	    sc96231_read(sc, SC96231_REG_MAC_2_5, hi, sizeof(hi)) ||
	    !memchr_inv(hi, 0, sizeof(hi))) {
		mca_log_err("failed to get the pen mac\n");
		return 0;
	}

	memcpy(mac, lo, sizeof(lo));
	memcpy(mac + sizeof(lo), hi, sizeof(hi));
	return 0;
}

static int sc96231_set_charge_type(int type, void *data)
{
	u8 val = type;

	return sc96231_write(data, SC96231_REG_CHARGE_TYPE, &val, sizeof(val));
}

static int sc96231_get_pen_hall3(int *val, void *data)
{
	*val = ((struct sc96231 *)data)->hall3;
	return 0;
}

static int sc96231_get_pen_hall4(int *val, void *data)
{
	*val = ((struct sc96231 *)data)->hall4;
	return 0;
}

static int sc96231_get_pen_hall3_s(int *val, void *data)
{
	*val = ((struct sc96231 *)data)->hall3_s;
	return 0;
}

static int sc96231_get_pen_hall4_s(int *val, void *data)
{
	*val = ((struct sc96231 *)data)->hall4_s;
	return 0;
}

static int sc96231_get_pen_hall_ppe_n(int *val, void *data)
{
	*val = ((struct sc96231 *)data)->hall_ppe_n;
	return 0;
}

static int sc96231_get_pen_hall_ppe_s(int *val, void *data)
{
	*val = ((struct sc96231 *)data)->hall_ppe_s;
	return 0;
}

static int sc96231_get_pen_place_err(int *err, void *data)
{
	*err = ((struct sc96231 *)data)->pen_place_err;
	return 0;
}

static int sc96231_set_pen_place_err(int err, void *data)
{
	((struct sc96231 *)data)->pen_place_err = err;
	return 0;
}

static int sc96231_get_fw_version(char *buf, void *data)
{
	struct sc96231 *sc = data;

	scnprintf(buf, 10, "%02x.%02x.%02x", sc->fw_ver[2], sc->fw_ver[0],
		  sc->fw_ver[1]);
	return 0;
}

/* there is no image to update to, the chip keeps the one it has */
static int sc96231_check_firmware_state(bool *update, void *data)
{
	struct sc96231 *sc = data;
	u32 ver = 0;
	int ret;

	*update = false;
	ret = sc96231_check_i2c(sc) ?: sc96231_read_fw_ver(sc, &ver);
	if (ret)
		mca_log_err("i2c error: %d\n", ret);
	else
		mca_log_info("firmware %02x.%02x.%02x\n", sc->fw_ver[2],
			     sc->fw_ver[0], sc->fw_ver[1]);

	return ret;
}

static int sc96231_get_fw_version_check(u8 *result, void *data)
{
	struct sc96231 *sc = data;
	u32 ver = 0, check = 0;
	int ret;

	ret = sc96231_read(sc, SC96231_REG_FW_CHECK, &check, sizeof(check)) ?:
	      sc96231_read_fw_ver(sc, &ver);
	if (ret) {
		mca_log_err("failed to check the firmware: %d\n", ret);
		return ret;
	}

	if (ver && ver == check)
		*result = SC96231_FW_CHECK_OK;

	return 0;
}

static int sc96231_download_fw(void *data)
{
	return -EOPNOTSUPP;
}

static struct platform_class_wireless_ops sc96231_wls_ops = {
	.wls_enable_reverse_chg = sc96231_enable_reverse_chg,
	.wls_get_fw_version = sc96231_get_fw_version,
	.wls_check_i2c_is_ok = sc96231_ops_check_i2c,
	.wls_get_fw_version_check = sc96231_get_fw_version_check,
	.wls_download_fw = sc96231_download_fw,
	.wls_check_firmware_state = sc96231_check_firmware_state,
	.wls_set_external_boost_enable = sc96231_set_external_boost_enable,
	.wls_get_pen_hall3 = sc96231_get_pen_hall3,
	.wls_get_pen_hall3_s = sc96231_get_pen_hall3_s,
	.wls_get_pen_hall4 = sc96231_get_pen_hall4,
	.wls_get_pen_hall4_s = sc96231_get_pen_hall4_s,
	.wls_get_pen_hall_ppe_n = sc96231_get_pen_hall_ppe_n,
	.wls_get_pen_hall_ppe_s = sc96231_get_pen_hall_ppe_s,
	.wls_get_pen_full_flag = sc96231_get_pen_full_flag,
	.wls_get_pen_place_err = sc96231_get_pen_place_err,
	.wls_set_pen_place_err = sc96231_set_pen_place_err,
	.wls_get_pen_mac = sc96231_get_pen_mac,
	.wls_get_pen_soc = sc96231_get_pen_soc,
	.wls_get_reverse_chg_en = sc96231_get_reverse_chg_en,
	.wls_set_hboost_enable = sc96231_set_hboost_enable,
	.wls_set_charge_type = sc96231_set_charge_type,
	.wls_get_tx_iout = sc96231_get_tx_iout,
	.wls_get_tx_vout = sc96231_get_tx_vout,
	.wls_get_ss_voltage = sc96231_get_tx_ss,
	.wls_get_temp = sc96231_get_tx_tdie,
};

static void sc96231_irq_work(struct work_struct *work)
{
	struct sc96231 *sc = container_of(to_delayed_work(work),
					  struct sc96231, irq_work);
	u32 flag = 0;
	int i;

	mutex_lock(&sc->irq_lock);
	if (sc96231_read(sc, SC96231_REG_IRQ_FLAG, &flag, sizeof(flag))) {
		mutex_unlock(&sc->irq_lock);
		return;
	}
	sc96231_write(sc, SC96231_REG_IRQ_CLR, &flag, sizeof(flag));
	mca_log_info("interrupt 0x%08x\n", flag);

	for (i = 0; i < ARRAY_SIZE(sc96231_irq_map); i++) {
		if (flag & sc96231_irq_map[i].mask)
			mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS,
						  MCA_EVENT_WIRELESS_INT_CHANGE,
						  sc96231_irq_map[i].flag);
	}
	mutex_unlock(&sc->irq_lock);
}

/* the halls are active low: a magnet pulls the line down */
static void sc96231_hall_work(struct work_struct *work)
{
	struct sc96231 *sc = container_of(to_delayed_work(work),
					  struct sc96231, hall_work);
	int value;

	if (!sc->power_on_checked)
		return;

	sc->hall3 = sc96231_get_gpio(sc, SC96231_GPIO_HALL3, 1);
	sc->hall4 = sc96231_get_gpio(sc, SC96231_GPIO_HALL4, 1);
	mca_log_info("hall3 %d, hall4 %d\n", sc->hall3, sc->hall4);

	if (!sc->hall3 || !sc->hall4) {
		if (sc->hall3_online || sc->hall4_online)
			return;
		sc->hall3_online = !sc->hall3;
		sc->hall4_online = !sc->hall4;
		value = sc->hall4_online << 4 | sc->hall3_online << 3;
		mca_log_info("pen attached\n");
	} else {
		if (!sc->hall3_online && !sc->hall4_online)
			return;
		sc->hall3_online = false;
		sc->hall4_online = false;
		value = 0;
		mca_log_info("pen detached\n");
	}

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS,
				  MCA_EVENT_PEN_HALL_CHANGE, value);
}

/*
 * The pen sits wrong when one of the placement halls sees it, or when the
 * keyboard cover is folded over the dock.
 */
static void sc96231_ppe_work(struct work_struct *work)
{
	struct sc96231 *sc = container_of(to_delayed_work(work),
					  struct sc96231, ppe_work);
	int kb_front, kb_back;
	bool kb_clear;

	sc->hall3_s = sc96231_get_gpio(sc, SC96231_GPIO_HALL3_S, 1);
	sc->hall4_s = sc96231_get_gpio(sc, SC96231_GPIO_HALL4_S, 1);
	sc->hall_ppe_n = sc96231_get_gpio(sc, SC96231_GPIO_HALL_PPE_N, 1);
	sc->hall_ppe_s = sc96231_get_gpio(sc, SC96231_GPIO_HALL_PPE_S, 1);
	kb_front = sc96231_get_gpio(sc, SC96231_GPIO_KB_FRONT, 1);
	kb_back = sc96231_get_gpio(sc, SC96231_GPIO_KB_BACK, 1);

	kb_clear = !kb_back || !kb_front || sc->hall4_s;
	if (!sc->hall_ppe_s || !sc->hall_ppe_n || !sc->hall3_s || !kb_clear)
		sc->pen_place_err = SC96231_PEN_PLACE_ERR_HALL;
	else
		sc->pen_place_err = 0;

	mca_log_info("hall3_s %d, hall4_s %d, ppe_n %d, ppe_s %d, kb %d/%d, err %d\n",
		     sc->hall3_s, sc->hall4_s, sc->hall_ppe_n, sc->hall_ppe_s,
		     kb_front, kb_back, sc->pen_place_err);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS,
				  MCA_EVENT_PEN_PPE_HALL_CHANGE,
				  sc->pen_place_err);
}

/* the halls settle during boot, look at them once things are up */
static void sc96231_power_on_work(struct work_struct *work)
{
	struct sc96231 *sc = container_of(to_delayed_work(work),
					  struct sc96231, power_on_work);

	sc->power_on_checked = true;
	schedule_delayed_work(&sc->hall_work,
			      msecs_to_jiffies(SC96231_HALL_DELAY_MS));
}

static irqreturn_t sc96231_isr(int irq, void *data)
{
	struct sc96231 *sc = data;

	schedule_delayed_work(&sc->irq_work, 0);
	return IRQ_HANDLED;
}

static irqreturn_t sc96231_hall_isr(int irq, void *data)
{
	struct sc96231 *sc = data;

	schedule_delayed_work(&sc->hall_work, 0);
	return IRQ_HANDLED;
}

static irqreturn_t sc96231_ppe_isr(int irq, void *data)
{
	struct sc96231 *sc = data;

	schedule_delayed_work(&sc->ppe_work, 0);
	return IRQ_HANDLED;
}

static int sc96231_request_irq(struct sc96231 *sc, int idx,
			       irq_handler_t handler, unsigned long flags)
{
	int gpio = sc->gpios[idx];
	int irq, ret;

	if (gpio < 0)
		return 0;

	ret = devm_gpio_request_one(sc->dev, gpio, GPIOF_IN,
				    sc96231_gpio_names[idx]);
	if (ret)
		return ret;

	irq = gpio_to_irq(gpio);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(sc->dev, irq, NULL, handler,
					flags | IRQF_ONESHOT,
					sc96231_gpio_names[idx], sc);
	if (ret)
		return ret;

	enable_irq_wake(irq);
	return 0;
}

static int sc96231_init_irqs(struct sc96231 *sc)
{
	const unsigned long edges = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;

	return sc96231_request_irq(sc, SC96231_GPIO_INT, sc96231_isr,
				   IRQF_TRIGGER_FALLING) ?:
	       sc96231_request_irq(sc, SC96231_GPIO_HALL3, sc96231_hall_isr,
				   edges) ?:
	       sc96231_request_irq(sc, SC96231_GPIO_HALL4, sc96231_hall_isr,
				   edges) ?:
	       sc96231_request_irq(sc, SC96231_GPIO_HALL3_S, sc96231_ppe_isr,
				   edges) ?:
	       sc96231_request_irq(sc, SC96231_GPIO_HALL4_S, sc96231_ppe_isr,
				   edges) ?:
	       sc96231_request_irq(sc, SC96231_GPIO_HALL_PPE_N, sc96231_ppe_isr,
				   edges) ?:
	       sc96231_request_irq(sc, SC96231_GPIO_HALL_PPE_S, sc96231_ppe_isr,
				   edges);
}

static int sc96231_parse_dt(struct sc96231 *sc)
{
	struct device_node *np = sc->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	struct pinctrl *pinctrl;
	struct pinctrl_state *state;
	int i, ret;

	mca_parse_dts_u32(np, "project_vendor", &sc->project_vendor, 0);
	mca_parse_dts_u32(np, "rx_role", &sc->role, 0);

	/* one hardware revision switches the transmitter on with a low */
	sc->txon_inverted = hwid && hwid->platform_version == 9 &&
			    !hwid->build_version;

	pinctrl = devm_pinctrl_get(sc->dev);
	if (!IS_ERR(pinctrl)) {
		for (i = 0; i < ARRAY_SIZE(sc96231_pinctrl_states); i++) {
			state = pinctrl_lookup_state(pinctrl,
						     sc96231_pinctrl_states[i]);
			if (!IS_ERR(state))
				pinctrl_select_state(pinctrl, state);
		}
	}

	for (i = 0; i < SC96231_GPIO_MAX; i++)
		sc->gpios[i] = of_get_named_gpio(np, sc96231_gpio_names[i], 0);

	if (sc->gpios[SC96231_GPIO_INT] < 0) {
		mca_log_err("no interrupt gpio\n");
		return -EINVAL;
	}

	if (sc->gpios[SC96231_GPIO_TXON] >= 0) {
		ret = devm_gpio_request_one(sc->dev, sc->gpios[SC96231_GPIO_TXON],
				sc->txon_inverted ? GPIOF_OUT_INIT_HIGH :
						    GPIOF_OUT_INIT_LOW,
				sc96231_gpio_names[SC96231_GPIO_TXON]);
		if (ret)
			return ret;
	}

	if (sc->gpios[SC96231_GPIO_BOOST] >= 0) {
		ret = devm_gpio_request_one(sc->dev,
				sc->gpios[SC96231_GPIO_BOOST],
				GPIOF_OUT_INIT_LOW,
				sc96231_gpio_names[SC96231_GPIO_BOOST]);
		if (ret)
			return ret;
	}

	/*
	 * The keyboard hall sensors belong to xiaomi-hall, which reports them
	 * as switches. They are only read here, so leave them unrequested.
	 */

	return 0;
}

static const struct regmap_config sc96231_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
};

static int sc96231_probe(struct i2c_client *client)
{
	struct sc96231 *sc;
	u32 ver = 0;
	int ret;

	sc = devm_kzalloc(&client->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->dev = &client->dev;
	i2c_set_clientdata(client, sc);
	mutex_init(&sc->i2c_rw_lock);
	mutex_init(&sc->irq_lock);
	INIT_DELAYED_WORK(&sc->irq_work, sc96231_irq_work);
	INIT_DELAYED_WORK(&sc->hall_work, sc96231_hall_work);
	INIT_DELAYED_WORK(&sc->ppe_work, sc96231_ppe_work);
	INIT_DELAYED_WORK(&sc->power_on_work, sc96231_power_on_work);
	sc->hall3 = sc->hall4 = 1;
	sc->hall3_s = sc->hall4_s = sc->hall_ppe_n = sc->hall_ppe_s = 1;

	sc->regmap = devm_regmap_init_i2c(client, &sc96231_regmap_config);
	if (IS_ERR(sc->regmap))
		return PTR_ERR(sc->regmap);

	ret = sc96231_parse_dt(sc);
	if (ret)
		return dev_err_probe(sc->dev, ret, "failed to parse the DT\n");

	if (!sc96231_read_fw_ver(sc, &ver))
		mca_log_info("firmware %02x.%02x.%02x\n", sc->fw_ver[2],
			     sc->fw_ver[0], sc->fw_ver[1]);

	ret = sc96231_init_irqs(sc);
	if (ret)
		return dev_err_probe(sc->dev, ret, "failed to set up the irqs\n");

	device_init_wakeup(sc->dev, true);

	ret = platform_class_wireless_register_ops(sc->role, sc,
						   &sc96231_wls_ops);
	if (ret)
		return ret;

	schedule_delayed_work(&sc->power_on_work,
			      msecs_to_jiffies(SC96231_POWER_ON_DELAY_MS));
	schedule_delayed_work(&sc->ppe_work, 0);

	return 0;
}

static void sc96231_remove(struct i2c_client *client)
{
	struct sc96231 *sc = i2c_get_clientdata(client);

	/* the class must not call into the driver once it is gone */
	platform_class_wireless_unregister_ops(sc->role, sc);
	cancel_delayed_work_sync(&sc->power_on_work);
	cancel_delayed_work_sync(&sc->hall_work);
	cancel_delayed_work_sync(&sc->ppe_work);
	cancel_delayed_work_sync(&sc->irq_work);
}

static const struct of_device_id sc96231_of_match[] = {
	{ .compatible = "sc,sc96231-wireless-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, sc96231_of_match);

static const struct i2c_device_id sc96231_i2c_id[] = {
	{ "sc96231", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc96231_i2c_id);

static struct i2c_driver sc96231_driver = {
	.driver = {
		.name = "sc96231-wireless-charger",
		.of_match_table = sc96231_of_match,
	},
	.probe = sc96231_probe,
	.remove = sc96231_remove,
	.id_table = sc96231_i2c_id,
};
module_i2c_driver(sc96231_driver);

MODULE_DESCRIPTION("MCA Southchip SC96231 pen charger driver");
MODULE_LICENSE("GPL");
