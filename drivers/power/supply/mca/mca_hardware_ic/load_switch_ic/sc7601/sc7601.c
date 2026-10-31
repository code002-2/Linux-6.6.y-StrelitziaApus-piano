#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <mca/platform/platform_loadsw_class.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include "inc/sc7601_reg.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "sc7601_ic"
#endif

#define MAX_LENGTH_BYTE 600
#define SC7601_DUMP_BUF_LEN 0x100
#define SC7601_PRESENT_RETRY 3

struct sc7601_device {
	struct i2c_client *client;
	struct device *dev;
	int bat_ovp;
	int pre_chg_curr;
	int vfc_chg_volt;
	int limit_chg_curr;
	bool present;
	int role;
	char log_tag[8];
};

static const u8 sc760x_reg_list[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x15,
};

static int ls_read_byte(struct sc7601_device *sc, u8 reg, u8 *val)
{
	s32 ret = i2c_smbus_read_byte_data(sc->client, reg);

	if (ret < 0) {
		mca_log_err(
			"i2c read word fail: can't read from reg 0x%02X, errcode=%d\n",
			reg, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static int ls_write_byte(struct sc7601_device *sc, u8 reg, u8 val)
{
	s32 ret = i2c_smbus_write_byte_data(sc->client, reg, val);

	if (ret < 0) {
		mca_log_err(
			"i2c write word fail: can't write to reg 0x%02X, errcode=%d\n",
			reg, ret);
		return ret;
	}
	return 0;
}

static int ls_update_bits(struct sc7601_device *sc, u8 reg, u8 mask, u8 val)
{
	u8 tmp = 0;

	ls_read_byte(sc, reg, &tmp);
	tmp = (tmp & ~mask) | (val & mask);
	return ls_write_byte(sc, reg, tmp);
}

static int sc760x_set_lowpower_mode(struct sc7601_device *sc, bool enable)
{
	u8 val = 0;
	int ret;

	ret = ls_update_bits(sc, SC7601_REG_04, SC7601_LOWPOWER_MASK,
			     enable ? SC7601_LOWPOWER_MASK : 0);

	ls_read_byte(sc, SC7601_REG_04, &val);
	mca_log_err("%s read reg: 0x%02x, data 0x%02x\n", sc->log_tag,
		    SC7601_REG_04, val);
	return ret;
}

static int ops_loadsw_get_present(bool *present, void *data)
{
	struct sc7601_device *sc = data;
	u8 val;
	int i;

	for (i = 0; i < SC7601_PRESENT_RETRY; i++) {
		if (!ls_read_byte(sc, SC7601_REG_03, &val)) {
			sc->present = true;
			*present = true;
			return 0;
		}
		sc->present = false;
	}

	*present = false;
	return 0;
}

static int ops_loadsw_get_ibat_limit(int *ibat_limit, void *data)
{
	struct sc7601_device *sc = data;
	u8 val = 0;
	int ret;

	ret = ls_read_byte(sc, SC7601_REG_01, &val);
	*ibat_limit = ret ? 50 : val * 50 + 50;
	return ret;
}

static int ops_loadsw_set_ibat_limit(int val, void *data)
{
	struct sc7601_device *sc = data;
	int ret;

	if (val < 51)
		val = 50;
	ret = ls_write_byte(sc, SC7601_REG_01, (val - 50) / 50);
	if (ret)
		mca_log_err("%s failed set ibat limit\n", sc->log_tag);
	return ret;
}

static int ops_loadsw_set_lowpower_mode(bool mode, void *data)
{
	struct sc7601_device *sc = data;
	int ret = sc760x_set_lowpower_mode(sc, mode);

	if (ret)
		mca_log_err("%s failed set lowpower mode\n", sc->log_tag);
	return ret;
}

static int ops_loadsw_get_lowpower_mode(bool *mode, void *data)
{
	struct sc7601_device *sc = data;
	u8 val = 0;
	int ret;

	ret = ls_read_byte(sc, SC7601_REG_04, &val);
	*mode = ret ? false : !!(val & SC7601_LOWPOWER_MASK);
	return ret;
}

static struct platform_class_loadsw_ops sc7601_chg_ops = {
	.loadsw_get_present = ops_loadsw_get_present,
	.loadsw_get_ibat_limit = ops_loadsw_get_ibat_limit,
	.loadsw_set_ibat_limit = ops_loadsw_set_ibat_limit,
	.loadsw_set_lowpower_mode = ops_loadsw_set_lowpower_mode,
	.loadsw_get_lowpower_mode = ops_loadsw_get_lowpower_mode,
};

#ifdef CONFIG_DEBUG_FS
enum ls_attr_list {
	LS_DEBUG_PROP_ADDRESS,
	LS_DEBUG_PROP_COUNT,
	LS_DEBUG_PROP_DATA,
};

static struct reg_context {
	int address;
	int count;
	int data;
} reg_info;

static ssize_t ls_debugfs_show(void *priv_data, char *buf)
{
	struct mca_debugfs_attr_data *attr_data = priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct sc7601_device *sc = attr_data->private;
	char read_buf[MAX_LENGTH_BYTE] = { '\0' };
	ssize_t count = 0;
	u8 val = 0;
	int i;

	if (!sc || !attr_info) {
		mca_log_err("null pointer show\n");
		return count;
	}

	switch (attr_info->debugfs_attr_name) {
	case LS_DEBUG_PROP_ADDRESS:
		count = scnprintf(buf, PAGE_SIZE, "%02x\n", reg_info.address);
		break;
	case LS_DEBUG_PROP_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%x\n", reg_info.count);
		break;
	case LS_DEBUG_PROP_DATA:
		for (i = 0; i < reg_info.count; i++) {
			ls_read_byte(sc, reg_info.address + i, &val);
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

static ssize_t ls_debugfs_store(void *priv_data, const char *buf, size_t count)
{
	struct mca_debugfs_attr_data *attr_data = priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct sc7601_device *sc = attr_data->private;
	int val = 0;

	if (!sc || !attr_info) {
		mca_log_err("null pointer store\n");
		return count;
	}

	if (kstrtoint(buf, 16, &val))
		return -EINVAL;

	switch (attr_info->debugfs_attr_name) {
	case LS_DEBUG_PROP_ADDRESS:
		reg_info.address = val;
		break;
	case LS_DEBUG_PROP_COUNT:
		if (val > SC7601_REG_MAX)
			reg_info.count = SC7601_REG_MAX;
		else if (reg_info.count < 1)
			reg_info.count = 1;
		else
			reg_info.count = val;
		break;
	case LS_DEBUG_PROP_DATA:
		ls_write_byte(sc, reg_info.address, val);
		break;
	default:
		break;
	}
	return count;
}

static struct mca_debugfs_attr_info ls_debugfs_field_tbl[] = {
	mca_debugfs_attr(ls_debugfs, 0664, LS_DEBUG_PROP_ADDRESS, address),
	mca_debugfs_attr(ls_debugfs, 0664, LS_DEBUG_PROP_COUNT, count),
	mca_debugfs_attr(ls_debugfs, 0600, LS_DEBUG_PROP_DATA, data),
};
#define LS_DEBUGFS_ATTRS_SIZE ARRAY_SIZE(ls_debugfs_field_tbl)
#endif /* CONFIG_DEBUG_FS */

/* ---- probe ---- */

static int sc7601_parse_dt(struct sc7601_device *sc)
{
	struct device_node *node = sc->dev->of_node;

	if (!node) {
		mca_log_err("device tree info missing\n");
		return -ENODEV;
	}

	mca_parse_dts_u32(node, "ic_role", &sc->role, 0);
	scnprintf(sc->log_tag, sizeof(sc->log_tag), "[%d]",
		  sc->role == 1 ? 1 : 0);

	mca_parse_dts_u32(node, "bat-ovp-threshold", &sc->bat_ovp, 0x157c);
	mca_parse_dts_u32(node, "pre-chg-curr", &sc->pre_chg_curr, 100);
	mca_parse_dts_u32(node, "vfc-chg-volt", &sc->vfc_chg_volt, 0xc1c);
	mca_parse_dts_u32(node, "limit-chg-curr", &sc->limit_chg_curr, 7000);
	mca_log_info("%s bat-ovp:%d, prechg:%d, vfcchg:%d\n", sc->log_tag,
		     sc->bat_ovp, sc->pre_chg_curr, sc->vfc_chg_volt);

	return 0;
}

static void sc7601_init_device(struct sc7601_device *sc)
{
	int limit, prechg, vfc, bat_ovp;
	u8 val;

	ls_update_bits(sc, SC7601_REG_04, SC7601_REG04_EN_MASK,
		       SC7601_REG04_EN_MASK);

	limit = sc->limit_chg_curr < 51 ? 50 : sc->limit_chg_curr;
	ls_write_byte(sc, SC7601_REG_01, (limit - 50) / 50);

	ls_write_byte(sc, SC7601_REG_02, 0x0b);
	ls_write_byte(sc, SC7601_REG_03, 0x90);

	sc760x_set_lowpower_mode(sc, true);

	bat_ovp = sc->bat_ovp < 4001 ? 4000 : sc->bat_ovp;
	val = 0;
	ls_read_byte(sc, SC7601_REG_08, &val);
	val = (val & 0x83) |
	      ((((((bat_ovp - 4000) >> 1) & 0x7fff) * 0x147b) >> 15) & 0x7c);
	ls_write_byte(sc, SC7601_REG_08, val);

	prechg = sc->pre_chg_curr < 51 ? 50 : sc->pre_chg_curr;
	val = 0;
	ls_read_byte(sc, SC7601_REG_06, &val);
	val = (val & 0x0f) |
	      ((((((prechg - 50) >> 1) & 0x7fff) * 0x147b) >> 13) & 0x7ff0);
	ls_write_byte(sc, SC7601_REG_06, val);

	vfc = sc->vfc_chg_volt < 51 ? 50 : sc->vfc_chg_volt;
	val = 0;
	ls_read_byte(sc, SC7601_REG_06, &val);
	val = (val & 0xf0) | (((vfc - 0xaf0) / 50) & 0x0f);
	ls_write_byte(sc, SC7601_REG_06, val);

	mca_log_err("%s success to init load switch device\n", sc->log_tag);
}

static void sc760x_dev_dump(struct sc7601_device *sc)
{
	char buf[SC7601_DUMP_BUF_LEN];
	u8 val;
	int i, len = 0;

	buf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(sc760x_reg_list); i++) {
		val = 0;
		ls_read_byte(sc, sc760x_reg_list[i], &val);
		len += scnprintf(buf + len, sizeof(buf) - len,
				 "[0x%02X]=0x%02X,", sc760x_reg_list[i], val);
		if (i == ARRAY_SIZE(sc760x_reg_list) - 1 ||
		    ((i + 1) & 7) == 0) {
			mca_log_info("%s %s\n", sc->log_tag, buf);
			len = 0;
			buf[0] = '\0';
		}
	}
}

static int sc7601_probe(struct i2c_client *client)
{
	struct sc7601_device *sc;
	int ret;

	sc = devm_kzalloc(&client->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->client = client;
	sc->dev = &client->dev;
	i2c_set_clientdata(client, sc);

	ret = sc7601_parse_dt(sc);
	if (ret) {
		mca_log_err("failed to parse DTS\n");
		return ret;
	}

	ls_update_bits(sc, SC7601_REG_04, SC7601_REG04_EN_MASK,
		       SC7601_REG04_EN_MASK);
	platform_class_loadsw_register_ops(sc->role, &sc7601_chg_ops, sc);
	sc7601_init_device(sc);

	sc->present = true;
	sc760x_dev_dump(sc);

#ifdef CONFIG_DEBUG_FS
	reg_info.address = 0;
	reg_info.count = 1;
	mca_debugfs_create_group("sc760x", ls_debugfs_field_tbl,
				 LS_DEBUGFS_ATTRS_SIZE, sc);
#endif
	mca_log_err("%s probe success %d\n", sc->log_tag, 0);
	return 0;
}

static void sc_loadswitch_remove(struct i2c_client *client)
{
}

static void sc_loadswitch_shutdown(struct i2c_client *client)
{
	mca_log_info("sc load switch driver shutdown!\n");
}

static int sc_loadswitch_suspend(struct device *dev)
{
	mca_log_info("suspend\n");
	return 0;
}

static int sc_loadswitch_resume(struct device *dev)
{
	mca_log_info("resume\n");
	return 0;
}

static const struct dev_pm_ops sc_loadswitch_pm_ops = {
	.suspend = sc_loadswitch_suspend,
	.resume = sc_loadswitch_resume,
};

static const struct of_device_id sc_loadswitch_of_match[] = {
	{ .compatible = "sc7601" },
	{},
};
MODULE_DEVICE_TABLE(of, sc_loadswitch_of_match);

static const struct i2c_device_id sc_loadswitch_i2c_id[] = {
	{ "sc7601", 0 },
	{},
};

static struct i2c_driver sc_loadswitch_driver = {
	.driver = {
		.name = "sc7601",
		.of_match_table = sc_loadswitch_of_match,
		.pm = &sc_loadswitch_pm_ops,
	},
	.probe = sc7601_probe,
	.remove = sc_loadswitch_remove,
	.shutdown = sc_loadswitch_shutdown,
	.id_table = sc_loadswitch_i2c_id,
};
module_i2c_driver(sc_loadswitch_driver);

MODULE_DESCRIPTION("SC sc7601 Driver");
MODULE_AUTHOR("yinshunan <yinshunan@xiaomi.com>");
MODULE_LICENSE("GPL v2");
