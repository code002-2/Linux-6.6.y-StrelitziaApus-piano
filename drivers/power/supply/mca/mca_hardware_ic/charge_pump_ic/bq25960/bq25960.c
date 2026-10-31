// SPDX-License-Identifier: GPL-2.0
/*
 * TI BQ25960 charge pump on the MCA charge pump class.
 *
 * Some yupei boards carry a BQ25960 in place of the SC8541. Reverse charging
 * goes through a second, hidden register page of the chip at I2C address
 * 0x3f, which the device tree describes as the "ti,bq25960h-master" node;
 * while it is on, the status of the chip is read through that page too.
 *
 * Written by sidharthify (Sidharth Sharma) <wednisegit@gmail.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/platform/platform_cp_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "cp_bq25960"
#endif

#define BQ25960_REG_BATOVP		0x00
#define BQ25960_REG_BATOVP_ALM		0x01
#define BQ25960_REG_BATOCP		0x02
#define BQ25960_REG_BATOCP_ALM		0x03
#define BQ25960_REG_BATUCP_ALM		0x04
#define BQ25960_REG_BUSUCP		0x05
#define BQ25960_REG_BUSOVP		0x06
#define BQ25960_REG_BUSOVP_ALM		0x07
#define BQ25960_REG_BUSOCP		0x08
#define BQ25960_REG_BUSOCP_ALM		0x09
#define BQ25960_REG_TEMP_CTRL		0x0a
#define BQ25960_REG_TDIE_ALM		0x0b
#define BQ25960_REG_VAC_OVP		0x0e
#define BQ25960_REG_CTRL2		0x0f
#define BQ25960_REG_CTRL3		0x10
#define BQ25960_REG_CTRL4		0x11
#define BQ25960_REG_CTRL5		0x12
#define BQ25960_REG_STAT2		0x15
#define BQ25960_REG_STAT4		0x17
#define BQ25960_REG_FLAG1		0x18
#define BQ25960_REG_FLAG2		0x19
#define BQ25960_REG_FLAG3		0x1a
#define BQ25960_REG_FLAG4		0x1b
#define BQ25960_REG_MASK4		0x21
#define BQ25960_REG_PART_INFO		0x22
#define BQ25960_REG_ADC_CTRL		0x23
#define BQ25960_REG_ADC_FN_DIS		0x24
#define BQ25960_REG_ADC_BASE		0x25

#define BQ25960_DIS			BIT(7)
#define BQ25960_BUSUCP_250MA		BIT(6)
#define BQ25960_VBUS_PD			BIT(7)
#define BQ25960_TSBUS_DIS		BIT(2)
#define BQ25960_TSBAT_DIS		BIT(3)
#define BQ25960_REG_RST			BIT(7)
#define BQ25960_EN_OTG			BIT(5)
#define BQ25960_CHG_EN			BIT(4)
#define BQ25960_EN_BYPASS		BIT(3)
#define BQ25960_DIS_ACDRV_BOTH		BIT(2)
#define BQ25960_ACDRV1_EN		BIT(1)
#define BQ25960_WATCHDOG_DIS		BIT(2)
#define BQ25960_SS_TIMEOUT		GENMASK(6, 4)
#define BQ25960_VOUT_OVP		GENMASK(6, 5)
#define BQ25960_VAC_OVP_MASK		GENMASK(7, 5)
#define BQ25960_VOUT_PRESENT		BIT(5)
#define BQ25960_VIN_PRESENT		GENMASK(4, 2)
#define BQ25960_CP_SWITCHING		BIT(6)
#define BQ25960_VBUS_ERRHI		BIT(4)
#define BQ25960_IBAT_OCP_FLAG		BIT(4)
#define BQ25960_VBUS_OVP_FLAG		BIT(1)
#define BQ25960_IBUS_UCP_FALL_FLAG	BIT(5)
#define BQ25960_VAC2_OVP_FLAG		BIT(6)
#define BQ25960_WD_TIMEOUT_FLAG		BIT(0)
#define BQ25960_TDIE_ALM_FLAG		BIT(1)
#define BQ25960_VBUS_ERRHI_MASK		BIT(4)
#define BQ25960_DEVICE_ID		GENMASK(3, 0)
#define BQ25960_ADC_EN			BIT(7)

/* registers of the hidden page that run reverse charging */
#define BQ25960H_REG_A0			0xa0
#define BQ25960H_REG_A8			0xa8
#define BQ25960H_REG_F9			0xf9
#define BQ25960H_REG_FA			0xfa
#define BQ25960H_OTG_CTRL		0x12
#define BQ25960H_RCP_DELAY_MS		500
#define BQ25960H_OTG_RETRY		5

#define BQ25960_REVCHG_RETRY		10
#define BQ25960_VBAT_SAMPLES		5
#define BQ25960_CHIP_VENDOR		4
#define BQ25960_DUMP_BUF_LEN		256

