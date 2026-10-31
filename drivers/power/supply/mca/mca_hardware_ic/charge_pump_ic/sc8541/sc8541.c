#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_charge_mievent.h>
#include "inc/sc8541_reg.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "cp_sc8541"
#endif

#define SC8541_MAX_REG 0x42
#define MAX_LENGTH_BYTE 600
#define SC8541_DUMP_BUF_LEN 0x100
#define SC8541_SET_OVPGATE_COUNT 10
#define SC8541_INIT_RETRY 5

enum sc8541_role {
	SC8541_ROLE_MASTER = 0,
	SC8541_ROLE_SLAVE = 1,
};

struct sc8541_device {
	struct device *dev;
	struct i2c_client *client;
	struct mutex i2c_rw_lock;
	struct mutex data_lock;
	struct mutex irq_complete;
	struct delayed_work irq_work;

	int cp_role;
	char log_tag[8];
	int irq_gpio;
	int irq;
	int device_id;
	int chip_vendor;

	bool present;
	bool support_wls;
	bool i2c_disabled;
	bool resume_pending;
	int charging_active;
	int acdrv_init_done;
	int probe_done;
	int ovpgate_enable;
	int adc_scaled;

	int bat_ovp;
	int bat_ovp_alarm;
	int bus_ovp[2];
	int bus_ocp[2];
	int usb_ovp[2];
	int work_mode;
	bool support_multiple_cp;
	int bus_ucp_fall_deglitch;
	struct wakeup_source *quick_revchg_ws;
};

static const u32 sc8541_mode_table[] = { 0, 0, 1 };

static const u8 sc8541_reg_list[] = {
	SC8541_REG_05, SC8541_REG_0F, SC8541_REG_10, SC8541_REG_11,
	SC8541_REG_12, SC8541_REG_13, SC8541_REG_14, SC8541_REG_15,
	SC8541_REG_16, SC8541_REG_17, SC8541_REG_18, SC8541_REG_19,
	SC8541_REG_1A, SC8541_REG_1B, SC8541_REG_1C, SC8541_REG_27,
	SC8541_REG_28, SC8541_REG_2D, SC8541_REG_2E, SC8541_REG_40,
};

