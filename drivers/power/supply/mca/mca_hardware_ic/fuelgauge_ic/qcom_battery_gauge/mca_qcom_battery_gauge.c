// SPDX-License-Identifier: GPL-2.0
/*
 * MCA fuel gauge IC driver for the Qualcomm battery gauge (QBG).
 *
 * The gauge itself runs on the ADSP; this driver reads its data over the MCA
 * QBG glink channel, feeds it the charge termination targets, and keeps the
 * cycle count, dates and UI state of health in the onewire authentication
 * chip of the battery pack.
 *
 * Written by sidharthify (Sidharth Sharma) <wednisegit@gmail.com>
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <mca/common/mca_adsp_glink.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_smem.h>
#include <mca/common/mca_sysfs.h>
#include <mca/hardware/onewire_auth.h>
#include <mca/platform/platform_fg_ic_ops.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "qcom_battery_gauge"
#endif

#define QBG_DATA_INVALID		-999999999
#define QBG_FAKE_TEMP_NONE		-999
#define QBG_FAKE_SOH_NONE		-1
#define QBG_PROBE_RETRY_MAX		200
#define QBG_ADAPT_POWER			45
#define QBG_RESISTANCE_ID		100000
#define QBG_TEMP_MAX			500
#define QBG_TEMP_MIN			-50
#define QBG_DATE_LEN			8

#define QBG_CELL_SN_SWD			2
#define QBG_PACK_VENDOR_SWD		2
#define QBG_PACK_VENDOR_NVT		3

/* readings cached by the driver, in the order of qbg_read_prop_id */
enum qbg_data {
	QBG_DATA_CURRENT,
	QBG_DATA_VOLTAGE,
	QBG_DATA_TEMP,
	QBG_DATA_RSOC,
	QBG_DATA_RM,
	QBG_DATA_FCC,
	QBG_DATA_CYCLE_COUNT,
	QBG_DATA_SOH,
	QBG_DATA_IBATT_FULL,
	QBG_DATA_VBATT_FULL,
	QBG_DATA_DESIGN_CAPACITY,
	QBG_DATA_CUTOFF_VOLTAGE,
	QBG_DATA_CHG_STATUS,
	QBG_DATA_CHG_FCC,
	QBG_DATA_MAX,
};

/* ADSP property of each reading */
static const u32 qbg_read_prop_id[QBG_DATA_MAX] = {
	[QBG_DATA_CURRENT] = 2,
	[QBG_DATA_VOLTAGE] = 1,
	[QBG_DATA_TEMP] = 3,
	[QBG_DATA_RSOC] = 0,
	[QBG_DATA_RM] = 6,
	[QBG_DATA_FCC] = 5,
	[QBG_DATA_CYCLE_COUNT] = 4,
	[QBG_DATA_SOH] = 11,
	[QBG_DATA_IBATT_FULL] = 7,
	[QBG_DATA_VBATT_FULL] = 8,
	[QBG_DATA_DESIGN_CAPACITY] = 12,
	[QBG_DATA_CUTOFF_VOLTAGE] = 13,
	[QBG_DATA_CHG_STATUS] = 9,
	[QBG_DATA_CHG_FCC] = 10,
};

/* properties the driver writes to the ADSP */
#define QBG_PROP_IBATT_FULL		7
#define QBG_PROP_VBATT_FULL		8
#define QBG_PROP_CHG_STATUS		9
#define QBG_PROP_CHG_FCC		10

/* used while the ADSP has never answered for a reading */
static const int qbg_default_data[QBG_DATA_MAX] = {
	[QBG_DATA_CURRENT] = 0,
	[QBG_DATA_VOLTAGE] = 3700,
	[QBG_DATA_TEMP] = 250,
	[QBG_DATA_RSOC] = 35,
	[QBG_DATA_RM] = 1234567,
	[QBG_DATA_FCC] = 3456789,
	[QBG_DATA_CYCLE_COUNT] = 0,
	[QBG_DATA_SOH] = 100,
	[QBG_DATA_IBATT_FULL] = QBG_DATA_INVALID,
	[QBG_DATA_VBATT_FULL] = QBG_DATA_INVALID,
	[QBG_DATA_DESIGN_CAPACITY] = 3456789,
	[QBG_DATA_CUTOFF_VOLTAGE] = 3210,
	[QBG_DATA_CHG_STATUS] = QBG_DATA_INVALID,
	[QBG_DATA_CHG_FCC] = QBG_DATA_INVALID,
};

/* ADSP notifications, and the reading each one updates */
static const int qbg_notify_data[] = {
	[0] = QBG_DATA_RSOC,
	[1] = -1,
	[2] = QBG_DATA_VOLTAGE,
	[3] = QBG_DATA_CURRENT,
	[4] = QBG_DATA_TEMP,
	[5] = QBG_DATA_FCC,
	[6] = QBG_DATA_RM,
};