enum bq25960_adc {
	BQ25960_ADC_IBUS,
	BQ25960_ADC_VBUS,
	BQ25960_ADC_VAC1,
	BQ25960_ADC_VAC2,
	BQ25960_ADC_VOUT,
	BQ25960_ADC_VBAT,
	BQ25960_ADC_IBAT,
	BQ25960_ADC_TSBUS,
	BQ25960_ADC_TSBAT,
	BQ25960_ADC_TDIE,
	BQ25960_ADC_MAX,
};

/* operation modes of the MCA, to the bypass bit */
static const u8 bq25960_mode_table[] = { 0, 0, 1, 0, 0, 0 };

static const u8 bq25960_reg_list[] = {
	0x05, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x19, 0x1a, 0x1b, 0x1c, 0x27, 0x28, 0x29, 0x2a, 0x2d, 0x2e, 0x2f,
	0x30, 0x37, 0x38,
};

struct bq25960_cfg {
	bool bat_ovp_disable;
	bool bat_ocp_disable;
	bool bat_ovp_alm_disable;
	bool bat_ocp_alm_disable;
	bool bat_ucp_alm_disable;
	bool bus_ovp_alm_disable;
	bool bus_ucp_disable;
	bool bus_ocp_alm_disable;
	bool bat_therm_disable;
	bool bus_therm_disable;
	u32 bat_ovp_th;
	u32 bat_ovp_alm_th;
	u32 bat_ocp_th;
	u32 bat_ocp_alm_th;
	u32 bat_ucp_alm_th;
	u32 bus_ovp_th;
	u32 bus_ovp_alm_th;
	u32 bus_ocp_th;
	u32 bus_ucp_th;
	u32 bus_ocp_alm_th;
	u32 usb_ovp_th;
	u32 vout_ovp_th;
	u32 die_therm_th;
};

struct bq25960_device {
	struct device *dev;
	struct i2c_client *client;
	/* the hidden register page, for reverse charging */
	struct i2c_client *hidden;
	struct mutex i2c_rw_lock;
	struct mutex otg_lock;
	struct delayed_work irq_work;
	struct delayed_work rcp_work;
	struct bq25960_cfg cfg;

	int cp_role;
	int irq_gpio;
	int irq;
	int part_no;
	bool present;
	bool probe_done;
	bool support_multiple_cp;
	/* reverse charging is on, the chip answers on the hidden page */
	bool otg_mode;
};

static struct i2c_client *bq25960_active_client(struct bq25960_device *bq)
{
	return bq->otg_mode && bq->hidden ? bq->hidden : bq->client;
}

static int bq25960_read_reg(struct bq25960_device *bq, u8 reg, u8 *val)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = i2c_smbus_read_byte_data(bq25960_active_client(bq), reg);
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0) {
		mca_log_err("failed to read reg 0x%02x: %d\n", reg, ret);
		return ret;
	}

	*val = ret;
	return 0;
}

static int bq25960_write_reg(struct bq25960_device *bq, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = i2c_smbus_write_byte_data(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0)
		mca_log_err("failed to write 0x%02x to reg 0x%02x: %d\n",
			    val, reg, ret);

	return ret < 0 ? ret : 0;
}

static int bq25960_update_bits(struct bq25960_device *bq, u8 reg, u8 mask,
			       u8 val)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = i2c_smbus_read_byte_data(bq->client, reg);
	if (ret >= 0)
		ret = i2c_smbus_write_byte_data(bq->client, reg,
						(ret & ~mask) | (val & mask));
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0)
		mca_log_err("failed to update reg 0x%02x: %d\n", reg, ret);

	return ret < 0 ? ret : 0;
}

static int bq25960_hidden_write(struct bq25960_device *bq, u8 reg, u8 val)
{
	int ret;

	if (!bq->hidden)
		return -ENODEV;

	mutex_lock(&bq->i2c_rw_lock);
	ret = i2c_smbus_write_byte_data(bq->hidden, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0)
		mca_log_err("failed to write 0x%02x to hidden reg 0x%02x: %d\n",
			    val, reg, ret);

	return ret < 0 ? ret : 0;
}

static int bq25960_hidden_update_bits(struct bq25960_device *bq, u8 reg,
				      u8 mask, u8 val)
{
	int ret;

	if (!bq->hidden)
		return -ENODEV;

	mutex_lock(&bq->i2c_rw_lock);
	ret = i2c_smbus_read_byte_data(bq->hidden, reg);
	if (ret >= 0)
		ret = i2c_smbus_write_byte_data(bq->hidden, reg,
						(ret & ~mask) | (val & mask));
	mutex_unlock(&bq->i2c_rw_lock);

	return ret < 0 ? ret : 0;
}