static int __sc8541_read_byte(struct sc8541_device *sc, u8 reg, u8 *val)
{
	s32 ret = i2c_smbus_read_byte_data(sc->client, reg);

	if (ret < 0) {
		mca_log_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static int __sc8541_write_byte(struct sc8541_device *sc, u8 reg, u8 val)
{
	s32 ret = i2c_smbus_write_byte_data(sc->client, reg, val);

	if (ret < 0) {
		mca_log_err(
			"i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}

static int sc8541_update_bits(struct sc8541_device *sc, u8 reg, u8 mask, u8 val)
{
	u8 tmp;
	int ret;

	if (sc->i2c_disabled)
		return 0;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8541_read_byte(sc, reg, &tmp);
	if (ret) {
		mca_log_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}
	tmp = (tmp & ~mask) | (val & mask);
	ret = __sc8541_write_byte(sc, reg, tmp);
	if (ret)
		mca_log_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}

static int sc8541_read_reg_locked(struct sc8541_device *sc, u8 reg, u8 *val)
{
	int ret;

	if (sc->i2c_disabled) {
		*val = 0;
		return 0;
	}

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8541_read_byte(sc, reg, val);
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}

static int sc8541_get_adc_data(struct sc8541_device *sc, int channel,
			       u32 *result)
{
	u8 hi = 0, lo = 0;
	u32 raw, val;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_ADC_BASE + channel * 2,
				     &hi);
	if (ret)
		hi = 0;
	ret = sc8541_read_reg_locked(sc, SC8541_REG_ADC_BASE + channel * 2 + 1,
				     &lo);
	if (ret)
		return ret;

	raw = ((u32)hi << 8) | lo;
	val = raw;

	if (!sc->adc_scaled) {
		*result = val & 0xffff;
		return 0;
	}

	switch (channel) {
	case SC8541_ADC_IBUS:
		val = raw * 25 / 10;
		break;
	case SC8541_ADC_VBUS:
		val = raw * 375 / 100;
		break;
	case SC8541_ADC_VUSB:
	case SC8541_ADC_VWPC:
		val = raw * 5;
		break;
	case SC8541_ADC_VOUT:
	case SC8541_ADC_VBAT:
		val = raw * 125 / 100;
		break;
	case SC8541_ADC_IBAT:
		val = raw * 3125 / 1000;
		break;
	case SC8541_ADC_VAC:
		val = raw * 9766 / 100000;
		break;
	case SC8541_ADC_TBAT:
		val = raw * 9766 / 1000;
		break;
	default:
		val = (raw >> 1) & 0x7fff;
		break;
	}

	*result = val & 0xffff;
	return 0;
}

static int sc8541_enable_ovpgate(struct sc8541_device *sc, bool enable)
{
	u8 val;
	int ret, i;

	sc->ovpgate_enable = enable;
	if (!sc->acdrv_init_done)
		msleep(30);

	val = enable ? SC8541_OVPGATE_EN_MASK : 0;
	ret = sc8541_update_bits(sc, SC8541_REG_40, SC8541_OVPGATE_EN_MASK,
				 val);
	mca_log_info("%s enable %d, ret %d\n", sc->log_tag, enable, ret);

	for (i = 0; i < SC8541_SET_OVPGATE_COUNT; i++) {
		u8 reg = 0;
		bool gate;

		if (!sc8541_read_reg_locked(sc, SC8541_REG_40, &reg)) {
			gate = !!(reg & SC8541_OVPGATE_EN_MASK);
			if (gate == sc->ovpgate_enable) {
				mca_log_info(
					"ovpgate enable success, value %d\n",
					reg);
				return 0;
			}
		}
		mca_log_info("%s count %d, ovpgate_en %d\n", sc->log_tag, i,
			     sc->ovpgate_enable);
		ret = sc8541_update_bits(sc, SC8541_REG_40,
					 SC8541_OVPGATE_EN_MASK, val);
		msleep(10);
	}
	return ret;
}

static int sc8541_set_operation_mode(struct sc8541_device *sc,
				     unsigned int mode)
{
	u32 wm;
	int ibus_ocp, vbus_ovp, vac_ovp, vac_val;
	int ret = 0;

	if (mode >= ARRAY_SIZE(sc8541_mode_table)) {
		mca_log_info("%s operation mode error %d\n", sc->log_tag, mode);
		return -1;
	}

	wm = sc8541_mode_table[mode];
	sc->work_mode = wm;

	ret |= sc8541_update_bits(sc, SC8541_REG_00, 0x80, 0);
	ret |= sc8541_update_bits(sc, SC8541_REG_12, 0x80, 0x80);
	ret |= sc8541_update_bits(sc, SC8541_REG_02, 0x80, 0x80);
	ret |= sc8541_update_bits(sc, SC8541_REG_08, 0x80, 0);
	ret |= sc8541_update_bits(sc, SC8541_REG_05, 0x80, 0);
	ret |= sc8541_update_bits(sc, SC8541_REG_01, 0x80, 0);
	ret |= sc8541_update_bits(sc, SC8541_REG_00, 0x7f,
				  (max(sc->bat_ovp, 0xf00) - 0xf00) / 10);
	ret |= sc8541_update_bits(sc, SC8541_REG_01, 0x7f,
				  (max(sc->bat_ovp_alarm, 0xf00) - 0xf00) / 10);
	ret |= sc8541_update_bits(sc, SC8541_REG_02, 0x7f, 0x50);

	ibus_ocp = max(sc->bus_ocp[wm], 1000);
	ret |= sc8541_update_bits(sc, SC8541_REG_08, 0x1f,
				  (ibus_ocp - 1000) / 250);

	vbus_ovp = max(sc->bus_ovp[wm], 7000);
	ret |= sc8541_update_bits(sc, SC8541_REG_06, 0xff,
				  (vbus_ovp - 7000) / 50);

	vac_ovp = sc->usb_ovp[wm];
	if (vac_ovp == 10500)
		vac_val = 0x20;
	else if (vac_ovp == 12000)
		vac_val = 0x40;
	else if (vac_ovp == 14000)
		vac_val = 0x60;
	else if (vac_ovp == 16000)
		vac_val = 0x80;
	else if (vac_ovp == 18000)
		vac_val = 0xa0;
	else
		vac_val = 0;
	ret |= sc8541_update_bits(sc, SC8541_REG_0E, 0xe0, vac_val);

	mca_log_info("%s set operation mode %d reg %d work_mode %d\n",
		     sc->log_tag, mode, wm, sc->work_mode);
	ret |= sc8541_update_bits(sc, SC8541_REG_0F, SC8541_MODE_MASK,
				  wm << SC8541_MODE_SHIFT);

	return ret;
}

static int sc8541_init_device(struct sc8541_device *sc)
{
	int ret = 0;
	int retry = SC8541_INIT_RETRY;

	sc8541_update_bits(sc, SC8541_REG_0F, 0x80, 0);

	do {
		ret = sc8541_update_bits(sc, SC8541_REG_10, 0x04,
					 0x04); /* enable wdt */

		sc8541_update_bits(sc, SC8541_REG_23, 0x40, 0);
		sc8541_update_bits(sc, SC8541_REG_23, 0x02, 0);
		sc8541_update_bits(sc, SC8541_REG_23, 0x01, 0);
		sc8541_update_bits(sc, SC8541_REG_24, 0x40, 0);
		sc8541_update_bits(sc, SC8541_REG_24, 0x20, 0);
		sc8541_update_bits(sc, SC8541_REG_24, 0x10, 0);
		sc8541_update_bits(sc, SC8541_REG_24, 0x08, 0);
		sc8541_update_bits(sc, SC8541_REG_24, 0x04, 0);
		sc8541_update_bits(sc, SC8541_REG_24, 0x02, 0);
		sc8541_update_bits(sc, SC8541_REG_24, 0x01, 0);

		if (!sc->support_wls || sc->cp_role == SC8541_ROLE_SLAVE) {
			ret |= sc8541_update_bits(sc, SC8541_REG_40,
						  SC8541_ACDRV_MANUAL_MASK,
						  SC8541_ACDRV_MANUAL_MASK);
			ret |= sc8541_enable_ovpgate(sc, true);
		} else {
			u32 vusb = 0, vbus = 0;
			int r;

			r = sc8541_update_bits(sc, SC8541_REG_23,
					       SC8541_ADC_EN_MASK,
					       SC8541_ADC_EN_MASK);
			r |= sc8541_get_adc_data(sc, SC8541_ADC_VUSB, &vusb);
			r |= sc8541_get_adc_data(sc, SC8541_ADC_VBUS, &vbus);
			if (r || vusb < 0xfa1) {
				ret |= sc8541_update_bits(
					sc, SC8541_REG_40,
					SC8541_ACDRV_MANUAL_MASK,
					SC8541_ACDRV_MANUAL_MASK);
				ret |= sc8541_enable_ovpgate(sc, true);
			} else {
				int wr =
					platform_class_wireless_set_enable_mode(
						0, false);

				mca_log_err(
					"%s not set manual vac1 %d, vbus %d, ret %d\n",
					sc->log_tag, vusb, vbus, wr);
				if (wr) {
					ret = -EIO;
					break;
				}
				sc->acdrv_init_done = 1;
			}
		}

		ret |= sc8541_update_bits(sc, SC8541_REG_03, 0x80, 0x80);
		ret |= sc8541_update_bits(sc, SC8541_REG_03, 0x7f, 0x44);
		ret |= sc8541_update_bits(sc, SC8541_REG_06, 0x80, 0);
		ret |= sc8541_update_bits(sc, SC8541_REG_07, 0x80, 0x80);
		ret |= sc8541_update_bits(sc, SC8541_REG_07, 0x7f, 0x22);
		ret |= sc8541_update_bits(sc, SC8541_REG_0A, 0x08, 0x08);
		ret |= sc8541_update_bits(sc, SC8541_REG_0A, 0x04, 0x04);
		ret |= sc8541_update_bits(sc, SC8541_REG_11, 0x0c, 0x0c);
		ret |= sc8541_update_bits(sc, SC8541_REG_11, 0x70, 0x70);
		ret |= sc8541_update_bits(sc, SC8541_REG_20, 0x20, 0x20);
		ret |= sc8541_update_bits(sc, SC8541_REG_0D, 0xff, 0);
		ret |= sc8541_update_bits(sc, SC8541_REG_0C, 0xff, 0);
		ret |= sc8541_set_operation_mode(sc, 1);
		ret |= sc8541_update_bits(sc, SC8541_REG_23, SC8541_ADC_EN_MASK,
					  0);

		if (ret >= 0) {
			mca_log_err("%s success to init CP device\n",
				    sc->log_tag);
			break;
		}
	} while (--retry);

	return ret;
}

static void sc8541_dump_important_regs(struct sc8541_device *sc)
{
	char buf[SC8541_DUMP_BUF_LEN];
	u8 vals[ARRAY_SIZE(sc8541_reg_list)] = { 0 };
	int i, len = 0;

	buf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(sc8541_reg_list); i++) {
		if (!sc->i2c_disabled)
			sc8541_read_reg_locked(sc, sc8541_reg_list[i],
					       &vals[i]);

		len += scnprintf(buf + len, sizeof(buf) - len,
				 "[0x%02X]=0x%02X,", sc8541_reg_list[i],
				 vals[i]);
		if (i == ARRAY_SIZE(sc8541_reg_list) - 1 ||
		    ((i + 1) & 7) == 0) {
			mca_log_info("%s %s\n", sc->log_tag, buf);
			len = 0;
			buf[0] = '\0';
		}
	}
}

static void sc8541_irq_handler(struct work_struct *work)
{
	struct sc8541_device *sc = container_of(to_delayed_work(work),
						struct sc8541_device, irq_work);
	u8 stat;

	if (!sc->i2c_disabled &&
	    !sc8541_read_reg_locked(sc, SC8541_REG_15, &stat)) {
		if (stat & (1 << SC8541_VBUS_ERRORHI_BIT)) {
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
					       MCA_EVENT_CP_VBUS_OVP, NULL);
			if (sc->ovpgate_enable) {
				sc8541_update_bits(sc, SC8541_REG_40,
						   SC8541_ACDRV_MANUAL_MASK,
						   SC8541_ACDRV_MANUAL_MASK);
				sc->ovpgate_enable = 0;
				sc8541_enable_ovpgate(sc, true);
				mca_log_err("switch ovpgate to manual\n");
			}
		}
	}

	mca_log_err("%s handler\n", sc->log_tag);
	sc8541_dump_important_regs(sc);
}

static irqreturn_t sc8541_int_isr(int irq, void *dev_id)
{
	struct sc8541_device *sc = dev_id;

	mca_log_info("%s int_isr\n", sc->log_tag);
	queue_delayed_work(system_wq, &sc->irq_work, 0);
	return IRQ_HANDLED;
}

static int ops_cp_enable_charge(bool en, void *data)
{
	struct sc8541_device *sc = data;
	int ret;

	mca_log_err("sc8541 charger %s\n", en ? "enable" : "disable");
	ret = sc8541_update_bits(sc, SC8541_REG_0F, SC8541_CHG_EN_MASK,
				 en ? SC8541_CHG_EN_MASK : 0);
	if (ret)
		mca_log_err("%s failed enable cp charge\n", sc->log_tag);
	return ret;
}

static int ops_cp_get_charge_enable(bool *en, void *data)
{
	struct sc8541_device *sc = data;
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_0F, &val);
	mca_log_info(">>>reg [0x0F] = 0x%02x\n", val);
	if (ret) {
		mca_log_err("%s failed get enable cp charge status ret =%d\n",
			    sc->log_tag, ret);
		return ret;
	}
	*en = !!(val & SC8541_CHG_EN_MASK);
	return 0;
}

static int ops_cp_get_present(bool *present, void *data)
{
	struct sc8541_device *sc = data;

	*present = sc->present;
	return 0;
}

static int ops_cp_get_vbatt(u32 *val, void *data)
{
	return sc8541_get_adc_data(data, SC8541_ADC_VBAT, val);
}

static int ops_cp_get_ibatt(u32 *val, void *data)
{
	return sc8541_get_adc_data(data, SC8541_ADC_IBAT, val);
}

static int ops_cp_get_battery_temmperature(u32 *val, void *data)
{
	return sc8541_get_adc_data(data, SC8541_ADC_TBAT, val);
}

static int ops_cp_get_vbus(u32 *val, void *data)
{
	return sc8541_get_adc_data(data, SC8541_ADC_VBUS, val);
}

static int ops_cp_get_ibus(u32 *val, void *data)
{
	return sc8541_get_adc_data(data, SC8541_ADC_IBUS, val);
}

static int ops_cp_get_vusb(u32 *val, void *data)
{
	return sc8541_get_adc_data(data, SC8541_ADC_VUSB, val);
}

static int ops_cp_get_tdie(int *val, void *data)
{
	return sc8541_get_adc_data(data, SC8541_ADC_TDIE, (u32 *)val);
}

static int ops_cp_enable_ovpgate(bool en, void *data)
{
	struct sc8541_device *sc = data;
	int ret;

	sc8541_update_bits(sc, SC8541_REG_40, SC8541_ACDRV_MANUAL_MASK,
			   SC8541_ACDRV_MANUAL_MASK);
	ret = sc8541_enable_ovpgate(sc, en);
	if (ret)
		mca_log_info("%s failed enable cp acdrv manual\n", sc->log_tag);
	return ret;
}

static u32 sc8541_ovp_type;
static bool sc8541_ovp_enable_flag;

static int ops_cp_enable_ovpgate_with_check(int type, bool en, void *data)
{
	struct sc8541_device *sc = data;
	u32 bit = 1u << (type & 0x1f);
	int ret;

	if (en) {
		sc8541_ovp_type &= ~bit;
		if ((sc8541_ovp_type != 0) == (!sc8541_ovp_enable_flag))
			return 0;
	} else {
		sc8541_ovp_type |= bit;
		if (sc8541_ovp_enable_flag)
			return 0;
	}

	sc8541_ovp_enable_flag = en;
	ret = sc8541_update_bits(sc, SC8541_REG_40, SC8541_ACDRV_MANUAL_MASK,
				 SC8541_ACDRV_MANUAL_MASK);
	ret |= sc8541_enable_ovpgate(sc, en);
	if (ret)
		mca_log_info("%s failed enable cp ovpgate with check\n",
			     sc->log_tag);
	return ret;
}

static int ops_cp_get_ovpgate_status(bool *en, void *data)
{
	struct sc8541_device *sc = data;
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_40, &val);
	mca_log_info("%s ovpgate_status SC8541_REG_40=0x%x\n", sc->log_tag,
		     val);
	if (ret) {
		mca_log_info("%s get ovpgate status fail\n", sc->log_tag);
		return ret;
	}
	*en = !!(val & SC8541_OVPGATE_EN_MASK);
	return 0;
}