/* battery full current for a termination current in quick charging */
struct qbg_full_param {
	u32 iterm;
	u32 ibatt_full;
};

struct qbg_full_table {
	struct qbg_full_param *params;
	int count;
};

struct qbg_info {
	struct device *dev;
	struct device *sysfs_dev;
	struct mutex data_lock;

	u32 buck_ibatt_full_delta;
	u32 buck_vbatt_full_delta;
	u32 quick_ibatt_full_delta;
	u32 quick_vbatt_full_delta;
	struct qbg_full_table full_table_swd;
	struct qbg_full_table full_table_nvt;
	struct qbg_full_table full_table_donghai;
	bool donghai;

	int fake_temp;
	int fake_soh;
	int fake_cycle_count;
	int authentic;
	int adapt_power;
	char device_name[8];
	const char *cell_name;
	int pack_vendor;
	int ui_soh;
	bool fastcharge;

	int data[QBG_DATA_MAX];

	/* the ADSP counts cycles from boot; the battery chip keeps them */
	bool cycle_count_init;
	int last_qbg_cycle_count;
	int ow_cycle_count;
};

static int qbg_read(struct qbg_info *info, enum qbg_data id, int *val)
{
	int *cache = &info->data[id];
	int ret;

	mutex_lock(&info->data_lock);
	ret = mca_adsp_glink_qbg_read_prop(qbg_read_prop_id[id], val, sizeof(*val));
	if (!ret) {
		*cache = *val;
	} else if (*cache == QBG_DATA_INVALID) {
		mca_log_err("failed to read data %d (%d), using %d\n",
			    qbg_read_prop_id[id], ret, qbg_default_data[id]);
		*cache = *val = qbg_default_data[id];
	} else {
		mca_log_err("failed to read data %d (%d), using the last value %d\n",
			    qbg_read_prop_id[id], ret, *cache);
		*val = *cache;
		ret = 0;
	}
	mutex_unlock(&info->data_lock);

	return ret;
}

static int qbg_write(struct qbg_info *info, u32 prop, enum qbg_data id, int val)
{
	int ret;

	if (info->data[id] == val)
		return 0;

	ret = mca_adsp_glink_qbg_write_prop(prop, &val, sizeof(val));
	if (ret)
		mca_log_err("failed to write data %d (%d)\n", prop, ret);
	else
		info->data[id] = val;

	return ret;
}

static int qbg_read_simple(struct qbg_info *info, enum qbg_data id, int *val)
{
	int ret = qbg_read(info, id, val);

	if (!ret)
		mca_log_debug("data %d: %d\n", id, *val);

	return ret;
}

static int qbg_get_probe_ok(void *data, bool *ok)
{
	*ok = true;
	return 0;
}

static int qbg_get_rsoc(void *data, int *val)
{
	return qbg_read_simple(data, QBG_DATA_RSOC, val);
}

static int qbg_get_curr(void *data, int *val)
{
	return qbg_read_simple(data, QBG_DATA_CURRENT, val);
}

static int qbg_get_volt(void *data, int *val)
{
	return qbg_read_simple(data, QBG_DATA_VOLTAGE, val);
}

static int qbg_set_temp(void *data, int temp)
{
	struct qbg_info *info = data;

	info->fake_temp = temp;
	mca_log_info("fake temperature: %d\n", temp);

	return 0;
}

static int qbg_get_temp(void *data, int *val)
{
	struct qbg_info *info = data;

	if (info->fake_temp == QBG_FAKE_TEMP_NONE)
		return qbg_read_simple(info, QBG_DATA_TEMP, val);

	*val = info->data[QBG_DATA_TEMP] = info->fake_temp;
	return 0;
}

static int qbg_get_rm(void *data, int *val)
{
	return qbg_read_simple(data, QBG_DATA_RM, val);
}

static int qbg_get_fcc(void *data, int *val)
{
	return qbg_read_simple(data, QBG_DATA_FCC, val);
}

static int qbg_get_full_design(void *data, int *val)
{
	return qbg_read_simple(data, QBG_DATA_DESIGN_CAPACITY, val);
}