static int bq25960_get_adc_data(struct bq25960_device *bq, int channel,
				u32 *result)
{
	u8 hi = 0, lo = 0, ctrl = 0;
	u32 raw;
	int ret;

	if (channel >= BQ25960_ADC_MAX)
		return -EINVAL;

	ret = bq25960_read_reg(bq, BQ25960_REG_ADC_BASE + channel * 2, &hi);
	if (ret)
		return ret;
	ret = bq25960_read_reg(bq, BQ25960_REG_ADC_BASE + channel * 2 + 1, &lo);
	if (ret)
		return ret;
	raw = hi << 8 | lo;

	/* trimmed against the readings of the stock calibration */
	switch (channel) {
	case BQ25960_ADC_IBUS:
		bq25960_read_reg(bq, BQ25960_REG_CTRL2, &ctrl);
		if (ctrl & BQ25960_EN_BYPASS)
			raw = raw * 10279 / 10000 + 64;
		else
			raw = raw * 9972 / 10000 + 66;
		break;
	case BQ25960_ADC_VBUS:
		raw = raw * 1002 / 1000;
		break;
	case BQ25960_ADC_VAC1:
		raw = raw * 10008 / 10000 + 3;
		break;
	case BQ25960_ADC_VOUT:
		raw = raw * 10037 / 10000 + 5;
		break;
	case BQ25960_ADC_VBAT:
		raw = raw * 10017 / 10000;
		break;
	case BQ25960_ADC_IBAT:
		raw = raw * 999 / 1000;
		break;
	case BQ25960_ADC_TSBUS:
		raw = raw * 9860 / 100000;
		break;
	case BQ25960_ADC_TSBAT:
		raw = raw * 9762 / 100000;
		break;
	case BQ25960_ADC_TDIE:
		raw = raw * 5079 / 10000;
		break;
	}

	*result = raw & 0xffff;
	return 0;
}

static int bq25960_init_protection(struct bq25960_device *bq)
{
	const struct bq25960_cfg *c = &bq->cfg;
	u8 vac, vout;
	int ret = 0;

	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOVP, BQ25960_DIS,
				   c->bat_ovp_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOCP, BQ25960_DIS,
				   c->bat_ocp_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOVP_ALM, BQ25960_DIS,
				   c->bat_ovp_alm_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOCP_ALM, BQ25960_DIS,
				   c->bat_ocp_alm_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATUCP_ALM, BQ25960_DIS,
				   c->bat_ucp_alm_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSOVP_ALM, BQ25960_DIS,
				   c->bus_ovp_alm_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSUCP, BQ25960_DIS,
				   c->bus_ucp_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSOCP_ALM, BQ25960_DIS,
				   c->bus_ocp_alm_disable ? BQ25960_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_TEMP_CTRL, BQ25960_TSBAT_DIS,
				   c->bat_therm_disable ? BQ25960_TSBAT_DIS : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_TEMP_CTRL, BQ25960_TSBUS_DIS,
				   c->bus_therm_disable ? BQ25960_TSBUS_DIS : 0);

	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOVP, 0x7f,
			(max_t(u32, c->bat_ovp_th, 3491) * 1000 - 3491000) / 9985);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOVP_ALM, 0x7f,
			(max_t(u32, c->bat_ovp_alm_th, 3500) - 3500) / 10);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOCP, 0x7f,
				   c->bat_ocp_th * 10 / 1025);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATOCP_ALM, 0x7f,
				   c->bat_ocp_alm_th / 100);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSOVP, 0x7f,
			(max_t(u32, c->bus_ovp_th, 7000) - 7000) / 50);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSOVP_ALM, 0x7f,
			(max_t(u32, c->bus_ovp_alm_th, 7000) - 7000) / 50);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSOCP, 0x1f,
			c->bus_ocp_th * 10 > 10175 ?
			(c->bus_ocp_th * 10 - 10175) / 2540 : 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSOCP_ALM, 0x1f,
			(max_t(u32, c->bus_ocp_alm_th, 1000) - 1000) / 250);
	ret |= bq25960_update_bits(bq, BQ25960_REG_BATUCP_ALM, 0x7f,
				   c->bat_ucp_alm_th / 50);

	switch (c->usb_ovp_th) {
	case 10500:
		vac = 0x20;
		break;
	case 12000:
		vac = 0x40;
		break;
	case 14000:
		vac = 0x60;
		break;
	case 16000:
		vac = 0x80;
		break;
	case 18000:
		vac = 0xa0;
		break;
	default:
		vac = 0;
		break;
	}
	ret |= bq25960_update_bits(bq, BQ25960_REG_VAC_OVP,
				   BQ25960_VAC_OVP_MASK, vac);

	ret |= bq25960_update_bits(bq, BQ25960_REG_BUSUCP, BQ25960_BUSUCP_250MA,
				   c->bus_ucp_th == 250 ?
				   BQ25960_BUSUCP_250MA : 0);

	switch (c->vout_ovp_th) {
	case 4700:
		vout = 0x00;
		break;
	case 4800:
		vout = 0x20;
		break;
	case 4900:
		vout = 0x40;
		break;
	default:
		vout = 0x60;
		break;
	}
	ret |= bq25960_update_bits(bq, BQ25960_REG_CTRL5, BQ25960_VOUT_OVP,
				   vout);

	ret |= bq25960_update_bits(bq, BQ25960_REG_TDIE_ALM, 0xff,
				   (c->die_therm_th * 10 - 250) / 5);

	return ret;
}