static int ops_cp_set_mode(int mode, void *data)
{
	struct sc8541_device *sc = data;
	int ret = sc8541_set_operation_mode(sc, mode);

	if (ret)
		mca_log_err("%s failed set cp charge mode\n", sc->log_tag);
	return ret;
}

static int ops_cp_get_mode(int *mode, void *data)
{
	struct sc8541_device *sc = data;
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_0F, &val);
	if (ret) {
		*mode = 0;
		mca_log_err("%s failed to get div_mode\n", sc->log_tag);
		return ret;
	}
	*mode = (val >> SC8541_MODE_SHIFT) & 1;
	return 0;
}

static int ops_cp_device_init(int val, void *data)
{
	struct sc8541_device *sc = data;
	int ret = sc8541_init_device(sc);

	if (ret)
		mca_log_err("%s failed init cp init device\n", sc->log_tag);
	return ret;
}

static int ops_cp_enable_adc(bool en, void *data)
{
	return sc8541_update_bits(data, SC8541_REG_23, SC8541_ADC_EN_MASK,
				  en ? SC8541_ADC_EN_MASK : 0);
}

static int ops_cp_get_bypass_support(bool *support, void *data)
{
	*support = true;
	mca_log_info("ops_cp_get_bypass_support %d\n", 1);
	return 0;
}

