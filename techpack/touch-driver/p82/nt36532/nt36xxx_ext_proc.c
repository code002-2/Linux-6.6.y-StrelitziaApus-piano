/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
 *
 * $Revision: 102158 $
 * $Date: 2022-07-07 11:10:06 +0800 (週四, 07 七月 2022) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */


#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "nt36xxx.h"

#if NVT_TOUCH_EXT_PROC
#define NVT_FW_VERSION "nvt_fw_version"
#define NVT_BASELINE "nvt_baseline"
#define NVT_RAW "nvt_raw"
#define NVT_DIFF "nvt_diff"
#define NVT_PEN_SWITCH "nvt_pen_switch"
#define NVT_ACTIVE_PEN_STATIONARY "nvt_active_pen_stationary"
#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
#define NVT_XIAOMI_LOCKDOWN_INFO "tp_lockdown_info"
#endif	/* #ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */


//thp start 6.9
#if TOUCH_THP_SUPPORT
#define NVT_EVENTBUF_DEBUG "nvt_eventbuf_debug"
#define XM_HTC_OP_MODE "xm_htc_op_mode"
#define XM_HTC_IDLE_WAKE_TH "xm_htc_idle_wake_th"
#define XM_HTC_STYLUS_ENABLE "xm_htc_stylus_enable"
#define XM_HTC_STYLUS_ONLY "xm_htc_stylus_only"
#define XM_HTC_RAW_DATA_TYPE "xm_htc_raw_data_type"
#define XM_HTC_GESTURE_SWITCH "xm_htc_gesture_switch"
#define XM_HTC_SW_RESET "xm_htc_sw_reset"
#define XM_HTC_FH_ENABLE "xm_htc_fh_enable" /* frequency hopping enable */
#define XM_HTC_SCAN_FREQ_NO "xm_htc_scan_freq_no"
#define XM_HTC_SCAN_FREQ "xm_htc_scan_freq"
#define XM_HTC_HAND_SCAN_RATE "xm_htc_hand_scan_rate"
#define XM_HTC_PEN_SCAN_RATE "xm_htc_pen_scan_rate"
#define XM_HTC_CALIBRATION "xm_htc_calibration"
#define XM_HTC_INT_STATE "xm_htc_int_state"
#define XM_HTC_SINGLE_STEP "xm_htc_single_step"
#define XM_HTC_GAME_MODE "xm_htc_game_mode"
#define XM_HTC_CHARGER "xm_htc_charger"
#define XM_HTC_GESTURE_DBCLK "xm_htc_gesture_dbclk"
#define XM_HTC_FLG_BUF "xm_htc_flg_buf"
#define XM_HTC_IC_LOG_LEVEL "xm_htc_ic_log_level"
#define XM_HTC_FILTER_LEVEL "xm_htc_filter_level"
#define XM_HTC_REPORT_COORDINATE "xm_htc_report_coordinate"
#define XM_HTC_IDLE_BASELINE_UPDATE "xm_htc_idle_baseline_update"
#define XM_HTC_CLICK_GESTURE_ENABLE "xm_htc_click_gesture_enable"
#define XM_HTC_IDLE_HIGH_BASE_EN "xm_htc_idle_high_base_en"
#define XM_HTC_STYLUS_PRESSURE "xm_htc_stylus_pressure"
#endif /* #if TOUCH_THP_SUPPORT */
//thp end 6.9

#define NORMAL_MODE 0x00
#define TEST_MODE_2 0x22
#define HANDSHAKING_HOST_READY 0xBB

#define XDATA_SECTOR_SIZE   256

static uint8_t xdata_tmp[8192] = {0};
static int32_t xdata[4096] = {0};

static struct proc_dir_entry *NVT_proc_fw_version_entry;
static struct proc_dir_entry *NVT_proc_baseline_entry;
static struct proc_dir_entry *NVT_proc_raw_entry;
static struct proc_dir_entry *NVT_proc_diff_entry;
static struct proc_dir_entry *NVT_proc_pen_switch_entry;
static struct proc_dir_entry *NVT_proc_active_pen_stationary_entry;
#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
extern ssize_t mi_dsi_panel_lockdown_info_read(unsigned char *plockdowninfo);
#endif	/* #ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */

//thp start 6.9
#if TOUCH_THP_SUPPORT
static struct proc_dir_entry *NVT_proc_eventbuf_debug_entry;
static struct proc_dir_entry *XM_HTC_proc_op_mode_entry;
static struct proc_dir_entry *XM_HTC_proc_idle_wake_th_entry;
static struct proc_dir_entry *XM_HTC_proc_stylus_enable_entry;
static struct proc_dir_entry *XM_HTC_proc_stylus_only_entry;
static struct proc_dir_entry *XM_HTC_proc_raw_data_type_entry;
static struct proc_dir_entry *XM_HTC_proc_gesture_switch_entry;
static struct proc_dir_entry *XM_HTC_proc_sw_reset_entry;
static struct proc_dir_entry *XM_HTC_proc_fh_enable_entry;
static struct proc_dir_entry *XM_HTC_proc_scan_freq_no_entry;
static struct proc_dir_entry *XM_HTC_proc_scan_freq_entry;
static struct proc_dir_entry *XM_HTC_proc_hand_scan_rate_entry;
static struct proc_dir_entry *XM_HTC_proc_pen_scan_rate_entry;
static struct proc_dir_entry *XM_HTC_proc_calibration_entry;
static struct proc_dir_entry *XM_HTC_proc_int_state_entry;
static struct proc_dir_entry *XM_HTC_proc_single_step_entry;
static struct proc_dir_entry *XM_HTC_proc_game_mode_entry;
static struct proc_dir_entry *XM_HTC_proc_charger_entry;
static struct proc_dir_entry *XM_HTC_proc_gesture_dbclk_entry;
static struct proc_dir_entry *XM_HTC_proc_flg_buf_entry;
static struct proc_dir_entry *XM_HTC_proc_ic_log_level_entry;
static struct proc_dir_entry *XM_HTC_proc_filter_level_entry;
static struct proc_dir_entry *XM_HTC_proc_report_coordinate_entry;
static struct proc_dir_entry *XM_HTC_proc_idle_baseline_update_entry;
static struct proc_dir_entry *XM_HTC_proc_click_gesture_enable_entry;
static struct proc_dir_entry *XM_HTC_proc_idle_high_base_en_entry;
static struct proc_dir_entry *XM_HTC_proc_stylus_pressure_entry;
#endif /* #if TOUCH_THP_SUPPORT */
//thp end 6.9

/*******************************************************
Description:
	Novatek touchscreen change mode function.

return:
	n.a.
*******************************************************/
void nvt_change_mode(uint8_t mode)
{
	uint8_t buf[8] = {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	//---set mode---
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = mode;
	CTP_SPI_WRITE(ts->client, buf, 2);

	if (mode == NORMAL_MODE) {
		usleep_range(20000, 20000);
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = HANDSHAKING_HOST_READY;
		CTP_SPI_WRITE(ts->client, buf, 2);
		usleep_range(20000, 20000);
	}
}

/*******************************************************
Description:
	Novatek touchscreen get firmware pipe function.

return:
	Executive outcomes. 0---pipe 0. 1---pipe 1.
*******************************************************/
uint8_t nvt_get_fw_pipe(void)
{
	uint8_t buf[8]= {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

	//---read fw status---
	buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
	buf[1] = 0x00;
	CTP_SPI_READ(ts->client, buf, 2);

	//NVT_LOG("FW pipe=%d, buf[1]=0x%02X\n", (buf[1]&0x01), buf[1]);

	return (buf[1] & 0x01);
}

/*******************************************************
Description:
	Novatek touchscreen read meta data function.

return:
	n.a.
*******************************************************/
void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr)
{
	int32_t transfer_len = 0;
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[BUS_TRANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;

	if (BUS_TRANSFER_LENGTH <= XDATA_SECTOR_SIZE)
		transfer_len = BUS_TRANSFER_LENGTH;
	else
		transfer_len = XDATA_SECTOR_SIZE;

	//---set xdata sector address & length---
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = ts->x_num * ts->y_num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	//printk("head_addr=0x%05X, dummy_len=0x%05X, data_len=0x%05X, residual_len=0x%05X\n", head_addr, dummy_len, data_len, residual_len);

	//read xdata : step 1
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		//---change xdata index---
		nvt_set_page(head_addr + XDATA_SECTOR_SIZE * i);

		//---read xdata by transfer_len
		for (j = 0; j < (XDATA_SECTOR_SIZE / transfer_len); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));
	}

	//read xdata : step2
	if (residual_len != 0) {
		//---change xdata index---
		nvt_set_page(xdata_addr + data_len - residual_len);

		//---read xdata by transfer_len
		for (j = 0; j < (residual_len / transfer_len + 1); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));
	}

	//---remove dummy data and 2bytes-to-1data---
	for (i = 0; i < (data_len / 2); i++) {
		xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
	}