static int bq25960_init_device(struct bq25960_device *bq)
{
	int ret;

	ret = bq25960_update_bits(bq, BQ25960_REG_CTRL2, BQ25960_REG_RST,
				  BQ25960_REG_RST);
	ret |= bq25960_update_bits(bq, BQ25960_REG_CTRL3, BQ25960_WATCHDOG_DIS,
				   BQ25960_WATCHDOG_DIS);
	ret |= bq25960_update_bits(bq, BQ25960_REG_CTRL4, BQ25960_SS_TIMEOUT,
				   0);
	ret |= bq25960_init_protection(bq);

	/* sample every channel, continuously */
	ret |= bq25960_update_bits(bq, BQ25960_REG_ADC_CTRL, 0x82, 0);
	ret |= bq25960_write_reg(bq, BQ25960_REG_ADC_FN_DIS, 0);
	ret |= bq25960_update_bits(bq, BQ25960_REG_ADC_CTRL, BQ25960_ADC_EN,
				   BQ25960_ADC_EN);

	if (ret)
		mca_log_err("failed to init the device\n");

	return ret;
}

static void bq25960_report_faults(struct bq25960_device *bq, const u8 *regs)
{
	u8 stat2 = regs[7], stat4 = regs[9], flag1 = regs[10];
	u8 flag2 = regs[11], flag3 = regs[12], flag4 = regs[13];
	u32 tdie = 0;

	if (!(stat2 & BQ25960_VOUT_PRESENT)) {
		mca_log_err("VOUT UVLO\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VOUT_UVLO, NULL);
	}
	if ((stat2 & BQ25960_VIN_PRESENT) != BQ25960_VIN_PRESENT)
		mca_log_err("VIN not present\n");
	if (stat4 & BQ25960_VBUS_ERRHI)
		mca_log_err("VBUS error high\n");
	if (flag1 & BQ25960_IBAT_OCP_FLAG) {
		mca_log_err("IBAT OCP\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_IBAT_OCP, NULL);
	}
	if (flag1 & BQ25960_VBUS_OVP_FLAG) {
		mca_log_err("VBUS OVP\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VBUS_OVP, NULL);
	}
	if (flag2 & BQ25960_IBUS_UCP_FALL_FLAG) {
		mca_log_err("IBUS UCP\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_IBUS_UCP, NULL);
	}
	if (flag3 & BQ25960_VAC2_OVP_FLAG) {
		mca_log_err("VAC2 OVP\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VWPC_OVP, NULL);
	}
	if (flag4 & BQ25960_WD_TIMEOUT_FLAG)
		mca_log_err("watchdog timeout\n");
	if (flag4 & BQ25960_TDIE_ALM_FLAG) {
		bq25960_get_adc_data(bq, BQ25960_ADC_TDIE, &tdie);
		mca_log_err("die temperature alarm, %u\n", tdie);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_TSHUT_FLAG, &tdie);
	}
}

static void bq25960_dump_regs(struct bq25960_device *bq)
{
	u8 vals[ARRAY_SIZE(bq25960_reg_list)] = { 0 };
	char buf[BQ25960_DUMP_BUF_LEN];
	int i, len = 0;

	buf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(bq25960_reg_list); i++) {
		bq25960_read_reg(bq, bq25960_reg_list[i], &vals[i]);
		len += scnprintf(buf + len, sizeof(buf) - len,
				 "[0x%02X]=0x%02X,", bq25960_reg_list[i],
				 vals[i]);
		if (i == ARRAY_SIZE(bq25960_reg_list) - 1 || ((i + 1) & 7) == 0) {
			mca_log_info("%s\n", buf);
			len = 0;
			buf[0] = '\0';
		}
	}

	/* the converter stopped: say why */
	if (!(vals[9] & BQ25960_CP_SWITCHING) ||
	    (vals[13] & BQ25960_TDIE_ALM_FLAG))
		bq25960_report_faults(bq, vals);
}

static void bq25960_irq_work(struct work_struct *work)
{
	struct bq25960_device *bq = container_of(to_delayed_work(work),
					struct bq25960_device, irq_work);

	bq25960_dump_regs(bq);
}

static irqreturn_t bq25960_isr(int irq, void *data)
{
	struct bq25960_device *bq = data;

	queue_delayed_work(system_wq, &bq->irq_work, 0);

	return IRQ_HANDLED;
}

static int ops_cp_enable_charge(bool en, void *data)
{
	struct bq25960_device *bq = data;
	int ret;

	/* VBUS error high is expected while the converter is off */
	bq25960_update_bits(bq, BQ25960_REG_MASK4, BQ25960_VBUS_ERRHI_MASK,
			    en ? 0 : BQ25960_VBUS_ERRHI_MASK);
	mca_log_err("charge %s\n", en ? "enable" : "disable");
	ret = bq25960_update_bits(bq, BQ25960_REG_CTRL2, BQ25960_CHG_EN,
				  en ? BQ25960_CHG_EN : 0);
	if (ret)
		mca_log_err("failed to enable charging\n");

	return ret;
}

static int ops_cp_get_charge_enable(bool *en, void *data)
{
	struct bq25960_device *bq = data;
	u8 ctrl = 0, stat = 0;
	int ret;

	ret = bq25960_read_reg(bq, BQ25960_REG_CTRL2, &ctrl) ?:
	      bq25960_read_reg(bq, BQ25960_REG_STAT4, &stat);
	if (ret)
		return ret;

	*en = (ctrl & BQ25960_CHG_EN) && (stat & BQ25960_CP_SWITCHING);
	return 0;
}

static int ops_cp_get_present(bool *present, void *data)
{
	struct bq25960_device *bq = data;

	*present = bq->present;
	return 0;
}

/* the median three of five samples, as the battery voltage swings under load */
static int ops_cp_get_vbatt(u32 *val, void *data)
{
	u32 s[BQ25960_VBAT_SAMPLES], sum = 0, lo = U32_MAX, hi = 0;
	int i, ret;

	for (i = 0; i < BQ25960_VBAT_SAMPLES; i++) {
		msleep(24);
		ret = bq25960_get_adc_data(data, BQ25960_ADC_VBAT, &s[i]);
		if (ret) {
			mca_log_err("failed to read vbat\n");
			return ret;
		}
		sum += s[i];
		lo = min(lo, s[i]);
		hi = max(hi, s[i]);
	}

	*val = DIV_ROUND_CLOSEST(sum - lo - hi, BQ25960_VBAT_SAMPLES - 2);
	return 0;
}

static int ops_cp_get_ibatt(u32 *val, void *data)
{
	return bq25960_get_adc_data(data, BQ25960_ADC_IBAT, val);
}

static int ops_cp_get_battery_temperature(u32 *val, void *data)
{
	return bq25960_get_adc_data(data, BQ25960_ADC_TSBAT, val);
}

static int ops_cp_get_vbus(u32 *val, void *data)
{
	return bq25960_get_adc_data(data, BQ25960_ADC_VBUS, val);
}

static int ops_cp_get_ibus(u32 *val, void *data)
{
	return bq25960_get_adc_data(data, BQ25960_ADC_IBUS, val);
}

static int ops_cp_get_vusb(u32 *val, void *data)
{
	return bq25960_get_adc_data(data, BQ25960_ADC_VAC1, val);
}

static int ops_cp_get_tdie(int *val, void *data)
{
	return bq25960_get_adc_data(data, BQ25960_ADC_TDIE, (u32 *)val);
}

static int ops_cp_enable_ovpgate(bool en, void *data)
{
	return bq25960_update_bits(data, BQ25960_REG_CTRL2,
				   BQ25960_DIS_ACDRV_BOTH,
				   en ? 0 : BQ25960_DIS_ACDRV_BOTH);
}

static int ops_cp_get_ovpgate_status(bool *en, void *data)
{
	u8 val = 0;
	int ret;

	ret = bq25960_read_reg(data, BQ25960_REG_CTRL2, &val);
	if (!ret)
		*en = !!(val & BQ25960_DIS_ACDRV_BOTH);

	return ret;
}

static int ops_cp_set_mode(int mode, void *data)
{
	if (mode < 0 || mode >= ARRAY_SIZE(bq25960_mode_table) ||
	    !(BIT(mode) & 0x27)) {
		mca_log_err("invalid operation mode %d\n", mode);
		return -EINVAL;
	}

	return bq25960_update_bits(data, BQ25960_REG_CTRL2, BQ25960_EN_BYPASS,
				   bq25960_mode_table[mode] ?
				   BQ25960_EN_BYPASS : 0);
}

static int ops_cp_get_mode(int *mode, void *data)
{
	u8 val = 0;
	int ret;

	ret = bq25960_read_reg(data, BQ25960_REG_CTRL2, &val);
	if (!ret)
		*mode = val & BQ25960_EN_BYPASS ? 2 : 1;

	return ret;
}

static int ops_cp_device_init(int val, void *data)
{
	bq25960_init_device(data);
	return 0;
}

static int ops_cp_enable_adc(bool en, void *data)
{
	return bq25960_update_bits(data, BQ25960_REG_ADC_CTRL, BQ25960_ADC_EN,
				   en ? BQ25960_ADC_EN : 0);
}

static int ops_cp_get_bypass_support(bool *support, void *data)
{
	*support = true;
	return 0;
}

static int ops_cp_dump_register(void *data)
{
	bq25960_dump_regs(data);
	return 0;
}

static int ops_cp_get_chip_vendor(int *vendor, void *data)
{
	*vendor = BQ25960_CHIP_VENDOR;
	return 0;
}

static int ops_cp_get_probe_status(void *data)
{
	struct bq25960_device *bq = data;

	return !bq->probe_done;
}

static int ops_cp_get_errorhl_stat(int *stat, void *data)
{
	u8 val = 0;
	int ret;

	ret = bq25960_read_reg(data, BQ25960_REG_STAT4, &val);
	if (ret)
		return ret;

	*stat = val & BQ25960_VBUS_ERRHI ? 2 : 0;
	return 0;
}

static int ops_cp_enable_busucp(bool en, void *data)
{
	return bq25960_update_bits(data, BQ25960_REG_BUSUCP, BQ25960_DIS,
				   en ? 0 : BQ25960_DIS);
}

/* drives VBUS from the battery through the converter and ACDRV1 */
static int ops_cp_set_revchg(bool en, void *data)
{
	struct bq25960_device *bq = data;
	u8 val = 0;
	int i, ret = 0;

	for (i = 0; i < BQ25960_REVCHG_RETRY; i++) {
		if (en) {
			ret = bq25960_update_bits(bq, BQ25960_REG_CTRL2,
						  BQ25960_EN_OTG, BQ25960_EN_OTG);
			ret |= bq25960_update_bits(bq, BQ25960_REG_CTRL2,
						   BQ25960_DIS_ACDRV_BOTH, 0);
			ret |= bq25960_update_bits(bq, BQ25960_REG_CTRL2,
						   BQ25960_ACDRV1_EN,
						   BQ25960_ACDRV1_EN);
		} else {
			ret = bq25960_update_bits(bq, BQ25960_REG_BUSOVP,
						  BQ25960_VBUS_PD, 0);
			ret |= bq25960_update_bits(bq, BQ25960_REG_CTRL2,
						   BQ25960_ACDRV1_EN, 0);
			ret |= bq25960_update_bits(bq, BQ25960_REG_CTRL2,
						   BQ25960_EN_OTG, 0);
		}
		ret |= bq25960_read_reg(bq, BQ25960_REG_CTRL2, &val);
		if (!ret) {
			mca_log_info("revchg %d, ctrl2 0x%02x\n", en, val);
			return 0;
		}
		mca_log_err("failed to set revchg, retry %d\n", i);
		msleep(20);
	}

	return ret;
}

static void bq25960_rcp_work(struct work_struct *work)
{
	struct bq25960_device *bq = container_of(to_delayed_work(work),
					struct bq25960_device, rcp_work);

	if (bq->otg_mode)
		bq25960_hidden_write(bq, BQ25960_REG_BUSUCP, 0x9e);
}

static int bq25960_hidden_enable_otg(struct bq25960_device *bq, bool en)
{
	u8 val = 0;
	int i, ret;

	if (!en) {
		cancel_delayed_work_sync(&bq->rcp_work);
		ret = bq25960_hidden_write(bq, BQ25960_REG_CTRL2, 0);
		ret |= bq25960_hidden_write(bq, BQ25960_REG_BUSUCP, 0x0e);
		ret |= bq25960_hidden_write(bq, BQ25960H_REG_FA, 0x20);
		ret |= bq25960_hidden_write(bq, BQ25960H_REG_F9, 0);
		msleep(10);
		ret |= bq25960_hidden_write(bq, BQ25960H_REG_A0, 0);
		mca_log_err("reverse charging off, %d\n", ret);
		return ret;
	}

	msleep(10);
	ret = bq25960_hidden_write(bq, BQ25960H_REG_A8, 0);
	ret |= bq25960_hidden_update_bits(bq, BQ25960_REG_BUSUCP, 0x80, 0x80);
	ret |= bq25960_hidden_update_bits(bq, BQ25960_REG_BUSUCP, 0x20, 0x20);
	ret |= bq25960_hidden_write(bq, BQ25960_REG_CTRL2, BQ25960H_OTG_CTRL);
	msleep(10);

	for (i = 0; i < BQ25960H_OTG_RETRY; i++) {
		bq25960_hidden_write(bq, BQ25960_REG_CTRL2, BQ25960H_OTG_CTRL);
		mutex_lock(&bq->i2c_rw_lock);
		val = i2c_smbus_read_byte_data(bq->hidden, BQ25960_REG_CTRL2);
		mutex_unlock(&bq->i2c_rw_lock);
		if (val == BQ25960H_OTG_CTRL)
			break;
		msleep(10);
	}

	ret |= bq25960_hidden_write(bq, BQ25960H_REG_FA, 0x21);
	ret |= bq25960_hidden_write(bq, BQ25960_REG_BUSUCP, 0xbe);
	schedule_delayed_work(&bq->rcp_work,
			      msecs_to_jiffies(BQ25960H_RCP_DELAY_MS));

	mca_log_err("reverse charging on, ctrl2 0x%02x, %d\n", val, ret);
	return ret;
}

/* 9V reverse charging, run from the hidden register page */
static int ops_cp_set_reverse_mode(bool en, void *data)
{
	struct bq25960_device *bq = data;
	int ret;

	if (!bq->hidden) {
		mca_log_err("no hidden page, no reverse charging\n");
		return -ENODEV;
	}

	mutex_lock(&bq->otg_lock);
	if (!en) {
		ret = bq25960_hidden_enable_otg(bq, false);
		bq->otg_mode = false;
		goto out;
	}

	bq25960_update_bits(bq, BQ25960_REG_CTRL3, BQ25960_WATCHDOG_DIS,
			    BQ25960_WATCHDOG_DIS);
	bq25960_update_bits(bq, BQ25960_REG_VAC_OVP, BQ25960_VAC_OVP_MASK, 0x60);
	bq25960_update_bits(bq, BQ25960_REG_BUSOVP, 0x7f, 0x6c);
	bq25960_update_bits(bq, BQ25960_REG_TEMP_CTRL, BQ25960_TSBUS_DIS,
			    BQ25960_TSBUS_DIS);
	bq25960_update_bits(bq, BQ25960_REG_TEMP_CTRL, BQ25960_TSBAT_DIS,
			    BQ25960_TSBAT_DIS);
	bq25960_write_reg(bq, 0x9a, 0x34);
	bq25960_write_reg(bq, 0x9b, 0x5b);
	bq25960_write_reg(bq, BQ25960H_REG_A0, 0x80);

	bq25960_hidden_write(bq, BQ25960H_REG_F9, 0x40);
	bq25960_hidden_write(bq, BQ25960H_REG_FA, 0x31);
	bq25960_hidden_write(bq, BQ25960H_REG_A8, 0xd7);

	bq->otg_mode = true;
	ret = bq25960_hidden_enable_otg(bq, true);
out:
	mutex_unlock(&bq->otg_lock);
	return ret;
}

static struct platform_class_cp_ops bq25960_cp_ops = {
	.cp_set_enable = ops_cp_enable_charge,
	.cp_get_enabled = ops_cp_get_charge_enable,
	.cp_get_present = ops_cp_get_present,
	.cp_get_battery_voltage = ops_cp_get_vbatt,
	.cp_get_battery_current = ops_cp_get_ibatt,
	.cp_get_battery_temperature = ops_cp_get_battery_temperature,
	.cp_get_bus_voltage = ops_cp_get_vbus,
	.cp_get_bus_current = ops_cp_get_ibus,
	.cp_get_usb_voltage = ops_cp_get_vusb,
	.cp_get_tdie = ops_cp_get_tdie,
	.cp_enable_ovpgate = ops_cp_enable_ovpgate,
	.cp_get_ovpgate_status = ops_cp_get_ovpgate_status,
	.cp_set_mode = ops_cp_set_mode,
	.cp_get_mode = ops_cp_get_mode,
	.cp_device_init = ops_cp_device_init,
	.cp_enable_adc = ops_cp_enable_adc,
	.cp_get_bypass_support = ops_cp_get_bypass_support,
	.cp_dump_register = ops_cp_dump_register,
	.cp_get_chip_vendor = ops_cp_get_chip_vendor,
	.cp_get_probe_ok = ops_cp_get_probe_status,
	.cp_get_errorhl_stat = ops_cp_get_errorhl_stat,
	.cp_enable_busucp = ops_cp_enable_busucp,
	.cp_set_revchg = ops_cp_set_revchg,
	.cp_set_cp_reverse_mode = ops_cp_set_reverse_mode,
};

static int bq25960_parse_dt(struct bq25960_device *bq)
{
	struct device_node *np = bq->dev->of_node;
	struct bq25960_cfg *c = &bq->cfg;
	const struct {
		const char *name;
		u32 *val;
	} props[] = {
		{ "ti,bq25960,bat-ovp-threshold", &c->bat_ovp_th },
		{ "ti,bq25960,bat-ovp-alarm-threshold", &c->bat_ovp_alm_th },
		{ "ti,bq25960,bus-ovp-threshold", &c->bus_ovp_th },
		{ "ti,bq25960,bus-ovp-alarm-threshold", &c->bus_ovp_alm_th },
		{ "ti,bq25960,bus-ocp-threshold", &c->bus_ocp_th },
		{ "ti,bq25960,bus-ucp-threshold", &c->bus_ucp_th },
		{ "ti,bq25960,vout-ovp-threshold", &c->vout_ovp_th },
		{ "ti,bq25960,usb-ovp-threshold", &c->usb_ovp_th },
		{ "ti,bq25960,bus-ocp-alarm-threshold", &c->bus_ocp_alm_th },
		{ "ic_role", &bq->cp_role },
		{ "ti,bq25960,die-therm-threshold", &c->die_therm_th },
	};
	int i, ret;

	c->bat_ovp_disable = of_property_read_bool(np, "ti,bq25960,bat-ovp-disable");
	c->bat_ocp_disable = of_property_read_bool(np, "ti,bq25960,bat-ocp-disable");
	c->bat_ovp_alm_disable =
		of_property_read_bool(np, "ti,bq25960,bat-ovp-alarm-disable");
	c->bat_ocp_alm_disable =
		of_property_read_bool(np, "ti,bq25960,bat-ocp-alarm-disable");
	c->bus_ucp_disable = of_property_read_bool(np, "ti,bq25960,bus-ucp-disable");
	c->bus_ovp_alm_disable =
		of_property_read_bool(np, "ti,bq25960,bus-ovp-alarm-disable");
	c->bus_ocp_alm_disable =
		of_property_read_bool(np, "ti,bq25960,bus-ocp-alarm-disable");
	c->bat_ucp_alm_disable =
		of_property_read_bool(np, "ti,bq25960,bat-ucp-alarm-disable");
	c->bat_therm_disable =
		of_property_read_bool(np, "ti,bq25960,bat-therm-disable");
	c->bus_therm_disable =
		of_property_read_bool(np, "ti,bq25960,bus-therm-disable");
	bq->support_multiple_cp = of_property_read_bool(np, "support-multiple-cp");

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_u32(np, props[i].name, props[i].val);
		if (ret) {
			mca_log_err("failed to read %s\n", props[i].name);
			return ret;
		}
	}

	bq->irq_gpio = of_get_named_gpio(np, "cp-int", 0);
	if (bq->irq_gpio < 0) {
		mca_log_err("failed to get the interrupt gpio\n");
		return bq->irq_gpio;
	}

	return 0;
}

static void bq25960_find_hidden(struct bq25960_device *bq)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "ti,bq25960h-master");
	if (!np)
		return;

	bq->hidden = of_find_i2c_device_by_node(np);
	of_node_put(np);
	if (bq->hidden)
		mca_log_info("hidden page at 0x%02x\n", bq->hidden->addr);
}