static int ops_cp_dump_register(void *data)
{
	sc8541_dump_important_regs(data);
	return 0;
}

static int ops_cp_get_chip_vendor(int *vendor, void *data)
{
	struct sc8541_device *sc = data;

	*vendor = sc->chip_vendor;
	return 0;
}

static int ops_cp_get_probe_status(void *data)
{
	struct sc8541_device *sc = data;

	return sc->probe_done == 0;
}

static int ops_enable_acdrv_manual(bool en, void *data)
{
	struct sc8541_device *sc = data;
	int ret;

	ret = sc8541_update_bits(sc, SC8541_REG_40, SC8541_ACDRV_MANUAL_MASK,
				 en ? SC8541_ACDRV_MANUAL_MASK : 0);
	if (ret)
		mca_log_info("%s failed enable cp acdrv manual\n", sc->log_tag);
	return ret;
}

static int ops_cp_get_int_stat(int type, bool *result, void *data)
{
	struct sc8541_device *sc = data;
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_15, &val);
	switch (type) {
	case 5:
		*result = (val >> 4) & 1;
		break;
	case 4:
		*result = (val >> 3) & 1;
		break;
	case 3:
		*result = (val >> 2) & 1;
		break;
	default:
		*result = 0;
		break;
	}
	if (ret)
		mca_log_info("failed get int stat %d\n", type);
	return ret;
}