#if TOUCH_KEY_NUM > 0
	//read button xdata : step3
	//---change xdata index---
	nvt_set_page(xdata_btn_addr);

	//---read data---
	buf[0] = (xdata_btn_addr & 0xFF);
	CTP_SPI_READ(ts->client, buf, (TOUCH_KEY_NUM * 2 + 1));

	//---2bytes-to-1data---
	for (i = 0; i < TOUCH_KEY_NUM; i++) {
		xdata[ts->x_num * ts->y_num + i] = (int16_t)(buf[1 + i * 2] + 256 * buf[1 + i * 2 + 1]);
	}
#endif

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
    Novatek touchscreen get meta data function.

return:
    n.a.
*******************************************************/
void nvt_get_mdata(int32_t *buf, uint8_t *m_x_num, uint8_t *m_y_num)
{
    *m_x_num = ts->x_num;
    *m_y_num = ts->y_num;
    memcpy(buf, xdata, ((ts->x_num * ts->y_num + TOUCH_KEY_NUM) * sizeof(int32_t)));
}

/*******************************************************
Description:
	Novatek touchscreen read and get number of meta data function.

return:
	n.a.
*******************************************************/
void nvt_read_get_num_mdata(uint32_t xdata_addr, int32_t *buffer, uint32_t num)
{
	int32_t transfer_len = 0;
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[BUS_TRANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;

	if (BUS_TRANSFER_LENGTH <= XDATA_SECTOR_SIZE)
		transfer_len = BUS_TRANSFER_LENGTH;
	else
		transfer_len = XDATA_SECTOR_SIZE;

	//---set xdata sector address & length---
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	//printk("head_addr=0x%05X, dummy_len=0x%05X, data_len=0x%05X, residual_len=0x%05X\n", head_addr, dummy_len, data_len, residual_len);

	//read xdata : step 1
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		//---change xdata index---
		nvt_set_page(head_addr + XDATA_SECTOR_SIZE * i);

		//---read xdata by transfer_len
		for (j = 0; j < (XDATA_SECTOR_SIZE / transfer_len); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i));
	}

	//read xdata : step2
	if (residual_len != 0) {
		//---change xdata index---
		nvt_set_page(xdata_addr + data_len - residual_len);

		//---read xdata by transfer_len
		for (j = 0; j < (residual_len / transfer_len + 1); j++) {
			//---read data---
			buf[0] = transfer_len * j;
			CTP_SPI_READ(ts->client, buf, transfer_len + 1);

			//---copy buf to xdata_tmp---
			for (k = 0; k < transfer_len; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + transfer_len * j + k] = buf[k + 1];
				//printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + transfer_len*j + k));
			}
		}
		//printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len));
	}

	//---remove dummy data and 2bytes-to-1data---
	for (i = 0; i < (data_len / 2); i++) {
		buffer[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
	}

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
	Novatek touchscreen firmware version show function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t c_fw_version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "fw_ver=%d, x_num=%d, y_num=%d, button_num=%d\n", ts->fw_ver, ts->x_num, ts->y_num, ts->max_button_num);
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print show
	function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t c_show(struct seq_file *m, void *v)
{
	int32_t i = 0;
	int32_t j = 0;

	for (i = 0; i < ts->y_num; i++) {
		for (j = 0; j < ts->x_num; j++) {
			seq_printf(m, "%6d,", xdata[i * ts->x_num + j]);
		}
		seq_puts(m, "\n");
	}

#if TOUCH_KEY_NUM > 0
	for (i = 0; i < TOUCH_KEY_NUM; i++) {
		seq_printf(m, "%6d,", xdata[ts->x_num * ts->y_num + i]);
	}
	seq_puts(m, "\n");
#endif

	seq_printf(m, "\n\n");
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print start
	function.

return:
	Executive outcomes. 1---call next function.
	NULL---not call next function and sequence loop
	stop.
*******************************************************/
static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print next
	function.

return:
	Executive outcomes. NULL---no next and call sequence
	stop function.
*******************************************************/
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print stop
	function.

return:
	n.a.
*******************************************************/
static void c_stop(struct seq_file *m, void *v)
{
	return;
}

const struct seq_operations nvt_fw_version_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_fw_version_show
};

const struct seq_operations nvt_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show
};

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_fw_version open
	function.

return:
	n.a.