static int bq25960_probe(struct i2c_client *client)
{
	struct bq25960_device *bq;
	int data = 0;
	int ret;

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;
	i2c_set_clientdata(client, bq);
	mutex_init(&bq->i2c_rw_lock);
	mutex_init(&bq->otg_lock);
	INIT_DELAYED_WORK(&bq->irq_work, bq25960_irq_work);
	INIT_DELAYED_WORK(&bq->rcp_work, bq25960_rcp_work);

	ret = bq25960_parse_dt(bq);
	if (ret)
		return ret;

	ret = i2c_smbus_read_byte_data(client, BQ25960_REG_PART_INFO);
	if (ret < 0) {
		mca_log_err("no bq25960 found\n");
		if (!bq->support_multiple_cp)
			mca_charge_mievent_report(0x2c, &data, 2);
		return -ENODEV;
	}
	bq->part_no = ret & BQ25960_DEVICE_ID;

	bq25960_find_hidden(bq);
	bq25960_init_device(bq);

	ret = devm_gpio_request(bq->dev, bq->irq_gpio, dev_name(bq->dev));
	if (ret)
		goto err_hidden;

	bq->irq = gpio_to_irq(bq->irq_gpio);
	if (bq->irq < 0) {
		ret = bq->irq;
		goto err_hidden;
	}

	ret = devm_request_threaded_irq(bq->dev, bq->irq, NULL, bq25960_isr,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(bq->dev), bq);
	if (ret)
		goto err_hidden;

	enable_irq_wake(bq->irq);
	device_init_wakeup(bq->dev, true);

	bq->present = true;
	platform_class_cp_register_ops(bq->cp_role, &bq25960_cp_ops, bq);
	bq->probe_done = true;

	mca_log_err("bq25960 probed, part %d\n", bq->part_no);
	return 0;

err_hidden:
	if (bq->hidden)
		put_device(&bq->hidden->dev);
	return ret;
}