static int qbg_get_cyclecount(void *data, int *val)
{
	struct qbg_info *info = data;
	int qbg_cycle_count = 0;
	int ret;

	if (info->fake_cycle_count) {
		*val = info->data[QBG_DATA_CYCLE_COUNT] = info->fake_cycle_count;
		return 0;
	}

	ret = qbg_read(info, QBG_DATA_CYCLE_COUNT, &qbg_cycle_count);
	if (ret)
		return ret;

	if (!info->cycle_count_init) {
		info->last_qbg_cycle_count = qbg_cycle_count;
		ret = onewire_get_batt_cycle_count(&info->ow_cycle_count);
		if (ret) {
			/* no battery chip to keep the count, report the ADSP one */
			*val = qbg_cycle_count;
			return 0;
		}
		info->cycle_count_init = true;
	}

	if (qbg_cycle_count > info->last_qbg_cycle_count) {
		ret = onewire_set_batt_cycle_count();
		if (!ret)
			ret = onewire_get_batt_cycle_count(&info->ow_cycle_count);
		if (ret) {
			mca_log_err("failed to update the battery cycle count: %d\n", ret);
			return ret;
		}
		info->last_qbg_cycle_count = qbg_cycle_count;
	}

	*val = info->data[QBG_DATA_CYCLE_COUNT] = info->ow_cycle_count;
	return 0;
}

static int qbg_get_soh(void *data, int *val)
{
	struct qbg_info *info = data;
	u8 ui_soh[ONEWIRE_UI_SOH_LEN] = { 0 };
	int soh = 0;
	int ret;

	if (info->fake_soh != QBG_FAKE_SOH_NONE) {
		*val = info->data[QBG_DATA_SOH] = info->fake_soh;
		return 0;
	}

	ret = qbg_read(info, QBG_DATA_SOH, &soh);
	if (ret)
		return ret;

	if (!soh) {
		if (!onewire_get_batt_ui_soh(ui_soh) && ui_soh[0])
			soh = ui_soh[0];
		else
			soh = 100;
		mca_log_info("no state of health from the gauge, using %d\n", soh);
	}

	*val = info->data[QBG_DATA_SOH] = soh;
	return 0;
}

/* filled for strategy_fg, laid out like its struct fg_batt_info */
struct qbg_batt_info {
	int curr;
	int volt;
	int vcell_max;
	int temp;
	int rsoc;
	int rm;
	int fcc;
	int cycle_count;
	int soh;
};

static int qbg_get_batt_info(void *data, void *pdata)
{
	struct qbg_info *info = data;
	struct qbg_batt_info *batt_info = pdata;
	int val;

	qbg_get_curr(info, &batt_info->curr);
	qbg_get_volt(info, &batt_info->volt);
	qbg_get_temp(info, &batt_info->temp);
	qbg_get_rsoc(info, &batt_info->rsoc);
	qbg_get_rm(info, &batt_info->rm);
	qbg_get_fcc(info, &batt_info->fcc);
	qbg_get_cyclecount(info, &batt_info->cycle_count);
	qbg_get_soh(info, &batt_info->soh);
	qbg_get_full_design(info, &val);
	batt_info->vcell_max = batt_info->volt;

	mca_log_debug("curr: %d, volt: %d, temp: %d, rsoc: %d, rm: %d, fcc: %d, cc: %d, soh: %d\n",
		      batt_info->curr, batt_info->volt, batt_info->temp,
		      batt_info->rsoc, batt_info->rm, batt_info->fcc,
		      batt_info->cycle_count, batt_info->soh);

	return 0;
}

static int qbg_get_fastcharge(void *data, int *val)
{
	struct qbg_info *info = data;

	*val = info->fastcharge;
	return 0;
}

static int qbg_set_fastcharge(void *data, bool enable)
{
	struct qbg_info *info = data;

	info->fastcharge = enable;
	return 0;
}

static int qbg_set_authentic(void *data, int authentic)
{
	struct qbg_info *info = data;

	info->authentic = authentic;
	return 0;
}

static int qbg_get_authentic(void *data, int *authentic)
{
	struct qbg_info *info = data;

	*authentic = info->authentic;
	return 0;
}

static int qbg_get_error_state(void *data, bool *error)
{
	*error = false;
	return 0;
}

static int qbg_get_cutoff_voltage(void *data, int *val)
{
	struct qbg_info *info = data;

	/* the cutoff voltage does not change, read it once */
	if (info->data[QBG_DATA_CUTOFF_VOLTAGE] != QBG_DATA_INVALID) {
		*val = info->data[QBG_DATA_CUTOFF_VOLTAGE];
		return 0;
	}

	return qbg_read_simple(info, QBG_DATA_CUTOFF_VOLTAGE, val);
}

static int qbg_set_cutoff_voltage(void *data, int val)
{
	return -EOPNOTSUPP;
}

static int qbg_get_adapt_power(void *data, int *val)
{
	struct qbg_info *info = data;

	*val = info->adapt_power;
	return 0;
}