*******************************************************/
static int32_t nvt_fw_version_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	return seq_open(file, &nvt_fw_version_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_fw_version_fops = {
	.proc_open = nvt_fw_version_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_fw_version_fops = {
	.owner = THIS_MODULE,
	.open = nvt_fw_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_baseline open function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_baseline_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_read_mdata(ts->mmap->BASELINE_ADDR, ts->mmap->BASELINE_BTN_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_baseline_fops = {
	.proc_open = nvt_baseline_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_baseline_fops = {
	.owner = THIS_MODULE,
	.open = nvt_baseline_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_raw open function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_raw_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->RAW_PIPE0_ADDR, ts->mmap->RAW_BTN_PIPE0_ADDR);
	else
		nvt_read_mdata(ts->mmap->RAW_PIPE1_ADDR, ts->mmap->RAW_BTN_PIPE1_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_raw_fops = {
	.proc_open = nvt_raw_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_raw_fops = {
	.owner = THIS_MODULE,
	.open = nvt_raw_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_diff open function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
static int32_t nvt_diff_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->DIFF_PIPE0_ADDR, ts->mmap->DIFF_BTN_PIPE0_ADDR);
	else
		nvt_read_mdata(ts->mmap->DIFF_PIPE1_ADDR, ts->mmap->DIFF_BTN_PIPE1_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_diff_fops = {
	.proc_open = nvt_diff_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_diff_fops = {
	.owner = THIS_MODULE,
	.open = nvt_diff_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif


int32_t nvt_set_pen_switch(uint8_t pen_switch)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;
	int32_t i = 0;
	int32_t retry = 10;

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	NVT_LOG("++\n");
	NVT_LOG("pen_switch: %d\n", pen_switch);

	//---set xdata index to EVENT BUF ADDR---
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("Set event buffer index fail!\n");
		goto out;
	}

	for (i = 0; i < retry; i++) {
		if (buf[1] != 0x7B) {
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = 0x7B;
			buf[2] = pen_switch;
			ret = CTP_SPI_WRITE(ts->client, buf, 3);
			if (ret < 0) {
				NVT_ERR("Write command fail!\n");
				goto out;
			}
		}

		usleep_range(10000, 11000);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);
		if (buf[1] == 0x00)
			break;
	}

	if (unlikely(i >= retry)) {
		NVT_ERR("send cmd failed, buf[1] = 0x%02X\n", buf[1]);
		ret = -1;
	} else {
		NVT_LOG("send cmd success, tried %d times\n", i);
		ret = 0;
		ts->pen_switch = pen_switch;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t nvt_pen_switch_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	uint8_t pen_switch;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value!, count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value!, ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	pen_switch = (uint8_t)tmp;
	NVT_LOG("pen_switch = %d\n", pen_switch);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_set_pen_switch(pen_switch);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_pen_switch_fops = {
	.proc_write = nvt_pen_switch_proc_write,
};
#else
static const struct file_operations nvt_pen_switch_fops = {
	.owner = THIS_MODULE,
	.write = nvt_pen_switch_proc_write,
};
#endif

int32_t nvt_set_active_pen_stationary(uint8_t pen_stationary)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;
	int32_t i = 0;
	int32_t retry = 10;

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	NVT_LOG("++\n");
	NVT_LOG("pen_stationary: %d\n", pen_stationary);

	//---set xdata index to EVENT BUF ADDR---
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("Set event buffer index fail!\n");
		goto out;
	}

	for (i = 0; i < retry; i++) {
		if (buf[1] != 0x7E) {
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = 0x7E;
			buf[2] = pen_stationary;
			ret = CTP_SPI_WRITE(ts->client, buf, 3);
			if (ret < 0) {
				NVT_ERR("Write command fail!\n");
				goto out;
			}
		}

		usleep_range(10000, 11000);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);
		if (buf[1] == 0x00)
			break;
	}

	if (unlikely(i >= retry)) {
		NVT_ERR("send cmd failed, buf[1] = 0x%02X\n", buf[1]);
		ret = -1;
	} else {
		NVT_LOG("send cmd success, tried %d times\n", i);
		ret = 0;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t nvt_active_pen_stationary_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	uint8_t pen_stationary;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value!, count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value!, ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	pen_stationary = (uint8_t)tmp;
	NVT_LOG("pen_stationary = %d\n", pen_stationary);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_set_active_pen_stationary(pen_stationary);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_active_pen_stationary_fops = {
	.proc_write = nvt_active_pen_stationary_proc_write,
};
#else
static const struct file_operations nvt_active_pen_stationary_fops = {
	.owner = THIS_MODULE,
	.write = nvt_active_pen_stationary_proc_write,
};
#endif

//thp start 6.9
#if TOUCH_THP_SUPPORT
static ssize_t nvt_eventbuf_debug_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	int32_t ret = 0;

	if (copy_to_user(buff, ts->eventbuf_debug, EVENTBUF_DEBUG_LEN)) {
		NVT_ERR("copy to user error\n");
		ret = -EFAULT;
		goto out;
	}
	ret = 0;

out:
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_eventbuf_debug_fops = {
	.proc_read = nvt_eventbuf_debug_read,
};
#else
static const struct file_operations nvt_eventbuf_debug_fops = {
	.owner = THIS_MODULE,
	.read = nvt_eventbuf_debug_read,
};
#endif

int32_t nvt_set_extend_custom_cmd(uint8_t sub_cmd, int16_t value)
{
	int32_t ret = 0;
	uint8_t buf[8] = {0};
	int32_t i = 0;
	int32_t retry = 10;

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	//---set xdata index to EVENT BUF ADDR---
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("--, nvt_set_page fail! ret=%d\n", ret);
		goto out;
	}

	for (i = 0; i < retry; i++) {
		if (buf[1] != 0xBF) {
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = 0xBF;
			buf[2] = sub_cmd;
			buf[3] = 0x00; // W
			buf[4] = value & 0xFF;
			buf[5] = (value >> 8) & 0xFF;
			ret = CTP_SPI_WRITE(ts->client, buf, 6);
			if (ret < 0) {
				NVT_ERR("--, CTP_SPI_WRITE fail! ret=%d\n", ret);
				goto out;
			}
		}

		usleep_range(10000, 11000);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		ret = CTP_SPI_READ(ts->client, buf, 2);
		if (ret < 0) {
			NVT_ERR("--, CTP_SPI_READ fail! ret=%d\n", ret);
			goto out;
		}
		if (buf[1] == 0x00)
			break;
	}

	if (unlikely(i >= retry)) {
		NVT_ERR("--, send cmd failed, buf[1] = 0x%02X\n", buf[1]);
		ret = -1;
		nvt_read_fw_history_all();
		nvt_read_print_fw_flow_debug_message();
	} else {
		ret = 0;
	}

out:
	return ret;
}

int32_t nvt_get_extend_custom_cmd(uint8_t sub_cmd, int16_t *value)
{
	int32_t ret = 0;
	uint8_t buf[8] = {0};
	int32_t i = 0;
	int32_t retry = 10;

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	NVT_LOG("++, cmd: 0xBF, sub_cmd: 0x%02X\n", sub_cmd);
	//---set xdata index to EVENT BUF ADDR---
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("nvt_set_page fail! ret=%d\n", ret);
		goto out;
	}

	for (i = 0; i < retry; i++) {
		if (buf[1] != 0xBF) {
			buf[0] = EVENT_MAP_HOST_CMD;
			buf[1] = 0xBF;
			buf[2] = sub_cmd;
			buf[3] = 0x01; // R
			ret = CTP_SPI_WRITE(ts->client, buf, 4);
			if (ret < 0) {
				NVT_ERR("CTP_SPI_WRITE fail! ret=%d\n", ret);
				goto out;
			}
		}

		usleep_range(10000, 11000);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		ret = CTP_SPI_READ(ts->client, buf, 6);
		if (ret < 0) {
			NVT_ERR("CTP_SPI_READ fail! ret=%d\n", ret);
			goto out;
		}
		if (buf[1] == 0x00)
			break;
	}

	if (unlikely(i >= retry)) {
		NVT_ERR("send cmd failed, buf[1] = 0x%02X\n", buf[1]);
		ret = -1;
		nvt_read_fw_history_all();
		nvt_read_print_fw_flow_debug_message();
	} else {
		NVT_LOG("send cmd success, tried %d times\n", i);
		*value = (int16_t)((buf[5] << 8) | buf[4]);
		NVT_LOG("get value: %d\n", *value);
		ret = 0;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_set_op_mode(int16_t op_mode)
{
	int32_t ret = 0;

	NVT_LOG("++, set op_mode: %d\n", op_mode);
	ret = nvt_set_extend_custom_cmd(0x01, op_mode);
	if (ret < 0) {
		NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_op_mode(int16_t *op_mode)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x01, op_mode);
	if (ret < 0) {
		NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}
	NVT_LOG("get op_mode: %d\n", *op_mode);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_op_mode_proc_read(struct file *filp, char __user *buf,
					size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t op_mode;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_op_mode(&op_mode);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "op_mode: %d\n", op_mode);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_op_mode_proc_write(struct file *filp,
					 const char __user *buf, size_t count,
					 loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t op_mode;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 1 || count == 2) {
		tmp_buf = kzalloc(count + 1, GFP_KERNEL);
		if (!tmp_buf) {
			NVT_ERR("Allocate tmp_buf fail!\n");
			ret = -ENOMEM;
			goto out;
		}
		if (copy_from_user(tmp_buf, buf, count)) {
			NVT_ERR("copy_from_user() error!\n");
			ret = -EFAULT;
			goto out;
		}

		ret = sscanf(tmp_buf, "%d", &tmp);
		if (ret != 1) {
			NVT_ERR("Invalid value! ret = %d\n", ret);
			ret = -EINVAL;
			goto out;
		}
		op_mode = (int16_t)tmp;
		NVT_LOG("op_mode = %d\n", op_mode);

		if (mutex_lock_interruptible(&ts->lock)) {
			ret = -ERESTARTSYS;
			goto out;
		}

	#if NVT_TOUCH_ESD_PROTECT
		nvt_esd_check_enable(false);
	#endif /* #if NVT_TOUCH_ESD_PROTECT */

		nvt_xm_htc_set_op_mode(op_mode);

		mutex_unlock(&ts->lock);

		ret = count;
	} else {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
	}
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops xm_htc_op_mode_fops = {
	.proc_read = xm_htc_op_mode_proc_read,
	.proc_write = xm_htc_op_mode_proc_write,
};
#else
static const struct file_operations xm_htc_op_mode_fops = {
	.owner = THIS_MODULE,
	.read = xm_htc_op_mode_proc_read,
	.write = xm_htc_op_mode_proc_write,
};
#endif

int32_t nvt_xm_htc_set_idle_wake_th(int16_t idle_wake_th)
{
	int32_t ret = 0;

	NVT_LOG("++\n");
	NVT_LOG("set idle_wake_th: %d\n", idle_wake_th);
	ret = nvt_set_extend_custom_cmd(0x02, idle_wake_th);

	if (ret < 0) {
		NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_idle_wake_th(int16_t *idle_wake_th)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x02, idle_wake_th);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get idle_wake_th: %d\n", *idle_wake_th);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_idle_wake_th_proc_read(struct file *filp,
					     char __user *buf, size_t count,
					     loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t idle_wake_th;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_idle_wake_th(&idle_wake_th);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "idle_wake_th: %d\n",
		       idle_wake_th);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_idle_wake_th_proc_write(struct file *filp,
					      const char __user *buf,
					      size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t idle_wake_th;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 6) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	idle_wake_th = (int16_t)tmp;
	NVT_LOG("idle_wake_th = %d\n", idle_wake_th);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_idle_wake_th(idle_wake_th);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops xm_htc_idle_wake_th_fops = {
	.proc_read = xm_htc_idle_wake_th_proc_read,
	.proc_write = xm_htc_idle_wake_th_proc_write,
};
#else
static const struct file_operations xm_htc_idle_wake_th_fops = {
	.owner = THIS_MODULE,
	.read = xm_htc_idle_wake_th_proc_read,
	.write = xm_htc_idle_wake_th_proc_write,
};
#endif

int32_t nvt_xm_htc_set_stylus_enable(int16_t stylus_enable)
{
	int32_t ret = 0;

	NVT_LOG("++, set stylus_enable: %d\n", stylus_enable);
	ret = nvt_set_extend_custom_cmd(0x04, stylus_enable);

    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_stylus_enable(int16_t *stylus_enable)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x04, stylus_enable);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get stylus_enable: %d\n", *stylus_enable);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_stylus_enable_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t stylus_enable;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_stylus_enable(&stylus_enable);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "stylus_enable: %d\n", stylus_enable);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_stylus_enable_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t stylus_enable;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	stylus_enable = (int16_t)tmp;
	NVT_LOG("stylus_enable = %d\n", stylus_enable);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_stylus_enable(stylus_enable);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_stylus_enable_fops = {
    .proc_read = xm_htc_stylus_enable_proc_read,
    .proc_write = xm_htc_stylus_enable_proc_write,
};
#else
static const struct file_operations xm_htc_stylus_enable_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_stylus_enable_proc_read,
	.write = xm_htc_stylus_enable_proc_write,
};
#endif

int32_t nvt_xm_htc_set_stylus_only(int16_t stylus_only)
{
	int32_t ret = 0;

	NVT_LOG("++\n");
	NVT_LOG("set stylus_only: %d\n", stylus_only);

	ret = nvt_set_extend_custom_cmd(0x05, stylus_only);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_stylus_only(int16_t *stylus_only)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x05, stylus_only);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get stylus_only: %d\n", *stylus_only);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_stylus_only_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t stylus_only;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_stylus_only(&stylus_only);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "stylus_only: %d\n", stylus_only);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_stylus_only_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t stylus_only;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	stylus_only = (int16_t)tmp;
	NVT_LOG("stylus_only = %d\n", stylus_only);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_stylus_only(stylus_only);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_stylus_only_fops = {
    .proc_read = xm_htc_stylus_only_proc_read,
    .proc_write = xm_htc_stylus_only_proc_write,
};
#else
static const struct file_operations xm_htc_stylus_only_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_stylus_only_proc_read,
	.write = xm_htc_stylus_only_proc_write,
};
#endif

int32_t nvt_xm_htc_set_raw_data_type(int16_t raw_data_type)
{
	int32_t ret = 0;

	NVT_LOG("++\n");
	NVT_LOG("set raw_data_type: %d\n", raw_data_type);

	ret = nvt_set_extend_custom_cmd(0x06, raw_data_type);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_raw_data_type(int16_t *raw_data_type)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x06, raw_data_type);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get raw_data_type: %d\n", *raw_data_type);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_raw_data_type_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t raw_data_type;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_raw_data_type(&raw_data_type);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "raw_data_type: %d\n", raw_data_type);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_raw_data_type_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t raw_data_type;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	raw_data_type = (int16_t)tmp;
	NVT_LOG("raw_data_type = %d\n", raw_data_type);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_raw_data_type(raw_data_type);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_raw_data_type_fops = {
    .proc_read = xm_htc_raw_data_type_proc_read,
    .proc_write = xm_htc_raw_data_type_proc_write,
};
#else
static const struct file_operations xm_htc_raw_data_type_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_raw_data_type_proc_read,
	.write = xm_htc_raw_data_type_proc_write,
};
#endif

int32_t nvt_xm_htc_set_gesture_switch(int16_t gesture_switch)
{
	int32_t ret = 0;

	NVT_LOG("++, set gesture_switch: 0x%04X\n", gesture_switch);
	ret = nvt_set_extend_custom_cmd(0x1E, gesture_switch);
	if (ret < 0) {
		NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}

out:
	NVT_LOG("--\n");

	return ret;
}

int32_t nvt_xm_htc_get_gesture_switch(int16_t *gesture_switch)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x1E, gesture_switch);
	if (ret < 0) {
		NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}
	NVT_LOG("get gesture_switch: %d\n", *gesture_switch);

out:
	return ret;
}

static ssize_t xm_htc_gesture_switch_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t gesture_switch;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_gesture_switch(&gesture_switch);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "gesture_switch: %d\n", gesture_switch);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_gesture_switch_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t gesture_switch;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	gesture_switch = (int16_t)tmp;
	NVT_LOG("gesture_switch = %d\n", gesture_switch);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_gesture_switch(gesture_switch);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_gesture_switch_fops = {
    .proc_read = xm_htc_gesture_switch_proc_read,
    .proc_write = xm_htc_gesture_switch_proc_write,
};
#else
static const struct file_operations xm_htc_gesture_switch_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_gesture_switch_proc_read,
	.write = xm_htc_gesture_switch_proc_write,
};
#endif

int32_t nvt_xm_htc_set_sw_reset(int16_t sw_reset)
{
	int32_t ret = 0;

	NVT_LOG("++\n");
	NVT_LOG("set sw_reset: %d\n", sw_reset);

	if (!!sw_reset) {
		nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	}

	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_sw_reset(int16_t *sw_reset)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	*sw_reset = ts->xm_htc_sw_reset;

	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_sw_reset_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t sw_reset;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_sw_reset(&sw_reset);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "sw_reset: %d\n", sw_reset);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_sw_reset_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t sw_reset;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	sw_reset = (int16_t)tmp;
	NVT_LOG("sw_reset = %d\n", sw_reset);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_sw_reset(sw_reset);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_sw_reset_fops = {
    .proc_read = xm_htc_sw_reset_proc_read,
    .proc_write = xm_htc_sw_reset_proc_write,
};
#else
static const struct file_operations xm_htc_sw_reset_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_sw_reset_proc_read,
	.write = xm_htc_sw_reset_proc_write,
};
#endif

int32_t nvt_xm_htc_set_fh_enable(int16_t fh_enable)
{
	int32_t ret = 0;

	NVT_LOG("++, set fh_enable: %d\n", fh_enable);
	ret = nvt_set_extend_custom_cmd(0x07, fh_enable);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_fh_enable(int16_t *fh_enable)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x07, fh_enable);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get fh_enable: %d\n", *fh_enable);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_fh_enable_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t fh_enable;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_fh_enable(&fh_enable);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "fh_enable: %d\n", fh_enable);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_fh_enable_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t fh_enable;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	fh_enable = (int16_t)tmp;
	NVT_LOG("fh_enable = %d\n", fh_enable);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_fh_enable(fh_enable);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_fh_enable_fops = {
    .proc_read = xm_htc_fh_enable_proc_read,
    .proc_write = xm_htc_fh_enable_proc_write,
};
#else
static const struct file_operations xm_htc_fh_enable_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_fh_enable_proc_read,
	.write = xm_htc_fh_enable_proc_write,
};
#endif