static void bq25960_remove(struct i2c_client *client)
{
	struct bq25960_device *bq = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&bq->irq_work);
	cancel_delayed_work_sync(&bq->rcp_work);
	bq25960_update_bits(bq, BQ25960_REG_ADC_CTRL, BQ25960_ADC_EN, 0);
	if (bq->hidden)
		put_device(&bq->hidden->dev);
}

static void bq25960_shutdown(struct i2c_client *client)
{
	struct bq25960_device *bq = i2c_get_clientdata(client);

	bq25960_update_bits(bq, BQ25960_REG_ADC_CTRL, BQ25960_ADC_EN, 0);
}

static int bq25960_suspend(struct device *dev)
{
	struct bq25960_device *bq = dev_get_drvdata(dev);

	/* the ADC draws power for nothing while nobody reads it */
	return bq25960_update_bits(bq, BQ25960_REG_ADC_CTRL, BQ25960_ADC_EN, 0);
}

static DEFINE_SIMPLE_DEV_PM_OPS(bq25960_pm_ops, bq25960_suspend, NULL);

static const struct of_device_id bq25960_of_match[] = {
	{ .compatible = "ti,bq25960-master" },
	{ }
};
MODULE_DEVICE_TABLE(of, bq25960_of_match);

static const struct i2c_device_id bq25960_i2c_id[] = {
	{ "bq25960", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bq25960_i2c_id);

static struct i2c_driver bq25960_driver = {
	.driver = {
		.name = "bq25960",
		.of_match_table = bq25960_of_match,
		.pm = pm_sleep_ptr(&bq25960_pm_ops),
	},
	.probe = bq25960_probe,
	.remove = bq25960_remove,
	.shutdown = bq25960_shutdown,
	.id_table = bq25960_i2c_id,
};
module_i2c_driver(bq25960_driver);

MODULE_DESCRIPTION("MCA TI BQ25960 charge pump driver");
MODULE_LICENSE("GPL");