static int qbg_get_raw_soc(void *data, int *val)
{
	struct qbg_info *info = data;
	int rm, fcc;

	if (qbg_read(info, QBG_DATA_RM, &rm) || qbg_read(info, QBG_DATA_FCC, &fcc))
		return -EIO;

	/* in hundredths of a percent */
	rm /= 1000;
	fcc /= 1000;
	*val = fcc ? rm * 10000 / fcc : 0;

	return 0;
}

static int qbg_get_batt_cell_info(void *data, const char **name)
{
	struct qbg_info *info = data;

	if (!info->cell_name)
		return -EINVAL;

	*name = info->cell_name;
	return 0;
}

static int qbg_get_device_name(void *data, const char **name)
{
	struct qbg_info *info = data;

	*name = info->device_name;
	return 0;
}

static int qbg_get_temp_max(void *data, int *val)
{
	*val = QBG_TEMP_MAX;
	return 0;
}

static int qbg_get_temp_min(void *data, int *val)
{
	*val = QBG_TEMP_MIN;
	return 0;
}

static void qbg_set_force_report_full(void *data)
{
}

static void qbg_get_ui_soh(void *data, int *val)
{
	struct qbg_info *info = data;

	*val = info->ui_soh;
}

static int qbg_get_pack_vendor(void *data, int *vendor)
{
	struct qbg_info *info = data;

	*vendor = info->pack_vendor;
	return 0;
}

/*
 * The battery chip stores the manufacturing date as one character per field.
 * The year counts from 2025 through the letters FGHJKLMNPRSTVWXY, then the
 * digits 1-9 and the letters A-E; the month is 1-9 or A-C, and the day 1-9,
 * A-Y for 10 to 30 (skipping I, O, Q and U) and 0 for 31. Older packs store
 * a digit for the last digit of the year and two digits for the day.
 */
static int qbg_get_manufacturing_date(void *data, u8 *buf)
{
	static const char year_codes[] = "FGHJKLMNPRSTVWXY";
	static const char day_codes[] = "ABCDEFGHJKLMNPRSTVWXY";
	u8 raw[ONEWIRE_PAGE_SIZE] = { 0 };
	char year[2], month[2], day[2];
	const char *p;
	int ret, y, d;

	ret = onewire_get_manufacturing_date(raw);
	if (ret) {
		mca_log_err("failed to read the manufacturing date: %d\n", ret);
		return ret;
	}

	if (raw[9] == '-') {
		p = strchr(year_codes, raw[6]);
		if (raw[6] && p)
			y = p - year_codes;
		else if (raw[6] >= '1' && raw[6] <= '9')
			y = raw[6] - '1' + 16;
		else if (raw[6] >= 'A' && raw[6] <= 'E')
			y = raw[6] - 'A' + 25;
		else
			y = 0;
		y += 2025;
		year[0] = '0' + (y / 10) % 10;
		year[1] = '0' + y % 10;

		p = strchr(day_codes, raw[8]);
		if (raw[8] && p) {
			d = p - day_codes + 10;
			day[0] = '0' + d / 10;
			day[1] = '0' + d % 10;
		} else if (raw[8] >= '1' && raw[8] <= '9') {
			day[0] = '0';
			day[1] = raw[8];
		} else if (raw[8] == '0') {
			day[0] = '3';
			day[1] = '1';
		} else {
			day[0] = day[1] = '0';
		}
	} else {
		year[0] = '2';
		year[1] = raw[6];
		day[0] = raw[8];
		day[1] = raw[9];
	}

	if (raw[7] >= '1' && raw[7] <= '9') {
		month[0] = '0';
		month[1] = raw[7];
	} else if (raw[7] >= 'A' && raw[7] <= 'C') {
		month[0] = '1';
		month[1] = raw[7] - 'A' + '0';
	} else {
		month[0] = month[1] = '0';
	}

	snprintf(buf, QBG_DATE_LEN + 1, "20%c%c%c%c%c%c", year[0], year[1],
		 month[0], month[1], day[0], day[1]);
	mca_log_info("manufacturing date: %s\n", buf);

	return 0;
}

/* the battery chip keeps the first usage date as YYMMDD */
static int qbg_get_first_usage_date(void *data, u8 *buf)
{
	u8 raw[16] = { 0 };
	int ret, i;

	ret = onewire_get_first_usage_date(raw);
	if (ret) {
		strscpy(buf, "99999999", QBG_DATE_LEN + 1);
		mca_log_err("failed to read the first usage date: %d\n", ret);
		return ret;
	}

	for (i = 0; i < 6; i++) {
		if (raw[i] && raw[i] != '0')
			break;
	}
	if (i == 6) {
		strscpy(buf, "00000000", QBG_DATE_LEN + 1);
		return 0;
	}

	for (i = 0; i < 6; i++) {
		if (!isdigit(raw[i])) {
			mca_log_err("invalid first usage date character %#x\n", raw[i]);
			return -EINVAL;
		}
	}

	snprintf(buf, QBG_DATE_LEN + 1, "20%.6s", raw);
	return 0;
}