int32_t nvt_xm_htc_set_scan_freq_no(int16_t scan_freq_no)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set scan_freq_no: %d\n", scan_freq_no);
	ret = nvt_set_extend_custom_cmd(0x08, scan_freq_no);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_scan_freq_no(int16_t *scan_freq_no)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x08, scan_freq_no);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get scan_freq_no: %d\n", *scan_freq_no);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_scan_freq_no_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t scan_freq_no;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_scan_freq_no(&scan_freq_no);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "scan_freq_no: %d\n", scan_freq_no);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_scan_freq_no_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t scan_freq_no;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	scan_freq_no = (int16_t)tmp;
	NVT_LOG("scan_freq_no = %d\n", scan_freq_no);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_scan_freq_no(scan_freq_no);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_scan_freq_no_fops = {
    .proc_read = xm_htc_scan_freq_no_proc_read,
    .proc_write = xm_htc_scan_freq_no_proc_write,
};
#else
static const struct file_operations xm_htc_scan_freq_no_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_scan_freq_no_proc_read,
	.write = xm_htc_scan_freq_no_proc_write,
};
#endif

int32_t nvt_xm_htc_get_scan_freq(int16_t *scan_freq)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x09, scan_freq);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get scan_freq: %d\n", *scan_freq);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_scan_freq_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t scan_freq;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_scan_freq(&scan_freq);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "scan_freq: %d\n", scan_freq);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_scan_freq_fops = {
    .proc_read = xm_htc_scan_freq_proc_read,
};
#else
static const struct file_operations xm_htc_scan_freq_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_scan_freq_proc_read,
};
#endif

int32_t nvt_xm_htc_get_hand_scan_rate(int16_t *hand_scan_rate)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x0A, hand_scan_rate);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get hand_scan_rate: %d\n", *hand_scan_rate);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_hand_scan_rate_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t hand_scan_rate;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_hand_scan_rate(&hand_scan_rate);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "hand_scan_rate: %d\n", hand_scan_rate);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_hand_scan_rate_fops = {
    .proc_read = xm_htc_hand_scan_rate_proc_read,
};
#else
static const struct file_operations xm_htc_hand_scan_rate_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_hand_scan_rate_proc_read,
};
#endif

int32_t nvt_xm_htc_get_pen_scan_rate(int16_t *pen_scan_rate)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x0B, pen_scan_rate);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get pen_scan_rate: %d\n", *pen_scan_rate);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_pen_scan_rate_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t pen_scan_rate;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_pen_scan_rate(&pen_scan_rate);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "pen_scan_rate: %d\n", pen_scan_rate);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_pen_scan_rate_fops = {
    .proc_read = xm_htc_pen_scan_rate_proc_read,
};
#else
static const struct file_operations xm_htc_pen_scan_rate_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_pen_scan_rate_proc_read,
};
#endif