static int ops_cp_get_errorhl_stat(int *stat, void *data)
{
	struct sc8541_device *sc = data;
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_17, &val);
	if (ret) {
		mca_log_err("%s get pmid error stat fail\n", sc->log_tag);
		return ret;
	}

	if (val & (1 << 3))
		*stat = 1;
	else if (val & (1 << 4))
		*stat = 2;
	else
		*stat = 0;

	mca_log_info("%s val: 0x%02x, stat: %d\n", sc->log_tag, val, *stat);
	return 0;
}

static int ops_cp_enable_busucp(bool en, void *data)
{
	struct sc8541_device *sc = data;
	int ret;

	ret = sc8541_update_bits(sc, SC8541_REG_05, 0x80, en ? 0 : 0x80);
	if (ret)
		mca_log_err("%s failed to [%d] busucp ret=%d\n", sc->log_tag,
			    en, ret);
	return ret;
}

static int sc8541_check_adc_enabled(struct sc8541_device *sc, bool *en)
{
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_23, &val);
	mca_log_info("%s >>>reg [0x23] = 0x%02x\n", sc->log_tag, val);
	if (ret)
		return ret;
	*en = !!(val & SC8541_ADC_EN_MASK);
	return 0;
}

static int sc8541_check_charge_enabled(struct sc8541_device *sc, bool *en)
{
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_0F, &val);
	mca_log_info("%s >>>reg [0x0F] = 0x%02x\n", sc->log_tag, val);
	if (ret)
		return ret;
	*en = !!(val & SC8541_CHG_EN_MASK);
	return 0;
}

static int sc8541_get_ovpgate_status(struct sc8541_device *sc, bool *en)
{
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_40, &val);
	mca_log_info("%s ovpgate_status SC8541_REG_40=0x%x\n", sc->log_tag, val);
	if (ret) {
		mca_log_info("%s get ovpgate status fail\n", sc->log_tag);
		return ret;
	}
	*en = !!(val & SC8541_OVPGATE_EN_MASK);
	return 0;
}

static int ops_cp_get_adc_enable(bool *en, void *data)
{
	struct sc8541_device *sc = data;
	int ret;

	ret = sc8541_check_adc_enabled(sc, en);
	if (ret)
		mca_log_err("%s failed get enable cp adc status ret =%d\n",
			    sc->log_tag, ret);
	return ret;
}

static int ops_cp_get_vout(u32 *val, void *data)
{
	struct sc8541_device *sc = data;

	return sc8541_get_adc_data(sc, SC8541_ADC_VOUT, val);
}

static int ops_cp_enable_vbus_errorhi(bool en, void *data)
{
	struct sc8541_device *sc = data;

	return sc8541_update_bits(sc, SC8541_REG_21, SC8541_VBUS_ERRORHI_EN_MASK,
				  en ? 0 : SC8541_VBUS_ERRORHI_EN_MASK);
}

static int ops_cp_enable_vbus_errorlo(bool en, void *data)
{
	struct sc8541_device *sc = data;

	return sc8541_update_bits(sc, SC8541_REG_21, SC8541_VBUS_ERRORLO_EN_MASK,
				  en ? 0 : SC8541_VBUS_ERRORLO_EN_MASK);
}

static int ops_cp_set_busovp(int mv, void *data)
{
	struct sc8541_device *sc = data;
	int ret;

	if (mv <= SC8541_BUSOVP_BASE_MV)
		mv = SC8541_BUSOVP_BASE_MV;
	ret = sc8541_update_bits(sc, SC8541_REG_06, 0xff,
				 (mv - SC8541_BUSOVP_BASE_MV) / SC8541_BUSOVP_STEP_MV);
	if (ret)
		mca_log_err("%s failed set cp busovp\n", sc->log_tag);
	return ret;
}

static int ops_cp_set_manual_revchg_mode(bool en, void *data)
{
	struct sc8541_device *sc = data;
	int ret;

	ret  = sc8541_update_bits(sc, SC8541_REG_05, 0x80, en ? 0x80 : 0);
	ret |= sc8541_update_bits(sc, SC8541_REG_05, 0x04, en ? 0x04 : 0);
	ret |= sc8541_update_bits(sc, SC8541_REG_41, 0x80, en ? 0x80 : 0);
	ret |= sc8541_update_bits(sc, SC8541_REG_41, 0x10, en ? 0x10 : 0);
	if (ret)
		mca_log_err("%s set revchg fail %d, ret %d\n", sc->log_tag, en,
			    ret);

	if (en) {
		if (!sc->quick_revchg_ws->active) {
			mca_log_info("%s awake quick revchg lock\n", sc->log_tag);
			__pm_stay_awake(sc->quick_revchg_ws);
		} else {
			mca_log_info("%s quick revchg Locked, ignore...\n",
				     sc->log_tag);
		}
	} else {
		if (sc->quick_revchg_ws->active) {
			mca_log_info("%s Release quick revchg lock\n",
				     sc->log_tag);
			__pm_relax(sc->quick_revchg_ws);
		} else {
			mca_log_info("%s quick revchg released, ignore...\n",
				     sc->log_tag);
		}
	}
	return ret;
}