static int qbg_write_first_usage_date(const u8 *date)
{
	u8 val[16] = { 0 }, check[16] = { 0 };
	int ret;

	memcpy(val, date, 6);
	ret = onewire_set_first_usage_date(val);
	if (ret)
		return ret;

	msleep(100);
	ret = onewire_get_first_usage_date(check);
	if (ret)
		return ret;

	return memcmp(val, check, 6) ? -EIO : 0;
}

/* takes YYYYMMDD, possibly with separators */
static int qbg_set_first_usage_date(void *data, const char *date)
{
	u8 digits[QBG_DATE_LEN + 1] = { 0 };
	int len = 0, retry, ret = -EINVAL;

	if (!date)
		return -EINVAL;

	for (; *date && len < QBG_DATE_LEN + 1; date++) {
		if (isspace(*date) || *date == '-' || *date == '/')
			continue;
		if (!isdigit(*date)) {
			mca_log_err("invalid first usage date\n");
			return -EINVAL;
		}
		digits[len++] = *date;
	}
	if (len != QBG_DATE_LEN) {
		mca_log_err("invalid first usage date %s\n", digits);
		return -EINVAL;
	}

	for (retry = 0; retry < 3; retry++) {
		ret = qbg_write_first_usage_date(digits + 2);
		if (!ret) {
			mca_log_info("first usage date set to %s\n", digits);
			return 0;
		}
		msleep(100);
	}

	/* do not leave a half written date behind */
	mca_log_err("failed to set the first usage date: %d\n", ret);
	memset(digits, 0, sizeof(digits));
	for (retry = 0; retry < 3; retry++) {
		if (!qbg_write_first_usage_date(digits))
			break;
	}

	return ret;
}

static int qbg_send_chg_data(void *data, struct mca_qbg_chg_data *chg)
{
	struct qbg_info *info = data;
	struct qbg_full_table *table;
	int ibatt_full, vbatt_full, i;

	if (!chg)
		return -EINVAL;

	if (!chg->charging) {
		ibatt_full = chg->term_curr + info->buck_ibatt_full_delta;
		vbatt_full = chg->term_volt - info->buck_vbatt_full_delta;
	} else {
		ibatt_full = chg->term_curr + info->quick_ibatt_full_delta;
		vbatt_full = chg->term_volt - info->quick_vbatt_full_delta;

		if (info->donghai)
			table = &info->full_table_donghai;
		else if (info->pack_vendor == QBG_PACK_VENDOR_SWD)
			table = &info->full_table_swd;
		else
			table = &info->full_table_nvt;

		for (i = 0; i < table->count; i++) {
			if (table->params[i].iterm == chg->term_curr) {
				ibatt_full = table->params[i].ibatt_full;
				break;
			}
		}
	}

	qbg_write(info, QBG_PROP_CHG_STATUS, QBG_DATA_CHG_STATUS, chg->chg_status);
	qbg_write(info, QBG_PROP_CHG_FCC, QBG_DATA_CHG_FCC, chg->ibat_max);
	qbg_write(info, QBG_PROP_IBATT_FULL, QBG_DATA_IBATT_FULL, ibatt_full);
	qbg_write(info, QBG_PROP_VBATT_FULL, QBG_DATA_VBATT_FULL, vbatt_full);

	mca_log_debug("quick: %d, ibatt_full: %d, vbatt_full: %d, status: %d, fcc: %d\n",
		      chg->charging, ibatt_full, vbatt_full, chg->chg_status,
		      chg->ibat_max);

	return 0;
}