int32_t nvt_xm_htc_set_calibration(int16_t calibration)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set calibration %d\n", calibration);
	ret = nvt_set_extend_custom_cmd(0x0C, calibration);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_calibration(int16_t *calibration)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x0C, calibration);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get calibration: %d\n", *calibration);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_calibration_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t calibration;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_calibration(&calibration);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "calibration: %d\n", calibration);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_calibration_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t calibration;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	calibration = (int16_t)tmp;
	NVT_LOG("calibration = %d\n", calibration);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_calibration(calibration);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_calibration_fops = {
    .proc_read = xm_htc_calibration_proc_read,
    .proc_write = xm_htc_calibration_proc_write,
};
#else
static const struct file_operations xm_htc_calibration_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_calibration_proc_read,
	.write = xm_htc_calibration_proc_write,
};
#endif

int32_t nvt_xm_htc_set_int_state(int16_t int_state)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set int_state %d\n", int_state);
	ret = nvt_set_extend_custom_cmd(0x0D, int_state);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_int_state(int16_t *int_state)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x0D, int_state);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get int_state: %d\n", *int_state);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_int_state_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t int_state;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_int_state(&int_state);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "int_state: %d\n", int_state);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_int_state_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t int_state;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	int_state = (int16_t)tmp;
	NVT_LOG("int_state = %d\n", int_state);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_int_state(int_state);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_int_state_fops = {
    .proc_read = xm_htc_int_state_proc_read,
    .proc_write = xm_htc_int_state_proc_write,
};
#else
static const struct file_operations xm_htc_int_state_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_int_state_proc_read,
	.write = xm_htc_int_state_proc_write,
};
#endif

int32_t nvt_xm_htc_set_single_step(int16_t single_step)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set single_step %d\n", single_step);
	ret = nvt_set_extend_custom_cmd(0x0E, single_step);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_single_step(int16_t *single_step)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x0E, single_step);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get single_step: %d\n", *single_step);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_single_step_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t single_step;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_single_step(&single_step);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "single_step: %d\n", single_step);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_single_step_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t single_step;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	single_step = (int16_t)tmp;
	NVT_LOG("single_step = %d\n", single_step);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_single_step(single_step);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_single_step_fops = {
    .proc_read = xm_htc_single_step_proc_read,
    .proc_write = xm_htc_single_step_proc_write,
};
#else
static const struct file_operations xm_htc_single_step_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_single_step_proc_read,
	.write = xm_htc_single_step_proc_write,
};
#endif

int32_t nvt_xm_htc_set_game_mode(int16_t game_mode)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set game_mode %d\n", game_mode);
	ret = nvt_set_extend_custom_cmd(0x0F, game_mode);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_game_mode(int16_t *game_mode)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x0F, game_mode);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get game_mode: %d\n", *game_mode);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_game_mode_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t game_mode;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_game_mode(&game_mode);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "game_mode: %d\n", game_mode);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_game_mode_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t game_mode;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	game_mode = (int16_t)tmp;
	NVT_LOG("game_mode = %d\n", game_mode);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_game_mode(game_mode);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_game_mode_fops = {
    .proc_read = xm_htc_game_mode_proc_read,
    .proc_write = xm_htc_game_mode_proc_write,
};
#else
static const struct file_operations xm_htc_game_mode_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_game_mode_proc_read,
	.write = xm_htc_game_mode_proc_write,
};
#endif

int32_t nvt_xm_htc_set_charger(int16_t charger)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set charger %d\n", charger);
	ret = nvt_set_extend_custom_cmd(0x10, charger);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_charger(int16_t *charger)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x10, charger);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get charger: %d\n", *charger);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_charger_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t charger;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_charger(&charger);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "charger: %d\n", charger);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_charger_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t charger;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	charger = (int16_t)tmp;
	NVT_LOG("charger = %d\n", charger);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_charger(charger);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_charger_fops = {
    .proc_read = xm_htc_charger_proc_read,
    .proc_write = xm_htc_charger_proc_write,
};
#else
static const struct file_operations xm_htc_charger_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_charger_proc_read,
	.write = xm_htc_charger_proc_write,
};
#endif

int32_t nvt_xm_htc_set_gesture_dbclk(int16_t gesture_dbclk)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set gesture_dbclk %d\n", gesture_dbclk);
	ret = nvt_set_extend_custom_cmd(0x11, gesture_dbclk);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_gesture_dbclk(int16_t *gesture_dbclk)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x11, gesture_dbclk);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get gesture_dbclk: %d\n", *gesture_dbclk);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_gesture_dbclk_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t gesture_dbclk;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_gesture_dbclk(&gesture_dbclk);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "gesture_dbclk: %d\n", gesture_dbclk);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_gesture_dbclk_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t gesture_dbclk;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	gesture_dbclk = (int16_t)tmp;
	NVT_LOG("gesture_dbclk = %d\n", gesture_dbclk);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_gesture_dbclk(gesture_dbclk);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_gesture_dbclk_fops = {
    .proc_read = xm_htc_gesture_dbclk_proc_read,
    .proc_write = xm_htc_gesture_dbclk_proc_write,
};
#else
static const struct file_operations xm_htc_gesture_dbclk_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_gesture_dbclk_proc_read,
	.write = xm_htc_gesture_dbclk_proc_write,
};
#endif

int32_t nvt_xm_htc_set_flg_buf(int16_t flg_buf)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set flg_buf %d\n", flg_buf);
	ret = nvt_set_extend_custom_cmd(0x12, flg_buf);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_flg_buf(int16_t *flg_buf)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x12, flg_buf);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get flg_buf: %d\n", *flg_buf);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_flg_buf_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t flg_buf;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_flg_buf(&flg_buf);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "flg_buf: %d\n", flg_buf);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_flg_buf_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t flg_buf;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 6) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	flg_buf = (int16_t)tmp;
	NVT_LOG("flg_buf = %d\n", flg_buf);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_flg_buf(flg_buf);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_flg_buf_fops = {
    .proc_read = xm_htc_flg_buf_proc_read,
    .proc_write = xm_htc_flg_buf_proc_write,
};
#else
static const struct file_operations xm_htc_flg_buf_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_flg_buf_proc_read,
	.write = xm_htc_flg_buf_proc_write,
};
#endif

int32_t nvt_xm_htc_set_ic_log_level(int16_t ic_log_level)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set ic_log_level %d\n", ic_log_level);
	ret = nvt_set_extend_custom_cmd(0x13, ic_log_level);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_ic_log_level(int16_t *ic_log_level)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x13, ic_log_level);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get ic_log_level: %d\n", *ic_log_level);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_ic_log_level_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t ic_log_level;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_ic_log_level(&ic_log_level);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "ic_log_level: %d\n", ic_log_level);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_ic_log_level_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t ic_log_level;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	ic_log_level = (int16_t)tmp;
	NVT_LOG("ic_log_level = %d\n", ic_log_level);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_ic_log_level(ic_log_level);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_ic_log_level_fops = {
    .proc_read = xm_htc_ic_log_level_proc_read,
    .proc_write = xm_htc_ic_log_level_proc_write,
};
#else
static const struct file_operations xm_htc_ic_log_level_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_ic_log_level_proc_read,
	.write = xm_htc_ic_log_level_proc_write,
};
#endif

int32_t nvt_xm_htc_set_filter_level(int16_t filter_level)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set filter_level %d\n", filter_level);
	ret = nvt_set_extend_custom_cmd(0x14, filter_level);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_filter_level(int16_t *filter_level)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x14, filter_level);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get filter_level: %d\n", *filter_level);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_filter_level_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t filter_level;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_filter_level(&filter_level);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "filter_level: %d\n", filter_level);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_filter_level_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t filter_level;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	filter_level = (int16_t)tmp;
	NVT_LOG("filter_level = %d\n", filter_level);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_filter_level(filter_level);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_filter_level_fops = {
    .proc_read = xm_htc_filter_level_proc_read,
    .proc_write = xm_htc_filter_level_proc_write,
};
#else
static const struct file_operations xm_htc_filter_level_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_filter_level_proc_read,
	.write = xm_htc_filter_level_proc_write,
};
#endif

int32_t nvt_xm_htc_set_report_coordinate(int16_t report_coordinate)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set report_coordinate %d\n", report_coordinate);
	ts->xm_htc_report_coordinate = !!report_coordinate;
	ret = nvt_set_extend_custom_cmd(0x15, report_coordinate);
    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_report_coordinate(int16_t *report_coordinate)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x15, report_coordinate);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get report_coordinate: %d\n", *report_coordinate);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_report_coordinate_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t report_coordinate;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_report_coordinate(&report_coordinate);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "report_coordinate: %d\n", report_coordinate);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_report_coordinate_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t report_coordinate;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	report_coordinate = (int16_t)tmp;
	NVT_LOG("report_coordinate = %d\n", report_coordinate);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_report_coordinate(report_coordinate);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_report_coordinate_fops = {
    .proc_read = xm_htc_report_coordinate_proc_read,
    .proc_write = xm_htc_report_coordinate_proc_write,
};
#else
static const struct file_operations xm_htc_report_coordinate_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_report_coordinate_proc_read,
	.write = xm_htc_report_coordinate_proc_write,
};
#endif