static int ops_cp_set_revchg(bool en, void *data)
{
	struct sc8541_device *sc = data;
	bool ovpgate_en = 0, chg_en = 0;
	u8 val = 0;
	int cp_mode, retry;

	for (retry = 0; retry < 10; retry++) {
		ovpgate_en = 0;
		chg_en = 0;
		if (en) {
			sc8541_update_bits(sc, SC8541_REG_40, 0x40, 0x40);
			sc8541_enable_ovpgate(sc, true);
			sc8541_get_ovpgate_status(sc, &ovpgate_en);
			mca_log_info("%s ovpgate_enable status: %d\n",
				     sc->log_tag, ovpgate_en);
			if (ovpgate_en)
				return 0;
		} else {
			sc8541_update_bits(sc, SC8541_REG_0F,
					   SC8541_CHG_EN_MASK, 0);
			sc8541_set_operation_mode(sc, 1);
			sc8541_get_ovpgate_status(sc, &ovpgate_en);
			cp_mode = 0;
			if (!sc->i2c_disabled) {
				sc8541_read_reg_locked(sc, SC8541_REG_0F, &val);
				cp_mode = !!(val & SC8541_CP_MODE_MASK);
			}
			sc8541_check_charge_enabled(sc, &chg_en);
			mca_log_info("%s cp mode: %d, charging_enable: %d\n",
				     sc->log_tag, cp_mode, chg_en);
			if (!cp_mode && !chg_en)
				return 0;
		}
		mca_log_info("%s failed set revchg: retry:%d\n", sc->log_tag,
			     retry);
		msleep(20);
	}
	return -1;
}

static struct platform_class_cp_ops sc8541_chg_ops = {
	.cp_set_enable = ops_cp_enable_charge,
	.cp_get_enabled = ops_cp_get_charge_enable,
	.cp_get_present = ops_cp_get_present,
	.cp_get_battery_voltage = ops_cp_get_vbatt,
	.cp_get_battery_current = ops_cp_get_ibatt,
	.cp_get_battery_temperature = ops_cp_get_battery_temmperature,
	.cp_get_bus_voltage = ops_cp_get_vbus,
	.cp_get_bus_current = ops_cp_get_ibus,
	.cp_get_usb_voltage = ops_cp_get_vusb,
	.cp_get_tdie = ops_cp_get_tdie,
	.cp_set_mode = ops_cp_set_mode,
	.cp_get_mode = ops_cp_get_mode,
	.cp_device_init = ops_cp_device_init,
	.cp_enable_adc = ops_cp_enable_adc,
	.cp_get_bypass_support = ops_cp_get_bypass_support,
	.cp_dump_register = ops_cp_dump_register,
	.cp_get_chip_vendor = ops_cp_get_chip_vendor,
	.cp_get_probe_ok = ops_cp_get_probe_status,
	.cp_enable_acdrv_manual = ops_enable_acdrv_manual,
	.cp_enable_ovpgate = ops_cp_enable_ovpgate,
	.cp_enable_ovpgate_with_check = ops_cp_enable_ovpgate_with_check,
	.cp_get_ovpgate_status = ops_cp_get_ovpgate_status,
	.cp_get_int_stat = ops_cp_get_int_stat,
	.cp_get_errorhl_stat = ops_cp_get_errorhl_stat,
	.cp_enable_busucp = ops_cp_enable_busucp,
	.cp_get_battery_vout = ops_cp_get_vout,
	.cp_get_adc_enabled = ops_cp_get_adc_enable,
	.cp_enable_vbus_errorhi = ops_cp_enable_vbus_errorhi,
	.cp_enable_vbus_errorlo = ops_cp_enable_vbus_errorlo,
	.cp_set_busovp = ops_cp_set_busovp,
	.cp_set_manual_revchg_mode = ops_cp_set_manual_revchg_mode,
	.cp_set_cp_reverse_mode = ops_cp_set_revchg,
};

/* ---- debugfs ---- */

#ifdef CONFIG_DEBUG_FS
enum cp_attr_list {
	CP_DEBUG_PROP_ADDRESS,
	CP_DEBUG_PROP_COUNT,
	CP_DEBUG_PROP_DATA,
};

static struct reg_context {
	int address;
	int count;
	int data;
} reg_info;