static struct fuelguage_ic_ops qbg_ops = {
	.fg_ic_probe_ok = qbg_get_probe_ok,
	.fg_ic_get_batt_info = qbg_get_batt_info,
	.fg_ic_get_rsoc = qbg_get_rsoc,
	.fg_ic_get_curr = qbg_get_curr,
	.fg_ic_set_authentic = qbg_set_authentic,
	.fg_ic_get_authentic = qbg_get_authentic,
	.fg_ic_get_error_state = qbg_get_error_state,
	.fg_ic_get_volt = qbg_get_volt,
	.fg_ic_set_temp = qbg_set_temp,
	.fg_ic_get_temp = qbg_get_temp,
	.fg_ic_get_rm = qbg_get_rm,
	.fg_ic_get_fastcharge = qbg_get_fastcharge,
	.fg_ic_set_fastcharge = qbg_set_fastcharge,
	.fg_ic_get_cyclecount = qbg_get_cyclecount,
	.fg_ic_get_fcc = qbg_get_fcc,
	.fg_ic_get_full_design = qbg_get_full_design,
	.fg_ic_get_soh = qbg_get_soh,
	.fg_ic_get_temp_max = qbg_get_temp_max,
	.fg_ic_get_batt_cell_info = qbg_get_batt_cell_info,
	.fg_ic_get_cutoff_voltage = qbg_get_cutoff_voltage,
	.fg_ic_set_cutoff_voltage = qbg_set_cutoff_voltage,
	.fg_ic_get_adapt_power = qbg_get_adapt_power,
	.fg_ic_get_raw_soc = qbg_get_raw_soc,
	.fg_ic_get_device_name = qbg_get_device_name,
	.fg_ic_get_temp_min = qbg_get_temp_min,
	.fg_ic_set_force_report_full = qbg_set_force_report_full,
	.fg_ic_get_ui_soh = qbg_get_ui_soh,
	.fg_ic_get_pack_vendor = qbg_get_pack_vendor,
	.fg_ic_get_first_usage_date = qbg_get_first_usage_date,
	.fg_ic_get_manufacturing_date = qbg_get_manufacturing_date,
	.fg_ic_set_first_usage_date = qbg_set_first_usage_date,
	.fg_ic_qbg_send_chg_data = qbg_send_chg_data,
};

static void qbg_glink_down(void *priv)
{
	mca_log_info("glink down\n");
}

static void qbg_glink_up(void *priv)
{
	mca_log_info("glink up\n");
}

static void qbg_notify(u32 event, void *data, u32 len, void *priv)
{
	struct qbg_info *info = priv;
	int id;

	if (!info || len < sizeof(int))
		return;

	pm_wakeup_dev_event(info->dev, 1000, true);

	if (event >= ARRAY_SIZE(qbg_notify_data) || qbg_notify_data[event] < 0)
		return;

	id = qbg_notify_data[event];
	info->data[id] = *(int *)data;
	mca_log_debug("event %u: %d\n", event, info->data[id]);
}

static struct mca_adsp_glink_ops qbg_glink_ops = {
	.glink_state_down = qbg_glink_down,
	.glink_state_up = qbg_glink_up,
	.notification = qbg_notify,
};

static ssize_t qbg_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buf);
static ssize_t qbg_sysfs_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count);

static struct mca_sysfs_attr_info qbg_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_CHIP_OK, chip_ok),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_VOL, vbatt),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_CURRENT, ibatt),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_RSOC, rsoc),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_TEMP, temp),
	mca_sysfs_attr_rw(qbg_sysfs, 0664, FG_IC_PROP_CYCLE, cyclecount),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_RM, rm),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_FCC, charger_full),
	mca_sysfs_attr_rw(qbg_sysfs, 0664, FG_IC_PROP_SOH, soh),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_FAST_CHARGE, fast_charge),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_RESISTANCE_ID, resistance_id),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_DESIGN_CAPACITY, design_capacity),
	mca_sysfs_attr_rw(qbg_sysfs, 0664, FG_IC_PROP_UI_SOH, ui_soh),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_BATT_SN, batt_sn),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_SOH_NEW, soh_new),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_CUTOFF_VOL, cutoff_vol),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_TEMP_MAX, temp_max),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_PACK_VENDOR_ID, pack_vendor),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_DEVICE_NAME, device_name),
	mca_sysfs_attr_ro(qbg_sysfs, 0440, FG_IC_PROP_MANUFACTURING_DATE, manufacturing_date),
	mca_sysfs_attr_rw(qbg_sysfs, 0664, FG_IC_PROP_FIRST_USAGE_DATE, first_usage_date),
};

#define QBG_SYSFS_ATTRS_SIZE ARRAY_SIZE(qbg_sysfs_field_tbl)