int32_t nvt_xm_htc_set_idle_baseline_update(void)
{
	int32_t ret = 0;

	NVT_LOG("++, set idle baseline update\n");
	ret = nvt_set_extend_custom_cmd(0x19, 0x01);
	if (ret < 0) {
		NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_idle_baseline_update(int16_t *idle_baseline_update)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x19, idle_baseline_update);
	if (ret < 0) {
		NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}
	NVT_LOG("get idle baseline update: %d\n", *idle_baseline_update);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_idle_baseline_update_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t idle_baseline_update;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_idle_baseline_update(&idle_baseline_update);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "idle baseline update: %d\n", idle_baseline_update);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_idle_baseline_update_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t idle_baseline_update;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	idle_baseline_update = (int16_t)tmp;
	NVT_LOG("idle_baseline_update = %d\n", idle_baseline_update);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_idle_baseline_update();

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_idle_baseline_update_fops = {
	.proc_read = xm_htc_idle_baseline_update_proc_read,
	.proc_write = xm_htc_idle_baseline_update_proc_write,
};
#else
static const struct file_operations xm_htc_idle_baseline_update_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_idle_baseline_update_proc_read,
	.write = xm_htc_idle_baseline_update_proc_write,
};
#endif
int32_t nvt_xm_htc_set_click_gesture_enable(int16_t click_gesture_enable)
{
	int32_t ret = 0;

	NVT_LOG("++, set click_gesture_enable %d\n", click_gesture_enable);
	ret = nvt_set_extend_custom_cmd(0x1B, click_gesture_enable);

    if (ret < 0) {
        NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_click_gesture_enable(int16_t *click_gesture_enable)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x1B, click_gesture_enable);
    if (ret < 0) {
        NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
        goto out;
    }
	NVT_LOG("get click_gesture_enable: %d\n", *click_gesture_enable);

out:
	NVT_LOG("--\n");
	return ret;
}


static ssize_t xm_htc_click_gesture_enable_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t click_gesture_enable = 0;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_click_gesture_enable(&click_gesture_enable);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "click_gesture_enable: %d\n", click_gesture_enable);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_click_gesture_enable_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t click_gesture_enable;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	click_gesture_enable = (int16_t)tmp;

	NVT_LOG("click_gesture_enable = %d\n", click_gesture_enable);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_click_gesture_enable(click_gesture_enable);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_click_gesture_enable_fops = {
    .proc_read = xm_htc_click_gesture_enable_proc_read,
    .proc_write = xm_htc_click_gesture_enable_proc_write,
};
#else
static const struct file_operations xm_htc_click_gesture_enable_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_click_gesture_enable_proc_read,
	.write = xm_htc_click_gesture_enable_proc_write,
};
#endif

int32_t nvt_xm_htc_set_idle_high_base_en(int16_t idle_high_base_en)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	NVT_LOG("set idle_high_base_en: %d\n", idle_high_base_en);
	ret = nvt_set_extend_custom_cmd(0x1F, idle_high_base_en);
	if (ret < 0) {
		NVT_ERR("nvt_set_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

int32_t nvt_xm_htc_get_idle_high_base_en(int16_t *idle_high_base_en)
{
	int32_t ret = 0;

	NVT_LOG("++\n");

	ret = nvt_get_extend_custom_cmd(0x1F, idle_high_base_en);
	if (ret < 0) {
		NVT_ERR("nvt_get_extend_custom_cmd fail! ret=%d\n", ret);
		goto out;
	}
	NVT_LOG("get idle_high_base_en: %d\n", *idle_high_base_en);

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_idle_high_base_en_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int finished = 0;
	int32_t cnt = 0;
	int32_t len = 0;
	int16_t idle_high_base_en;
	char tmp_buf[64];

	NVT_LOG("++\n");

	/*
	* We return 0 to indicate end of file, that we have
	* no more information. Otherwise, processes will
	* continue to read from us in an endless loop.
	*/
	if (finished) {
		NVT_LOG("read END\n");
		finished = 0;
		return 0;
	}
	finished = 1;

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_get_idle_high_base_en(&idle_high_base_en);

	mutex_unlock(&ts->lock);

	cnt = snprintf(tmp_buf, sizeof(tmp_buf), "idle_high_base_en: %d\n", idle_high_base_en);
	if (copy_to_user(buf, tmp_buf, sizeof(tmp_buf))) {
		NVT_ERR("copy_to_user() error!\n");
		return -EFAULT;
	}
	buf += cnt;
	len += cnt;

	NVT_LOG("--\n");
	return len;
}

static ssize_t xm_htc_idle_high_base_en_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t idle_high_base_en;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	idle_high_base_en = (int16_t)tmp;
	NVT_LOG("idle_high_base_en = %d\n", idle_high_base_en);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_idle_high_base_en(idle_high_base_en);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_idle_high_base_en_fops = {
	.proc_read = xm_htc_idle_high_base_en_proc_read,
	.proc_write = xm_htc_idle_high_base_en_proc_write,
};
#else
static const struct file_operations xm_htc_idle_high_base_en_fops = {
	.owner = THIS_MODULE,
	.read  = xm_htc_idle_high_base_en_proc_read,
	.write = xm_htc_idle_high_base_en_proc_write,
};
#endif


int32_t nvt_xm_htc_set_stylus_pressure(int16_t stylus_pressure)
{
	int32_t ret = 0;
	uint8_t buf[8] = {0};
	uint8_t sub_cmd = 0x27;
	int16_t value = stylus_pressure;

	NVT_LOG("++\n");

	NVT_LOG("set stylus_pressure: %d\n", stylus_pressure);

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	NVT_LOG("cmd: 0xBF, sub_cmd: 0x%02X, value: %d\n", sub_cmd, value);
	//---set xdata index to EVENT BUF ADDR---
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("nvt_set_page fail! ret=%d\n", ret);
		goto out;
	}

	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0xBF;
	buf[2] = sub_cmd;
	buf[3] = 0x00; // W
	buf[4] = value & 0xFF;
	buf[5] = (value >> 8) & 0xFF;
	ret = CTP_SPI_WRITE(ts->client, buf, 6);
	if (ret < 0) {
		NVT_ERR("--, CTP_SPI_WRITE fail! ret=%d\n", ret);
		goto out;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

static ssize_t xm_htc_stylus_pressure_proc_write(struct file *filp,const char __user *buf, size_t count, loff_t *f_pos)
{
	int32_t ret;
	int32_t tmp;
	int16_t stylus_pressure;
	char *tmp_buf = NULL;

	NVT_LOG("++\n");

	if (count == 0 || count > 2) {
		NVT_ERR("Invalid value! count = %zu\n", count);
		ret = -EINVAL;
		goto out;
	}

	tmp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_buf) {
		NVT_ERR("Allocate tmp_buf fail!\n");
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(tmp_buf, buf, count)) {
		NVT_ERR("copy_from_user() error!\n");
		ret = -EFAULT;
		goto out;
	}

	ret = sscanf(tmp_buf, "%d", &tmp);
	if (ret != 1) {
		NVT_ERR("Invalid value! ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}
	stylus_pressure = (int16_t)tmp;
	NVT_LOG("stylus_pressure = %d\n", stylus_pressure);

	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	nvt_xm_htc_set_stylus_pressure(stylus_pressure);

	mutex_unlock(&ts->lock);

	ret = count;
out:
	if (tmp_buf)
		kfree(tmp_buf);
	NVT_LOG("--\n");
	return ret;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  xm_htc_stylus_pressure_fops = {
	.proc_write = xm_htc_stylus_pressure_proc_write,
};
#else
static const struct file_operations xm_htc_stylus_pressure_fops = {
	.owner = THIS_MODULE,
	.write = xm_htc_stylus_pressure_proc_write,
};
#endif

#endif /* #if TOUCH_THP_SUPPORT */
//thp end 6.9

/*******************************************************
Description:
	Novatek touchscreen extra function proc. file node
	initial function.

return:
	Executive outcomes. 0---succeed. -12---failed.
*******************************************************/
int32_t nvt_extra_proc_init(void)
{
	NVT_proc_fw_version_entry = proc_create(NVT_FW_VERSION, 0444, NULL,&nvt_fw_version_fops);
	if (NVT_proc_fw_version_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_FW_VERSION);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_FW_VERSION);
	}

	NVT_proc_baseline_entry = proc_create(NVT_BASELINE, 0444, NULL,&nvt_baseline_fops);
	if (NVT_proc_baseline_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_BASELINE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_BASELINE);
	}

	NVT_proc_raw_entry = proc_create(NVT_RAW, 0444, NULL,&nvt_raw_fops);
	if (NVT_proc_raw_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_RAW);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_RAW);
	}

	NVT_proc_diff_entry = proc_create(NVT_DIFF, 0444, NULL,&nvt_diff_fops);
	if (NVT_proc_diff_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_DIFF);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_DIFF);
	}

	if (ts->pen_support) {
		NVT_proc_pen_switch_entry = proc_create(NVT_PEN_SWITCH, 0666, NULL, &nvt_pen_switch_fops);
		if (NVT_proc_pen_switch_entry == NULL) {
			NVT_ERR("create proc/%s Failed!\n", NVT_PEN_SWITCH);
			return -ENOMEM;
		} else {
			NVT_LOG("create proc/%s Succeeded!\n", NVT_PEN_SWITCH);
		}

		NVT_proc_active_pen_stationary_entry = proc_create(NVT_ACTIVE_PEN_STATIONARY, 0666, NULL, &nvt_active_pen_stationary_fops);
		if (NVT_proc_active_pen_stationary_entry == NULL) {
			NVT_ERR("create proc/%s Failed!\n", NVT_ACTIVE_PEN_STATIONARY);
			return -ENOMEM;
		} else {
			NVT_LOG("create proc/%s Succeeded!\n", NVT_ACTIVE_PEN_STATIONARY);
		}
	}

	//thp start 6.9