static ssize_t cp_debugfs_show(void *priv_data, char *buf)
{
	struct mca_debugfs_attr_data *attr_data = priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct sc8541_device *sc = attr_data->private;
	char read_buf[MAX_LENGTH_BYTE] = { '\0' };
	ssize_t count = 0;
	u8 val = 0;
	int i;

	if (!sc || !attr_info) {
		mca_log_err("null pointer show\n");
		return count;
	}

	switch (attr_info->debugfs_attr_name) {
	case CP_DEBUG_PROP_ADDRESS:
		count = scnprintf(buf, PAGE_SIZE, "%02x\n", reg_info.address);
		break;
	case CP_DEBUG_PROP_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%x\n", reg_info.count);
		break;
	case CP_DEBUG_PROP_DATA:
		for (i = 0; i < reg_info.count; i++) {
			sc8541_read_reg_locked(sc, reg_info.address + i, &val);
			count += scnprintf(read_buf, MAX_LENGTH_BYTE,
					   "%02x: %02x\n", reg_info.address + i,
					   val);
			strcat(buf, read_buf);
		}
		break;
	default:
		break;
	}
	return count;
}

static ssize_t cp_debugfs_store(void *priv_data, const char *buf, size_t count)
{
	struct mca_debugfs_attr_data *attr_data = priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct sc8541_device *sc = attr_data->private;
	int val = 0;

	if (!sc || !attr_info) {
		mca_log_err("null pointer store\n");
		return count;
	}

	if (kstrtoint(buf, 16, &val))
		return -EINVAL;

	switch (attr_info->debugfs_attr_name) {
	case CP_DEBUG_PROP_ADDRESS:
		reg_info.address = val;
		break;
	case CP_DEBUG_PROP_COUNT:
		if (val > SC8541_MAX_REG)
			reg_info.count = SC8541_MAX_REG;
		else if (reg_info.count < 1)
			reg_info.count = 1;
		else
			reg_info.count = val;
		break;
	case CP_DEBUG_PROP_DATA:
		if (!sc->i2c_disabled) {
			mutex_lock(&sc->i2c_rw_lock);
			__sc8541_write_byte(sc, reg_info.address, val);
			mutex_unlock(&sc->i2c_rw_lock);
		}
		break;
	default:
		break;
	}
	return count;
}

static struct mca_debugfs_attr_info cp_debugfs_field_tbl[] = {
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_ADDRESS, address),
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_COUNT, count),
	mca_debugfs_attr(cp_debugfs, 0600, CP_DEBUG_PROP_DATA, data),
};
#define CP_DEBUGFS_ATTRS_SIZE ARRAY_SIZE(cp_debugfs_field_tbl)
#endif /* CONFIG_DEBUG_FS */

static int sc8541_parse_dt(struct sc8541_device *sc)
{
	struct device_node *node = sc->dev->of_node;

	if (!node) {
		mca_log_err("device tree info missing\n");
		return -ENODEV;
	}

	mca_parse_dts_u32(node, "ic_role", &sc->cp_role, 0);
	scnprintf(sc->log_tag, sizeof(sc->log_tag), "[%d]",
		  sc->cp_role == SC8541_ROLE_SLAVE ? 1 : 0);

	sc->irq_gpio = of_get_named_gpio(node, "cp-int", 0);
	if (sc->irq_gpio < 0) {
		mca_log_err("%s failed to parse irq_gpio\n", sc->log_tag);
		return -ENODEV;
	}

	mca_parse_dts_u32(node, "bat-ovp-threshold", &sc->bat_ovp, 0x1194);
	mca_parse_dts_u32(node, "bat-ovp-alarm-threshold", &sc->bat_ovp_alarm,
			  0x1130);
	mca_parse_dts_u32_array(node, "bus-ovp-threshold", sc->bus_ovp, 2);
	mca_parse_dts_u32_array(node, "bus-ocp-threshold", sc->bus_ocp, 2);
	mca_parse_dts_u32_array(node, "usb-ovp-threshold", sc->usb_ovp, 2);
	sc->support_multiple_cp =
		of_find_property(node, "support-multiple-cp", NULL);
	mca_parse_dts_u32(node, "bus-ucp-fall-deglitch",
			  &sc->bus_ucp_fall_deglitch, 0);
	sc->support_wls = of_find_property(node, "support-wls", NULL);
	mca_log_info("%s support-wls %d\n", sc->log_tag, sc->support_wls);

	return 0;
}

static int sc8541_detect_device(struct sc8541_device *sc)
{
	u8 val = 0;
	int ret;

	ret = sc8541_read_reg_locked(sc, SC8541_REG_DEVICE_ID, &val);
	if (ret)
		val = 0;
	sc->device_id = val;
	mca_log_info("%s sucess read device id = %x\n", sc->log_tag, val);
	if (val != SC8541_DEVICE_ID_VAL) {
		mca_log_info("%s device_id is invalid\n", sc->log_tag);
		return -ENODEV;
	}
	return 0;
}

static int sc8541_register_irq(struct sc8541_device *sc)
{
	int ret;

	ret = devm_gpio_request(sc->dev, sc->irq_gpio, sc->log_tag);
	if (ret < 0) {
		mca_log_info("%s failed to master request gpio\n", sc->log_tag);
		return ret;
	}

	sc->irq = gpiod_to_irq(gpio_to_desc(sc->irq_gpio));
	if (sc->irq < 0) {
		mca_log_info("%s failed to master get gpio_irq\n", sc->log_tag);
		return sc->irq;
	}

	ret = request_threaded_irq(sc->irq, sc8541_int_isr, NULL,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   sc->log_tag, sc);
	if (ret < 0) {
		mca_log_info("%s failed to master request irq\n", sc->log_tag);
		return ret;
	}

	irq_set_irq_wake(sc->irq, 1);
	return 0;
}