static struct attribute *qbg_sysfs_attrs[QBG_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group qbg_sysfs_attr_group = {
	.attrs = qbg_sysfs_attrs,
};

static ssize_t qbg_sysfs_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct qbg_info *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	u8 str[ONEWIRE_PAGE_SIZE + 1] = { 0 };
	const char *name;
	bool ok = false;
	int val = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name, qbg_sysfs_field_tbl,
					  QBG_SYSFS_ATTRS_SIZE);
	if (!attr_info || !info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case FG_IC_PROP_CHIP_OK:
		onewire_check_chip_ok(&ok);
		val = ok;
		break;
	case FG_IC_PROP_VOL:
		qbg_get_volt(info, &val);
		break;
	case FG_IC_PROP_CURRENT:
		qbg_get_curr(info, &val);
		break;
	case FG_IC_PROP_RSOC:
		qbg_get_rsoc(info, &val);
		break;
	case FG_IC_PROP_TEMP:
		qbg_get_temp(info, &val);
		break;
	case FG_IC_PROP_CYCLE:
		qbg_get_cyclecount(info, &val);
		break;
	case FG_IC_PROP_RM:
		qbg_get_rm(info, &val);
		break;
	case FG_IC_PROP_FCC:
		qbg_get_fcc(info, &val);
		break;
	case FG_IC_PROP_SOH:
		qbg_get_soh(info, &val);
		break;
	case FG_IC_PROP_FAST_CHARGE:
		val = info->fastcharge;
		break;
	case FG_IC_PROP_RESISTANCE_ID:
		val = QBG_RESISTANCE_ID;
		break;
	case FG_IC_PROP_DESIGN_CAPACITY:
		qbg_get_full_design(info, &val);
		break;
	case FG_IC_PROP_UI_SOH:
		if (onewire_get_batt_ui_soh(str))
			return -EIO;
		return scnprintf(buf, PAGE_SIZE, "%u %u %u %u %u %u %u %u %u %u %u\n",
				 str[0], str[1], str[2], str[3], str[4], str[5],
				 str[6], str[7], str[8], str[9], str[10]);
	case FG_IC_PROP_BATT_SN:
		if (onewire_get_batt_sn(str))
			return -EIO;
		return scnprintf(buf, PAGE_SIZE, "%s\n", str);
	case FG_IC_PROP_SOH_NEW:
		val = info->ui_soh;
		break;
	case FG_IC_PROP_CUTOFF_VOL:
		qbg_get_cutoff_voltage(info, &val);
		break;
	case FG_IC_PROP_TEMP_MAX:
		val = QBG_TEMP_MAX;
		break;
	case FG_IC_PROP_PACK_VENDOR_ID:
		val = info->pack_vendor;
		break;
	case FG_IC_PROP_DEVICE_NAME:
		qbg_get_device_name(info, &name);
		return scnprintf(buf, PAGE_SIZE, "%s\n", name);
	case FG_IC_PROP_MANUFACTURING_DATE:
		if (qbg_get_manufacturing_date(info, str))
			return -EIO;
		return scnprintf(buf, PAGE_SIZE, "%s\n", str);
	case FG_IC_PROP_FIRST_USAGE_DATE:
		qbg_get_first_usage_date(info, str);
		return scnprintf(buf, PAGE_SIZE, "%s\n", str);
	default:
		return -EINVAL;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static int qbg_set_ui_soh(struct qbg_info *info, const char *buf)
{
	u8 data[ONEWIRE_UI_SOH_LEN] = { 0 };
	char *str, *p, *tok;
	unsigned int val;
	int i = 0, ret;

	str = kstrdup(buf, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	p = strim(str);
	while ((tok = strsep(&p, " ")) && i < ONEWIRE_UI_SOH_LEN) {
		if (!*tok)
			continue;
		ret = kstrtouint(tok, 0, &val);
		if (ret) {
			kfree(str);
			return ret;
		}
		data[i++] = val;
	}
	kfree(str);

	info->ui_soh = data[0];

	ret = onewire_set_batt_ui_soh(data);
	if (ret)
		mca_log_err("failed to set the UI state of health: %d\n", ret);

	return ret;
}

static ssize_t qbg_sysfs_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct qbg_info *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int val, ret = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name, qbg_sysfs_field_tbl,
					  QBG_SYSFS_ATTRS_SIZE);
	if (!attr_info || !info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case FG_IC_PROP_CYCLE:
		ret = kstrtoint(buf, 10, &val);
		if (!ret)
			info->fake_cycle_count = val;
		break;
	case FG_IC_PROP_SOH:
		ret = kstrtoint(buf, 10, &val);
		if (!ret)
			info->fake_soh = val;
		break;
	case FG_IC_PROP_UI_SOH:
		ret = qbg_set_ui_soh(info, buf);
		break;
	case FG_IC_PROP_FIRST_USAGE_DATE:
		ret = qbg_set_first_usage_date(info, buf);
		break;
	default:
		return -EINVAL;
	}

	return ret ? ret : count;
}

static int qbg_parse_full_table(struct qbg_info *info, struct device_node *np,
				const char *prop, struct qbg_full_table *table)
{
	int len;

	table->count = 0;
	if (!of_find_property(np, prop, &len))
		return 0;

	if (len % sizeof(struct qbg_full_param)) {
		mca_log_err("%s has an odd number of entries\n", prop);
		return -EINVAL;
	}

	table->params = devm_kzalloc(info->dev, len, GFP_KERNEL);
	if (!table->params)
		return -ENOMEM;

	if (of_property_read_u32_array(np, prop, (u32 *)table->params,
				       len / sizeof(u32)))
		return -EINVAL;

	table->count = len / sizeof(struct qbg_full_param);
	return 0;
}