#if TOUCH_THP_SUPPORT
	NVT_proc_eventbuf_debug_entry = proc_create(NVT_EVENTBUF_DEBUG, 0444, NULL,&nvt_eventbuf_debug_fops);
	if (NVT_proc_eventbuf_debug_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_EVENTBUF_DEBUG);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_EVENTBUF_DEBUG);
	}

	XM_HTC_proc_op_mode_entry = proc_create(XM_HTC_OP_MODE, 0666, NULL, &xm_htc_op_mode_fops);
	if (XM_HTC_proc_op_mode_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_OP_MODE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_OP_MODE);
	}

	XM_HTC_proc_idle_wake_th_entry = proc_create(XM_HTC_IDLE_WAKE_TH, 0666, NULL, &xm_htc_idle_wake_th_fops);
	if (XM_HTC_proc_idle_wake_th_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_IDLE_WAKE_TH);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_IDLE_WAKE_TH);
	}

	XM_HTC_proc_stylus_enable_entry = proc_create(XM_HTC_STYLUS_ENABLE, 0666, NULL, &xm_htc_stylus_enable_fops);
	if ( XM_HTC_proc_stylus_enable_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_STYLUS_ENABLE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_STYLUS_ENABLE);
	}

	XM_HTC_proc_stylus_only_entry = proc_create(XM_HTC_STYLUS_ONLY, 0666, NULL, &xm_htc_stylus_only_fops);
	if ( XM_HTC_proc_stylus_only_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_STYLUS_ONLY);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_STYLUS_ONLY);
	}

	XM_HTC_proc_raw_data_type_entry = proc_create(XM_HTC_RAW_DATA_TYPE, 0666, NULL, &xm_htc_raw_data_type_fops);
	if ( XM_HTC_proc_raw_data_type_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_RAW_DATA_TYPE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_RAW_DATA_TYPE);
	}


	XM_HTC_proc_gesture_switch_entry = proc_create(XM_HTC_GESTURE_SWITCH, 0666, NULL, &xm_htc_gesture_switch_fops);
	if ( XM_HTC_proc_gesture_switch_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_GESTURE_SWITCH);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_GESTURE_SWITCH);
	}

	XM_HTC_proc_sw_reset_entry = proc_create(XM_HTC_SW_RESET, 0666, NULL, &xm_htc_sw_reset_fops);
	if ( XM_HTC_proc_sw_reset_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_SW_RESET);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_SW_RESET);
	}

	XM_HTC_proc_fh_enable_entry = proc_create(XM_HTC_FH_ENABLE, 0666, NULL, &xm_htc_fh_enable_fops);
	if ( XM_HTC_proc_fh_enable_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_FH_ENABLE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_FH_ENABLE);
	}

	XM_HTC_proc_scan_freq_no_entry = proc_create(XM_HTC_SCAN_FREQ_NO, 0666, NULL, &xm_htc_scan_freq_no_fops);
	if ( XM_HTC_proc_scan_freq_no_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_SCAN_FREQ_NO);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_SCAN_FREQ_NO);
	}

	XM_HTC_proc_scan_freq_entry = proc_create(XM_HTC_SCAN_FREQ, 0666, NULL, &xm_htc_scan_freq_fops);
	if ( XM_HTC_proc_scan_freq_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_SCAN_FREQ);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_SCAN_FREQ);
	}


	XM_HTC_proc_hand_scan_rate_entry = proc_create(XM_HTC_HAND_SCAN_RATE, 0666, NULL, &xm_htc_hand_scan_rate_fops);
	if ( XM_HTC_proc_hand_scan_rate_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_HAND_SCAN_RATE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_HAND_SCAN_RATE);
	}

	XM_HTC_proc_pen_scan_rate_entry = proc_create(XM_HTC_PEN_SCAN_RATE, 0666, NULL, &xm_htc_pen_scan_rate_fops);
	if ( XM_HTC_proc_pen_scan_rate_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_PEN_SCAN_RATE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_PEN_SCAN_RATE);
	}

	XM_HTC_proc_calibration_entry = proc_create(XM_HTC_CALIBRATION, 0666, NULL, &xm_htc_calibration_fops);
	if ( XM_HTC_proc_calibration_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_CALIBRATION);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_CALIBRATION);
	}

	XM_HTC_proc_int_state_entry = proc_create(XM_HTC_INT_STATE, 0666, NULL, &xm_htc_int_state_fops);
	if ( XM_HTC_proc_int_state_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_INT_STATE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_INT_STATE);
	}

	XM_HTC_proc_single_step_entry = proc_create(XM_HTC_SINGLE_STEP, 0666, NULL, &xm_htc_single_step_fops);
	if ( XM_HTC_proc_single_step_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_SINGLE_STEP);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_SINGLE_STEP);
	}

	XM_HTC_proc_game_mode_entry = proc_create(XM_HTC_GAME_MODE, 0666, NULL, &xm_htc_game_mode_fops);
	if ( XM_HTC_proc_game_mode_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_GAME_MODE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_GAME_MODE);
	}

	XM_HTC_proc_charger_entry = proc_create(XM_HTC_CHARGER, 0666, NULL, &xm_htc_charger_fops);
	if ( XM_HTC_proc_charger_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_CHARGER);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_CHARGER);
	}

	XM_HTC_proc_gesture_dbclk_entry = proc_create(XM_HTC_GESTURE_DBCLK, 0666, NULL, &xm_htc_gesture_dbclk_fops);
	if ( XM_HTC_proc_gesture_dbclk_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_GESTURE_DBCLK);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_GESTURE_DBCLK);
	}

	XM_HTC_proc_flg_buf_entry = proc_create(XM_HTC_FLG_BUF, 0666, NULL, &xm_htc_flg_buf_fops);
	if ( XM_HTC_proc_flg_buf_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_FLG_BUF);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_FLG_BUF);
	}

	XM_HTC_proc_ic_log_level_entry = proc_create(XM_HTC_IC_LOG_LEVEL, 0666, NULL, &xm_htc_ic_log_level_fops);
	if ( XM_HTC_proc_ic_log_level_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_IC_LOG_LEVEL);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_IC_LOG_LEVEL);
	}

	XM_HTC_proc_filter_level_entry = proc_create(XM_HTC_FILTER_LEVEL, 0666, NULL, &xm_htc_filter_level_fops);
	if ( XM_HTC_proc_filter_level_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_FILTER_LEVEL);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_FILTER_LEVEL);
	}

	XM_HTC_proc_report_coordinate_entry = proc_create(XM_HTC_REPORT_COORDINATE, 0666, NULL, &xm_htc_report_coordinate_fops);
	if ( XM_HTC_proc_report_coordinate_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_REPORT_COORDINATE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_REPORT_COORDINATE);
	}

	XM_HTC_proc_idle_baseline_update_entry = proc_create(XM_HTC_IDLE_BASELINE_UPDATE, 0666, NULL, &xm_htc_idle_baseline_update_fops);
	if ( XM_HTC_proc_idle_baseline_update_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_IDLE_BASELINE_UPDATE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_IDLE_BASELINE_UPDATE);
	}
	XM_HTC_proc_click_gesture_enable_entry = proc_create(XM_HTC_CLICK_GESTURE_ENABLE, 0666, NULL, &xm_htc_click_gesture_enable_fops);
	if ( XM_HTC_proc_click_gesture_enable_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_CLICK_GESTURE_ENABLE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_CLICK_GESTURE_ENABLE);
	}

	XM_HTC_proc_idle_high_base_en_entry = proc_create(XM_HTC_IDLE_HIGH_BASE_EN, 0666, NULL, &xm_htc_idle_high_base_en_fops);
	if ( XM_HTC_proc_idle_high_base_en_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_IDLE_HIGH_BASE_EN);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_IDLE_HIGH_BASE_EN);
	}

	XM_HTC_proc_stylus_pressure_entry = proc_create(XM_HTC_STYLUS_PRESSURE, 0666, NULL, &xm_htc_stylus_pressure_fops);
	if (XM_HTC_proc_stylus_pressure_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", XM_HTC_STYLUS_PRESSURE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", XM_HTC_STYLUS_PRESSURE);
	}