static void sc8541_register_platform(struct sc8541_device *sc)
{
	sc8541_update_bits(sc, SC8541_REG_0F, 0x80, 0);
	platform_class_cp_register_ops(sc->cp_role, &sc8541_chg_ops, sc);
}

static int sc8541_probe(struct i2c_client *client)
{
	struct sc8541_device *sc;
	int data = 0;
	int ret;

	mca_log_err("start\n");

	sc = devm_kzalloc(&client->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->dev = &client->dev;
	sc->client = client;
	i2c_set_clientdata(client, sc);
	mutex_init(&sc->i2c_rw_lock);
	mutex_init(&sc->data_lock);
	mutex_init(&sc->irq_complete);
	sc->adc_scaled = 1;

	ret = sc8541_parse_dt(sc);
	if (ret) {
		mca_log_err("%s failed to parse DTS\n", sc->log_tag);
		return ret;
	}

	ret = sc8541_detect_device(sc);
	if (ret) {
		mca_log_err("No sc8541 device found!\n");
		mca_charge_mievent_report(0x27, &data, 2);
		return -ENODEV;
	}

	sc->chip_vendor = 0;
	sc->present = true;
	INIT_DELAYED_WORK(&sc->irq_work, sc8541_irq_handler);

	ret = sc8541_init_device(sc);
	if (ret) {
		mca_log_err("Failed to init device\n");
		mca_charge_mievent_report(0x27, &data, 2);
		return ret;
	}

	ret = sc8541_register_irq(sc);
	if (ret) {
		mca_log_err("%s failed to int irq\n", sc->log_tag);
		return ret;
	}

	sc8541_register_platform(sc);

#ifdef CONFIG_DEBUG_FS
	reg_info.address = 0;
	reg_info.count = 1;
	mca_debugfs_create_group(sc->cp_role ? "sc85xx_01" : "sc85xx_00",
				 cp_debugfs_field_tbl, CP_DEBUGFS_ATTRS_SIZE,
				 sc);
#endif
	sc->quick_revchg_ws = wakeup_source_register(
		sc->dev,
		sc->cp_role ? "sc8541_quick_revchg_slave" :
			      "sc8541_quick_revchg");

	sc->probe_done = 1;
	mca_log_err("%s probe success %d\n", sc->log_tag, 0);
	return 0;
}

static void sc8541_remove(struct i2c_client *client)
{
	struct sc8541_device *sc = i2c_get_clientdata(client);

	sc8541_update_bits(sc, SC8541_REG_23, SC8541_ADC_EN_MASK, 0);
	sc->charging_active = 0;
}

static void sc8541_shutdown(struct i2c_client *client)
{
	struct sc8541_device *sc = i2c_get_clientdata(client);

	sc8541_update_bits(sc, SC8541_REG_23, SC8541_ADC_EN_MASK, 0);
	if (sc->support_wls) {
		sc8541_update_bits(sc, SC8541_REG_10, 0x18, 0x10);
		sc8541_update_bits(sc, SC8541_REG_10, 0x04, 0);
	}
	mca_log_err("sc8541_shutdown!\n");
	sc->charging_active = 0;
}

static int sc8541_suspend(struct device *dev)
{
	struct sc8541_device *sc = dev_get_drvdata(dev);

	sc8541_update_bits(sc, SC8541_REG_23, SC8541_ADC_EN_MASK, 0);
	sc->charging_active = 0;
	sc->acdrv_init_done = 0;
	mca_log_err("Suspend successfully!\n");
	return 0;
}

static int sc8541_resume(struct device *dev)
{
	struct sc8541_device *sc = dev_get_drvdata(dev);

	if (!sc) {
		mca_log_err("sc get_i2c_client fail!\n");
		return -EINVAL;
	}
	sc->acdrv_init_done = 1;
	mca_log_err("Resume successfully!\n");
	return 0;
}

static int sc8541_suspend_noirq(struct device *dev)
{
	struct sc8541_device *sc = dev_get_drvdata(dev);

	if (sc->resume_pending) {
		pr_err_ratelimited(
			"[sc8541] %s: Aborting suspend, an interrupt was detected while suspending\n",
			__func__);
		return -EBUSY;
	}

	return 0;
}

static const struct dev_pm_ops sc8541_pm_ops = {
	.suspend = sc8541_suspend,
	.resume = sc8541_resume,
	.suspend_noirq = sc8541_suspend_noirq,
};

static const struct of_device_id sc8541_of_match[] = {
	{ .compatible = "sc8541" },
	{ .compatible = "sc8541_master" },
	{ .compatible = "sc8541_slave" },
	{},
};
MODULE_DEVICE_TABLE(of, sc8541_of_match);

static const struct i2c_device_id sc8541_i2c_id[] = {
	{ "sc8541", 0 },
	{},
};

static struct i2c_driver sc8541_driver = {
	.driver = {
		.name = "sc8541_charger_pump",
		.of_match_table = sc8541_of_match,
		.pm = &sc8541_pm_ops,
	},
	.probe = sc8541_probe,
	.remove = sc8541_remove,
	.shutdown = sc8541_shutdown,
	.id_table = sc8541_i2c_id,
};
module_i2c_driver(sc8541_driver);

MODULE_DESCRIPTION("SC sc8541 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