static void qbg_parse_dt(struct qbg_info *info)
{
	struct device_node *np = info->dev->of_node;

	mca_parse_dts_u32(np, "buck-ibatt-full-delta", &info->buck_ibatt_full_delta, 200);
	mca_parse_dts_u32(np, "buck-vbatt-full-delta", &info->buck_vbatt_full_delta, 15);
	mca_parse_dts_u32(np, "quick-ibatt-full-delta", &info->quick_ibatt_full_delta, 800);
	mca_parse_dts_u32(np, "quick-vbatt-full-delta", &info->quick_vbatt_full_delta, 5);

	qbg_parse_full_table(info, np, "quick-ibatt-full-table-swd", &info->full_table_swd);
	qbg_parse_full_table(info, np, "quick-ibatt-full-table-nvt", &info->full_table_nvt);
	qbg_parse_full_table(info, np, "quick-ibatt-full-table-donghai",
			     &info->full_table_donghai);
}

static int qbg_probe(struct platform_device *pdev)
{
	static int probe_retries;
	const struct mca_hwid *hwid;
	struct qbg_info *info;
	u32 cell_sn = 0;
	int val, ret, i;

	/* the ADSP side of the gauge comes up after the kernel */
	ret = mca_adsp_glink_qbg_read_prop(qbg_read_prop_id[QBG_DATA_RSOC],
					   &val, sizeof(val));
	if (ret && probe_retries < QBG_PROBE_RETRY_MAX) {
		probe_retries++;
		return -EPROBE_DEFER;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	mutex_init(&info->data_lock);
	info->fake_temp = QBG_FAKE_TEMP_NONE;
	info->fake_soh = QBG_FAKE_SOH_NONE;
	info->adapt_power = QBG_ADAPT_POWER;
	info->ui_soh = 100;
	for (i = 0; i < QBG_DATA_MAX; i++)
		info->data[i] = QBG_DATA_INVALID;

	/* donghai packs a different cell */
	hwid = mca_get_hwid_info();
	info->donghai = hwid && hwid->product_name &&
			!strcmp(hwid->product_name, "donghai");
	strscpy(info->device_name, info->donghai ? "QBGq82" : "QBGdev",
		sizeof(info->device_name));

	get_smem_battery_cell_sn(&cell_sn);
	if (cell_sn == QBG_CELL_SN_SWD) {
		info->cell_name = "swd";
		info->pack_vendor = QBG_PACK_VENDOR_SWD;
	} else {
		info->cell_name = "nvt";
		info->pack_vendor = QBG_PACK_VENDOR_NVT;
	}
	mca_log_info("cell sn: %u, cell: %s\n", cell_sn, info->cell_name);

	qbg_parse_dt(info);
	platform_set_drvdata(pdev, info);
	device_init_wakeup(info->dev, true);

	ret = platform_fg_ic_ops_register(FG_IC_MASTER, info, &qbg_ops);
	if (ret) {
		mca_log_err("failed to register the gauge ops: %d\n", ret);
		return ret;
	}

	ret = mca_adsp_glink_qbg_resister_ops(&qbg_glink_ops, info);
	if (ret) {
		mca_log_err("failed to register the glink ops: %d\n", ret);
		return ret;
	}

	mca_sysfs_init_attrs(qbg_sysfs_attrs, qbg_sysfs_field_tbl, QBG_SYSFS_ATTRS_SIZE);
	info->sysfs_dev = mca_sysfs_create_group("xm_power", "fg_master",
						 &qbg_sysfs_attr_group);
	if (info->sysfs_dev)
		dev_set_drvdata(info->sysfs_dev, info);

	return 0;
}

static int qbg_remove(struct platform_device *pdev)
{
	struct qbg_info *info = platform_get_drvdata(pdev);

	if (info->sysfs_dev)
		mca_sysfs_remove_group("xm_power", info->sysfs_dev,
				       &qbg_sysfs_attr_group);

	return 0;
}

static const struct of_device_id qbg_of_match[] = {
	{ .compatible = "mca,qcom-battery-gauge" },
	{ }
};
MODULE_DEVICE_TABLE(of, qbg_of_match);

static struct platform_driver qbg_driver = {
	.driver = {
		.name = "qcom_battery_gauge",
		.of_match_table = qbg_of_match,
	},
	.probe = qbg_probe,
	.remove = qbg_remove,
};
module_platform_driver(qbg_driver);

MODULE_DESCRIPTION("MCA Qualcomm battery gauge driver");
MODULE_LICENSE("GPL");