#endif /* #if TOUCH_THP_SUPPORT */
	//thp end 6.9
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen extra function proc. file node
	deinitial function.

return:
	n.a.
*******************************************************/
void nvt_extra_proc_deinit(void)
{
	if (NVT_proc_fw_version_entry != NULL) {
		remove_proc_entry(NVT_FW_VERSION, NULL);
		NVT_proc_fw_version_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_FW_VERSION);
	}

	if (NVT_proc_baseline_entry != NULL) {
		remove_proc_entry(NVT_BASELINE, NULL);
		NVT_proc_baseline_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_BASELINE);
	}

	if (NVT_proc_raw_entry != NULL) {
		remove_proc_entry(NVT_RAW, NULL);
		NVT_proc_raw_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_RAW);
	}

	if (NVT_proc_diff_entry != NULL) {
		remove_proc_entry(NVT_DIFF, NULL);
		NVT_proc_diff_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_DIFF);
	}

	if (ts->pen_support) {
		if (NVT_proc_pen_switch_entry != NULL) {
			remove_proc_entry(NVT_PEN_SWITCH, NULL);
			NVT_proc_pen_switch_entry = NULL;
			NVT_LOG("Removed /proc/%s\n", NVT_PEN_SWITCH);
		}

		if (NVT_proc_active_pen_stationary_entry != NULL) {
			remove_proc_entry(NVT_ACTIVE_PEN_STATIONARY, NULL);
			NVT_proc_active_pen_stationary_entry = NULL;
			NVT_LOG("Removed /proc/%s\n", NVT_ACTIVE_PEN_STATIONARY);
		}
	}

	//thp start 6.9
#if TOUCH_THP_SUPPORT
	if (NVT_proc_eventbuf_debug_entry != NULL) {
		remove_proc_entry(NVT_EVENTBUF_DEBUG, NULL);
		NVT_proc_eventbuf_debug_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_EVENTBUF_DEBUG);
	}

	if (XM_HTC_proc_op_mode_entry != NULL) {
		remove_proc_entry(XM_HTC_OP_MODE, NULL);
		XM_HTC_proc_op_mode_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_OP_MODE);
	}

	if (XM_HTC_proc_idle_wake_th_entry != NULL) {
		remove_proc_entry(XM_HTC_IDLE_WAKE_TH, NULL);
		XM_HTC_proc_idle_wake_th_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_IDLE_WAKE_TH);
	}

	if (XM_HTC_proc_stylus_enable_entry != NULL) {
		remove_proc_entry(XM_HTC_STYLUS_ENABLE, NULL);
		XM_HTC_proc_stylus_enable_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_STYLUS_ENABLE);
	}

	if (XM_HTC_proc_stylus_only_entry != NULL) {
		remove_proc_entry(XM_HTC_STYLUS_ONLY, NULL);
		XM_HTC_proc_stylus_only_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_STYLUS_ONLY);
	}

	if (XM_HTC_proc_raw_data_type_entry != NULL) {
		remove_proc_entry(XM_HTC_RAW_DATA_TYPE, NULL);
		XM_HTC_proc_raw_data_type_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_RAW_DATA_TYPE);
	}


	if (XM_HTC_proc_gesture_switch_entry != NULL) {
		remove_proc_entry(XM_HTC_GESTURE_SWITCH, NULL);
		XM_HTC_proc_gesture_switch_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_GESTURE_SWITCH);
	}

	if (XM_HTC_proc_sw_reset_entry != NULL) {
		remove_proc_entry(XM_HTC_SW_RESET, NULL);
		XM_HTC_proc_sw_reset_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_SW_RESET);
	}

	if (XM_HTC_proc_fh_enable_entry != NULL) {
		remove_proc_entry(XM_HTC_FH_ENABLE, NULL);
		XM_HTC_proc_fh_enable_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_FH_ENABLE);
	}

	if (XM_HTC_proc_scan_freq_no_entry != NULL) {
		remove_proc_entry(XM_HTC_SCAN_FREQ_NO, NULL);
		XM_HTC_proc_scan_freq_no_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_SCAN_FREQ_NO);
	}

	if (XM_HTC_proc_scan_freq_entry != NULL) {
		remove_proc_entry(XM_HTC_SCAN_FREQ, NULL);
		XM_HTC_proc_scan_freq_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_SCAN_FREQ);
	}


	if (XM_HTC_proc_hand_scan_rate_entry != NULL) {
		remove_proc_entry(XM_HTC_HAND_SCAN_RATE, NULL);
		XM_HTC_proc_hand_scan_rate_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_HAND_SCAN_RATE);
	}

	if (XM_HTC_proc_pen_scan_rate_entry != NULL) {
		remove_proc_entry(XM_HTC_PEN_SCAN_RATE, NULL);
		XM_HTC_proc_pen_scan_rate_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_PEN_SCAN_RATE);
	}

	if (XM_HTC_proc_calibration_entry != NULL) {
		remove_proc_entry(XM_HTC_CALIBRATION, NULL);
		XM_HTC_proc_calibration_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_CALIBRATION);
	}

	if (XM_HTC_proc_int_state_entry != NULL) {
		remove_proc_entry(XM_HTC_INT_STATE, NULL);
		XM_HTC_proc_int_state_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_INT_STATE);
	}

	if (XM_HTC_proc_single_step_entry != NULL) {
		remove_proc_entry(XM_HTC_SINGLE_STEP, NULL);
		XM_HTC_proc_single_step_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_SINGLE_STEP);
	}

	if (XM_HTC_proc_game_mode_entry != NULL) {
		remove_proc_entry(XM_HTC_GAME_MODE, NULL);
		XM_HTC_proc_game_mode_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_GAME_MODE);
	}

	if (XM_HTC_proc_charger_entry != NULL) {
		remove_proc_entry(XM_HTC_CHARGER, NULL);
		XM_HTC_proc_charger_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_CHARGER);
	}

	if (XM_HTC_proc_gesture_dbclk_entry != NULL) {
		remove_proc_entry(XM_HTC_GESTURE_DBCLK, NULL);
		XM_HTC_proc_gesture_dbclk_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_GESTURE_DBCLK);
	}

	if (XM_HTC_proc_flg_buf_entry != NULL) {
		remove_proc_entry(XM_HTC_FLG_BUF, NULL);
		XM_HTC_proc_flg_buf_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_FLG_BUF);
	}

	if (XM_HTC_proc_ic_log_level_entry != NULL) {
		remove_proc_entry(XM_HTC_IC_LOG_LEVEL, NULL);
		XM_HTC_proc_ic_log_level_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_IC_LOG_LEVEL);
	}

	if (XM_HTC_proc_filter_level_entry != NULL) {
		remove_proc_entry(XM_HTC_FILTER_LEVEL, NULL);
		XM_HTC_proc_filter_level_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_FILTER_LEVEL);
	}

	if (XM_HTC_proc_report_coordinate_entry != NULL) {
		remove_proc_entry(XM_HTC_REPORT_COORDINATE, NULL);
		XM_HTC_proc_report_coordinate_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_REPORT_COORDINATE);
	}

	if (XM_HTC_proc_idle_baseline_update_entry != NULL) {
		remove_proc_entry(XM_HTC_IDLE_BASELINE_UPDATE, NULL);
		XM_HTC_proc_idle_baseline_update_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_IDLE_BASELINE_UPDATE);
	}
	if (XM_HTC_proc_click_gesture_enable_entry != NULL) {
		remove_proc_entry(XM_HTC_CLICK_GESTURE_ENABLE, NULL);
		XM_HTC_proc_click_gesture_enable_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_CLICK_GESTURE_ENABLE);
	}

	if (XM_HTC_proc_idle_high_base_en_entry != NULL) {
		remove_proc_entry(XM_HTC_IDLE_HIGH_BASE_EN, NULL);
		XM_HTC_proc_idle_high_base_en_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_IDLE_HIGH_BASE_EN);
	}

	if (XM_HTC_proc_stylus_pressure_entry != NULL) {
		remove_proc_entry(XM_HTC_STYLUS_PRESSURE, NULL);
		XM_HTC_proc_stylus_pressure_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", XM_HTC_STYLUS_PRESSURE);
	}

#endif /* #if TOUCH_THP_SUPPORT */
	//thp end 6.9
}
#endif
