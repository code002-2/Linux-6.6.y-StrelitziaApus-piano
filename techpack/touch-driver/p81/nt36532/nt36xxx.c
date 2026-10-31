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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include "nt36xxx.h"
#if defined(CONFIG_DRM_MSM)
#include <linux/msm_drm_notify.h>
#elif defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#if NVT_TOUCH_ESD_PROTECT
#include <linux/jiffies.h>
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#if NVT_TOUCH_ESD_PROTECT
static struct delayed_work nvt_esd_check_work;
static struct workqueue_struct *nvt_esd_check_wq;
static unsigned long irq_timer = 0;
uint8_t esd_check = false;
uint8_t esd_retry = 0;
#endif /* #if NVT_TOUCH_ESD_PROTECT */

//thp start 6.8
static int htc_ic_mode = 0;
//thp end 6.8

#if NVT_TOUCH_EXT_PROC
extern int32_t nvt_extra_proc_init(void);
extern void nvt_extra_proc_deinit(void);
#endif

#if NVT_TOUCH_MP
extern int32_t nvt_mp_proc_init(void);
extern void nvt_mp_proc_deinit(void);
#endif

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
#define TOUCH_MAJOR_MAX_VALUE 255

extern int pen_charge_state_notifier_register_client(struct notifier_block *nb);
extern int pen_charge_state_notifier_unregister_client(struct notifier_block *nb);
extern ssize_t mi_dsi_panel_lockdown_info_read(unsigned char *plockdowninfo);
static int32_t nvt_ts_suspend(struct device *dev);
static int32_t nvt_ts_resume(struct device *dev);
static int32_t nvt_ts_resume_suspend(bool is_resume, u8 gesture_type);
extern void dsi_panel_gesture_enable(bool on);

// for ic_self_test
#define NORMAL_MODE 0x00
#define MP_MODE_CC 0x41
#define FREQ_HOP_DISABLE 0x66
extern void nvt_change_mode(uint8_t mode);
//end

/* cpu_boost_and_cpu_lpm_mode_disable setup */
// extern void touch_irq_boost(void); //qcom support
// extern void lpm_disable_for_input(bool on);
// #ifdef CONFIG_TOUCH_BOOST
// #define EVENT_INPUT 0x1
// extern void lpm_disable_for_dev(bool on, char event_dev);
// #endif	/* CONFIG_TOUCH_BOOST */
/* cpu_boost_and_cpu_lpm_mode_disable setup end*/


#endif	/* #ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */

struct nvt_ts_data *ts;

#if BOOT_UPDATE_FIRMWARE
static struct workqueue_struct *nvt_fwu_wq;
extern void Boot_Update_Firmware(struct work_struct *work);
#endif

extern int32_t nvt_xm_htc_set_idle_baseline_update(void);
extern int32_t nvt_xm_htc_set_op_mode(int16_t op_mode);
extern int32_t nvt_xm_htc_set_stylus_enable(int16_t stylus_enable);

#if defined(_MSM_DRM_NOTIFY_H_)
static int nvt_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#elif defined(CONFIG_FB)
static int nvt_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void nvt_ts_early_suspend(struct early_suspend *h);
static void nvt_ts_late_resume(struct early_suspend *h);
#endif

uint32_t ENG_RST_ADDR  = 0x7FFF80;
uint32_t SPI_RD_FAST_ADDR = 0;	//read from dtsi

enum screen_orientation {
	PANEL_ORIENTATION_DEGREE_0 = 0,	/* normal portrait orientation */
	PANEL_ORIENTATION_DEGREE_90,	/* anticlockwise 90 degrees */
	PANEL_ORIENTATION_DEGREE_180,	/* anticlockwise 180 degrees */
	PANEL_ORIENTATION_DEGREE_270,	/* anticlockwise 270 degrees */
};
// add edge params
enum edge_filter_type {
    ZONE_TYPE_CORNER_ZONE = 0, // 普通角落抑制区域
    ZONE_TYPE_EDGE_ZONE, // 普通边缘抑制
    ZONE_TYPE_GAME_CORNER, // 游戏模式下的角落抑制
    ZONE_TYPE_DEAD_ZONE, // 普通死区
    ZONE_TYPE_BIG_AREA_EDGE, // 存在大面积抑制的边缘抑制
    ZONE_TYPE_BUFFER_FEATURE,//缓冲抑制区
    ZONE_TYPE_GESTURE, //手势区
    ZONE_TYPE_NORMAL // 正常报点区域
};

// type, pos, startx, starty, endx, endy, re, re
static int edge_filter_params[128] = {
	ZONE_TYPE_GAME_CORNER, 0, 0, 0, 0, 0, 0, 0,
	ZONE_TYPE_GAME_CORNER, 1, 0, 0, 0, 0, 0, 0,
	ZONE_TYPE_GAME_CORNER, 2, 0, 0, 0, 2031, 0, 0,
	ZONE_TYPE_GAME_CORNER, 3, 2031, 0, 2031, 3047, 0, 0,
	ZONE_TYPE_EDGE_ZONE, 0, 0, 0, 0, 0, 0, 0,
	ZONE_TYPE_EDGE_ZONE, 1, 0, 0, 0, 0, 0, 0,
	ZONE_TYPE_EDGE_ZONE, 2, 0, 0, 45, 3047, 0, 0,
	ZONE_TYPE_EDGE_ZONE, 3, 1986, 0, 2031, 3047, 0, 0,
	ZONE_TYPE_CORNER_ZONE, 0, 0, 0, 0, 0, 0, 0,
	ZONE_TYPE_CORNER_ZONE, 1, 0, 0, 0, 0, 0, 0,
	ZONE_TYPE_CORNER_ZONE, 2, 0, 2647, 180, 3047, 0, 0,
	ZONE_TYPE_CORNER_ZONE, 3, 1851, 2647, 2031, 3047, 0, 0
};
// add edge params end
#if TOUCH_KEY_NUM > 0
const uint16_t touch_key_array[TOUCH_KEY_NUM] = {
	KEY_BACK,
	KEY_HOME,
	KEY_MENU
};
#endif

#if WAKEUP_GESTURE
const uint16_t gesture_key_array[] = {
	KEY_POWER,  //GESTURE_WORD_C
	KEY_POWER,  //GESTURE_WORD_W
	KEY_POWER,  //GESTURE_WORD_V
	KEY_WAKEUP,  //GESTURE_DOUBLE_CLICK
	KEY_POWER,  //GESTURE_WORD_Z
	KEY_POWER,  //GESTURE_WORD_M
	KEY_POWER,  //GESTURE_WORD_O
	KEY_POWER,  //GESTURE_WORD_e
	KEY_POWER,  //GESTURE_WORD_S
	KEY_POWER,  //GESTURE_SLIDE_UP
	KEY_POWER,  //GESTURE_SLIDE_DOWN
	KEY_POWER,  //GESTURE_SLIDE_LEFT
	KEY_POWER,  //GESTURE_SLIDE_RIGHT
	KEY_WAKEUP,  //GESTURE_SINGLE_CLICK
};
#endif

//thp start 6.8
/*k81 adapt*/  // change to m80
enum AVAIL_ADDR{
#if TOUCH_THP_SUPPORT
	EVENT_BUF_ADDR           = 0x11C400,
#else
	EVENT_BUF_ADDR           = 0x125800,
#endif
	RAW_PIPE0_ADDR           = 0x10B200,
	RAW_PIPE1_ADDR           = 0x10B200,
	BASELINE_ADDR            = 0x109E00,
	BASELINE_BTN_ADDR        = 0,
	DIFF_PIPE0_ADDR          = 0x128140,
	DIFF_PIPE1_ADDR          = 0x129540,
	RAW_BTN_PIPE0_ADDR       = 0,
	RAW_BTN_PIPE1_ADDR       = 0,
	DIFF_BTN_PIPE0_ADDR      = 0,
	DIFF_BTN_PIPE1_ADDR      = 0,
	PEN_2D_BL_TIP_X_ADDR     = 0x10F940,
	PEN_2D_BL_TIP_Y_ADDR     = 0x10FD40,
	PEN_2D_BL_RING_X_ADDR    = 0x110140,
	PEN_2D_BL_RING_Y_ADDR    = 0x110540,
	PEN_2D_DIFF_TIP_X_ADDR   = 0x111140,
	PEN_2D_DIFF_TIP_Y_ADDR   = 0x111540,
	PEN_2D_DIFF_RING_X_ADDR  = 0x111940,
	PEN_2D_DIFF_RING_Y_ADDR  = 0x111D40,
	PEN_2D_RAW_TIP_X_ADDR    = 0x126E00,
	PEN_2D_RAW_TIP_Y_ADDR    = 0x127210,
	PEN_2D_RAW_RING_X_ADDR   = 0x127620,
	PEN_2D_RAW_RING_Y_ADDR   = 0x127A30,
	PEN_1D_DIFF_TIP_X_ADDR   = 0x127E40,
	PEN_1D_DIFF_TIP_Y_ADDR   = 0x127EC0,
	PEN_1D_DIFF_RING_X_ADDR  = 0x127F40,
	PEN_1D_DIFF_RING_Y_ADDR  = 0x127FC0,
	ENB_CASC_REG_ADDR	= 0x1FB12C,
	/* FW History */
	MMAP_HISTORY_EVENT0      = 0x121AFC,
	MMAP_HISTORY_EVENT1      = 0x121B3C,
	MMAP_HISTORY_EVENT0_ICS  = 0x121B80,
	MMAP_HISTORY_EVENT1_ICS  = 0x121BC0,
	/* Phase 2 Host Download */
	BOOT_RDY_ADDR            = 0x1FB50D,
	ACI_ERR_CLR_ADDR         = 0x1FB605,
	TX_AUTO_COPY_EN          = 0x1FC925,
	SPI_DMA_TX_INFO          = 0x1FC914,
	/* BLD CRC */
	BLD_LENGTH_ADDR          = 0x1FB538,	//0x1FB538 ~ 0x1FB53A (3 bytes)
	ILM_LENGTH_ADDR          = 0x1FB518,	//0x1FB518 ~ 0x1FB51A (3 bytes)
	DLM_LENGTH_ADDR          = 0x1FB530,	//0x1FB530 ~ 0x1FB532 (3 bytes)
	BLD_DES_ADDR             = 0x1FB514,	//0x1FB514 ~ 0x1FB516 (3 bytes)
	ILM_DES_ADDR             = 0x1FB528,	//0x1FB528 ~ 0x1FB52A (3 bytes)
	DLM_DES_ADDR             = 0x1FB52C,	//0x1FB52C ~ 0x1FB52E (3 bytes)
	G_ILM_CHECKSUM_ADDR      = 0x1FB500,   //0x1FB500 ~ 0x1FB503 (4 bytes)
	G_DLM_CHECKSUM_ADDR      = 0x1FB504,	//0x1FB504 ~ 0x1FB507 (4 bytes)
	R_ILM_CHECKSUM_ADDR      = 0x1FB520,	//0x1FB520 ~ 0x1FB523 (4 bytes)
	R_DLM_CHECKSUM_ADDR      = 0x1FB524,	//0x1FB524 ~ 0x1FB527 (4 bytes)
	DMA_CRC_EN_ADDR          = 0x1FB536,
	BLD_ILM_DLM_CRC_ADDR     = 0x1FB533,
	DMA_CRC_FLAG_ADDR        = 0x1FB534,
#if TOUCH_THP_SUPPORT
	/* Xiaomi Host Touch Computing */
	XM_HTC_POLL_INFO_ADDR    = 0x1093D8,
#endif
};

bool check_address(int addr) {
	switch (addr) {
	case RAW_BTN_PIPE0_ADDR :
	case RAW_PIPE0_ADDR :
	case BASELINE_ADDR :
	case DIFF_PIPE0_ADDR :
	case DIFF_PIPE1_ADDR :
	case PEN_2D_BL_TIP_X_ADDR :
	case PEN_2D_BL_TIP_Y_ADDR :
	case PEN_2D_BL_RING_X_ADDR :
	case PEN_2D_BL_RING_Y_ADDR :
	case PEN_2D_DIFF_TIP_X_ADDR :
	case PEN_2D_DIFF_TIP_Y_ADDR :
	case PEN_2D_DIFF_RING_X_ADDR :
	case PEN_2D_DIFF_RING_Y_ADDR :
	case PEN_2D_RAW_TIP_X_ADDR :
	case PEN_2D_RAW_TIP_Y_ADDR :
	case PEN_2D_RAW_RING_X_ADDR :
	case PEN_2D_RAW_RING_Y_ADDR :
	case PEN_1D_DIFF_TIP_X_ADDR :
	case PEN_1D_DIFF_TIP_Y_ADDR :
	case PEN_1D_DIFF_RING_X_ADDR :
	case PEN_1D_DIFF_RING_Y_ADDR :
	case ENB_CASC_REG_ADDR:
	// case READ_FLASH_CHECKSUM_ADDR :
	// case RW_FLASH_DATA_ADDR :
	/* FW History */
	case MMAP_HISTORY_EVENT0 :
	case MMAP_HISTORY_EVENT1 :
	case MMAP_HISTORY_EVENT0_ICS :
	case MMAP_HISTORY_EVENT1_ICS :
	/* Phase 2 Host Download */
	case BOOT_RDY_ADDR :
	case ACI_ERR_CLR_ADDR :
	case TX_AUTO_COPY_EN :
	case SPI_DMA_TX_INFO :
	/* BLD CRC */
	case BLD_LENGTH_ADDR :
	case ILM_LENGTH_ADDR :
	case DLM_LENGTH_ADDR :
	case BLD_DES_ADDR :
	case ILM_DES_ADDR :
	case DLM_DES_ADDR :
	case G_ILM_CHECKSUM_ADDR :
	case G_DLM_CHECKSUM_ADDR :
	case R_ILM_CHECKSUM_ADDR :
	case R_DLM_CHECKSUM_ADDR :
	// case BLD_CRC_EN_ADDR :
	case DMA_CRC_EN_ADDR :
	case BLD_ILM_DLM_CRC_ADDR :
	case DMA_CRC_FLAG_ADDR :
	// case CHIP_VER_TRIM_ADDR :
	// case CHIP_VER_TRIM_OLD_ADDR :
		NVT_LOG("address is avail 0x:%x\n", addr);
		return true;
#if TOUCH_THP_SUPPORT
	/* Xiaomi Host Touch Computing */
	case XM_HTC_POLL_INFO_ADDR :
		NVT_LOG("address is avail 0x:%x\n", addr);
		return true;
	case EVENT_BUF_ADDR :
		if (ts->enable_touch_raw) {
			NVT_LOG("address is avail 0x:%x\n", addr);
			return true;
		} else {
			NVT_LOG("address is not avail 0x:%x\n", addr);
			return false;
		}
		break;
	// case 0x2FE00:
	// 	if (!ts->enable_touch_raw) {
	// 		NVT_LOG("address is avail 0x:%x\n", addr);
	// 		return true;
	// 	} else {
	// 		NVT_LOG("address is not avail 0x:%x\n", addr);
	// 		return false;
	// 	}
	// 	break;
#else
	case EVENT_BUF_ADDR :
		NVT_LOG("address is avail 0x:%x\n", addr);
		return true;
		break;
#endif


	default:
		NVT_LOG("address is not avail 0x:%x\n", addr);
	}
	return false;
}
//thp end 6.8

#ifdef CONFIG_MTK_SPI
const struct mt_chip_conf spi_ctrdata = {
	.setuptime = 25,
	.holdtime = 25,
	.high_time = 5,	/* 10MHz (SPI_SPEED=100M / (high_time+low_time(10ns)))*/
	.low_time = 5,
	.cs_idletime = 2,
	.ulthgh_thrsh = 0,
	.cpol = 0,
	.cpha = 0,
	.rx_mlsb = 1,
	.tx_mlsb = 1,
	.tx_endian = 0,
	.rx_endian = 0,
	.com_mod = DMA_TRANSFER,
	.pause = 0,
	.finish_intr = 1,
	.deassert = 0,
	.ulthigh = 0,
	.tckdly = 0,
};
#endif

#ifdef CONFIG_SPI_MT65XX
const struct mtk_chip_config spi_ctrdata = {
    .rx_mlsb = 1,
    .tx_mlsb = 1,
    .cs_pol = 0,
};
#endif

static uint8_t bTouchIsAwake = 0;

/* MIPP Start */
#if !TOUCH_THP_SUPPORT
void nvt_pen_data_collect(struct nvt_pen_press* press)
{
	struct nvt_pen_pdata* pdata = NULL;
	struct nvt_pen_press_buff* press_buff = NULL;

	if(IS_ERR_OR_NULL(ts->pen_pdata)) {
        NVT_ERR("MIPP padta has not been registered\n");
        return;
    }
	pdata = ts->pen_pdata;

	spin_lock(&pdata->spinlock);
	press_buff = pdata->buff;
	memcpy(&press_buff->buff[press_buff->head], press, sizeof(struct nvt_pen_press));
	press_buff->head++;
	press_buff->head &= MIPP_MAX_BUFFER_LENGTH - 1;
	spin_unlock(&pdata->spinlock);
}

unsigned int nvt_pen_press_get(void)
{
	int read_pos;
	unsigned int pressure;
	struct nvt_pen_pdata* pdata = NULL;
	struct nvt_pen_press_buff* press_buff = NULL;

	/*attention*/
	if(IS_ERR_OR_NULL(ts->pen_pdata)) {
        NVT_ERR("MIPP pdata has not been registered\n");
        return 0;
    }
	pdata = ts->pen_pdata;

	spin_lock(&pdata->spinlock);
	press_buff = pdata->buff;
	if(press_buff->head) {
		read_pos = press_buff->head - 1;
	} else {
		read_pos = MIPP_MAX_BUFFER_LENGTH - 1;
	}
	pressure = press_buff->buff[read_pos].pressure;
	spin_unlock(&pdata->spinlock);

	return pressure;
}

long long nvt_time_stamp_transfer(char* data, int len)
{
	int i = 0;
	long long time = 0;

	len = (len > 8) ? 8 : len;

	for(i = 0; i < (len / 2); i++) {
		time |= data[i * 2 + 1];
		time <<= 8;
		time |= data[i * 2];
		if(i < (len / 2 - 1)) {
			time <<= 8;
		}
	}

	//NVT_LOG("%lld\n", time);
	return time;
}

static ssize_t nvt_pen_fops_write(struct file *file, const char __user *user, size_t size, loff_t *loff)
{
	int copy = 0;
	struct nvt_pen_press press;
	char data[MIPP_PEN_DATA_LENGTH] = {0};

	copy = min(size, (size_t)MIPP_PEN_DATA_LENGTH);
	if(copy_from_user(data, user, copy)){
		return -EFAULT;
	}

	if(data[0] != 0x05) {
		goto err_data_format;
	}
	press.time_stamp = \
		nvt_time_stamp_transfer(&data[MIPP_PEN_TIME_OFFSET], MIPP_PEN_TIME_LENGTH);
	press.pressure = ((unsigned int)data[2] << 8) | data[1];
    //pressure collect
	nvt_pen_data_collect(&press);

err_data_format:
	return 0;
}

static const struct file_operations nvt_pen_fops = {
	.owner = THIS_MODULE,
	.write = nvt_pen_fops_write,
};
#endif

static void nvt_pen_hopping_frequency(uint8_t pen_id, uint8_t pen_hopping_frequency)
{
	int cmd = 0;
	char* mesg[2];
	char cmd_str[MIPP_MAX_UEVENT_LENGTH];
	struct device* device = NULL;

	if(IS_ERR_OR_NULL(ts->client)) {
        NVT_ERR("spi client has not been registered\n");
        return;
    }

	cmd = ((int)MIPP_PEN_FREQUENCY << 16) | ((pen_id & 0xFF) << 8) | (pen_hopping_frequency & 0xFF);
	if(snprintf(cmd_str, sizeof(cmd_str), "MIPP_PEN_STATE=%d", cmd) < 0) {
		NVT_ERR("failed to copy cmd\n");
		return;
	}

	mesg[0] = cmd_str;
	mesg[1] = NULL;

    device = &ts->client->dev;
	kobject_uevent_env(&device->kobj, KOBJ_CHANGE, mesg);
	NVT_LOG("send pen hopping cmd: %x\n", cmd);
}

static void nvt_pen_AG_update(uint8_t pen_id, uint8_t state)
{
	int cmd = 0;
	char* mesg[2];
	char cmd_str[MIPP_MAX_UEVENT_LENGTH];
	struct device* device = NULL;

	if (IS_ERR_OR_NULL(ts->client)) {
        NVT_ERR("spi client has not been registered\n");
        return;
    }

	cmd = ((int)MIPP_PEN_AG << 16) | ((pen_id & 0xFF) << 8) | (state & 0xFF);
	if (snprintf(cmd_str, sizeof(cmd_str), "MIPP_PEN_STATE=%d", cmd) < 0) {
		NVT_ERR("failed to copy cmd\n");
		return;
	}

	mesg[0] = cmd_str;
	mesg[1] = NULL;

	device = &ts->client->dev;
	kobject_uevent_env(&device->kobj, KOBJ_CHANGE, mesg);
	NVT_LOG("send pen AG cmd: %x\n", cmd);
}

static void nvt_pen_report_status_update(uint8_t pen_id, uint8_t state)
{
	int cmd = 0;
	char* mesg[2];
	char cmd_str[MIPP_MAX_UEVENT_LENGTH];
	struct device* device = NULL;

	if (IS_ERR_OR_NULL(ts->client)) {
		NVT_ERR("spi client has not been registered\n");
		return;
	}

	cmd = ((int)MIPP_PEN_REPORT << 16) | ((pen_id & 0xFF) << 8) | (state & 0xFF);
	if (snprintf(cmd_str, sizeof(cmd_str), "MIPP_PEN_STATE=%d", cmd) < 0) {
		NVT_ERR("failed to copy cmd\n");
		return;
	}

	mesg[0] = cmd_str;
	mesg[1] = NULL;

	device = &ts->client->dev;
	kobject_uevent_env(&device->kobj, KOBJ_CHANGE, mesg);
	NVT_LOG("send pen report status cmd: %x\n", cmd);
}

#if !TOUCH_THP_SUPPORT
static int nvt_pen_device_register(struct nvt_ts_data* nvt_data)
{
	int ret = 0;
    struct device* dev = &nvt_data->client->dev;
    struct nvt_pen_pdata* pdata = NULL;
    struct nvt_pen_device* device = NULL;

	pdata = devm_kzalloc(dev, sizeof(struct nvt_pen_pdata), GFP_KERNEL);
	if (pdata == NULL) {
		NVT_ERR("MIPP failed to allocated pdata\n");
		ret = -1;
		goto err_malloc_pdata;
	}

    spin_lock_init(&pdata->spinlock);

	pdata->dev = devm_kzalloc(dev, sizeof(struct nvt_pen_device), GFP_KERNEL);
	if(pdata->dev == NULL) {
		NVT_ERR("MIPP failed to allocated device\n");
		ret = -1;
		goto err_malloc_dev;
	}

    pdata->buff = devm_kzalloc(dev, sizeof(struct nvt_pen_press_buff), GFP_KERNEL);
	if(pdata->buff == NULL) {
		NVT_ERR("MIPP failed to allocated buffer\n");
		ret = -1;
		goto err_malloc_buff;
	}

    device = pdata->dev;
	device->dev = NULL;
	device->class.name  = "mipp_pen";
	device->class.owner = THIS_MODULE;

	ret = class_register(&device->class);
	if(ret < 0) {
		NVT_LOG("MIPP class register fail\n");
		goto err_register_class;
	}

	if(alloc_chrdev_region(&device->basedev, 0, 1, "mipp_pen") < 0) {
		NVT_LOG("MIPP alloc region fail\n");
		ret = -1;
		goto err_alloc_region;
	}

	cdev_init(&device->cdev, &nvt_pen_fops);
	if(cdev_add(&device->cdev, (device->basedev + 0), 1)) {
		NVT_LOG("MIPP add character device fail\n");
		ret = -1;
		goto err_add_cdev;
	}

	device->dev = device_create(&device->class, NULL, device->basedev + 0, NULL, "mipp_pen0");
	if(IS_ERR_OR_NULL(device->dev)) {
		NVT_LOG("MIPP create device fail\n");
		ret = -1;
		goto err_device_create;
	}

    nvt_data->pen_pdata = pdata;
	NVT_LOG("MIPP pen device register success\n");
	return 0;

err_device_create:
	cdev_del(&device->cdev);
err_add_cdev:
	unregister_chrdev_region(device->basedev, 1);
err_alloc_region:
	class_destroy(&device->class);
err_register_class:
    device = NULL;
err_malloc_buff:
	if(pdata->buff) {
		devm_kfree(dev, pdata->buff);
		pdata->buff = NULL;
	}
err_malloc_dev:
    if(pdata->dev) {
        devm_kfree(dev, pdata->dev);
        pdata->dev = NULL;
    }
err_malloc_pdata:
	if(pdata) {
		devm_kfree(dev, pdata);
		pdata = NULL;
	}
	return ret;
}

static void nvt_pen_device_release(struct nvt_ts_data* nvt_data)
{
    struct device* dev = &nvt_data->client->dev;
	struct nvt_pen_pdata* pdata = NULL;
    struct nvt_pen_device* device = NULL;

	pdata = nvt_data->pen_pdata;
    device = nvt_data->pen_pdata->dev;
	device_destroy(&device->class, device->basedev + 0);
	cdev_del(&device->cdev);
	unregister_chrdev_region(device->basedev, 1);
	class_destroy(&device->class);
    if(pdata->buff) {
        devm_kfree(dev, pdata->buff);
        pdata->buff = NULL;
    }

    device = NULL;
    if(pdata->dev) {
        devm_kfree(dev, pdata->dev);
        pdata->dev = NULL;
    }

	if(pdata) {
		devm_kfree(dev, pdata);
		pdata = NULL;
	}
	NVT_LOG("MIPP device release success\n");
}
#endif
/* MIPP End */

/*******************************************************
Description:
	Novatek touchscreen irq enable/disable function.

return:
	n.a.
*******************************************************/
static void nvt_irq_enable(bool enable)
{
	struct irq_desc *desc;

	if (enable) {
		if (!ts->irq_enabled) {
			enable_irq(ts->client->irq);
			ts->irq_enabled = true;
		}
	} else {
		if (ts->irq_enabled) {
			disable_irq(ts->client->irq);
			ts->irq_enabled = false;
		}
	}

	desc = irq_to_desc(ts->client->irq);
	NVT_LOG("enable=%d, desc->depth=%d\n", enable, desc->depth);
}

/*******************************************************
Description:
	Novatek touchscreen spi read/write core function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static inline int32_t spi_read_write(struct spi_device *client, uint8_t *buf, size_t len , NVT_SPI_RW rw)
{
	struct spi_message m;
	struct spi_transfer t = {
		.len    = len,
	};

	memset(ts->xbuf, 0, len + DUMMY_BYTES);
	memcpy(ts->xbuf, buf, len);

	switch (rw) {
		case NVTREAD:
			t.tx_buf = ts->xbuf;
			t.rx_buf = ts->rbuf;
			t.len    = (len + DUMMY_BYTES);
			break;

		case NVTWRITE:
			t.tx_buf = ts->xbuf;
			break;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(client, &m);
}

/*******************************************************
Description:
	Novatek touchscreen spi read function.

return:
	Executive outcomes. 2---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;

	mutex_lock(&ts->xbuf_lock);

	buf[0] = SPI_READ_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NVTREAD);
		if (ret == 0) break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		NVT_ERR("read error, ret=%d\n", ret);
		ret = -EIO;
	} else {
		memcpy((buf+1), (ts->rbuf+2), (len-1));
	}

	mutex_unlock(&ts->xbuf_lock);

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen spi write function.

return:
	Executive outcomes. 1---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;

	mutex_lock(&ts->xbuf_lock);

	buf[0] = SPI_WRITE_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NVTWRITE);
		if (ret == 0)	break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		NVT_ERR("error, ret=%d\n", ret);
		ret = -EIO;
	}

	mutex_unlock(&ts->xbuf_lock);

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen set index/page/addr address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_set_page(uint32_t addr)
{
	uint8_t buf[4] = {0};

	buf[0] = 0xFF;	//set index/page/addr command
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;

	return CTP_SPI_WRITE(ts->client, buf, 3);
}

/*******************************************************
Description:
	Novatek touchscreen write data to specify address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_write_addr(uint32_t addr, uint8_t data)
{
	int32_t ret = 0;
	uint8_t buf[4] = {0};

	//---set xdata index---
	buf[0] = 0xFF;	//set index/page/addr command
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret) {
		NVT_ERR("set page 0x%06X failed, ret = %d\n", addr, ret);
		return ret;
	}

	//---write data to index---
	buf[0] = addr & (0x7F);
	buf[1] = data;
	ret = CTP_SPI_WRITE(ts->client, buf, 2);
	if (ret) {
		NVT_ERR("write data to 0x%06X failed, ret = %d\n", addr, ret);
		return ret;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen read value to specific register.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_read_reg(nvt_ts_reg_t reg, uint8_t *val)
{
	int32_t ret = 0;
	uint32_t addr = 0;
	uint8_t mask = 0;
	uint8_t shift = 0;
	uint8_t buf[8] = {0};
	uint8_t temp = 0;

	addr = reg.addr;
	mask = reg.mask;
	/* get shift */
	temp = reg.mask;
	shift = 0;
	while (1) {
		if ((temp >> shift) & 0x01)
			break;
		if (shift == 8) {
			NVT_ERR("mask all bits zero!\n");
			ret = -1;
			break;
		}
		shift++;
	}
	/* read the byte of the register is in */
	nvt_set_page(addr);
	buf[0] = addr & 0xFF;
	buf[1] = 0x00;
	ret = CTP_SPI_READ(ts->client, buf, 2);
	if (ret < 0) {
		NVT_ERR("CTP_SPI_READ failed!(%d)\n", ret);
		goto nvt_read_register_exit;
	}
	/* get register's value in its field of the byte */
	*val = (buf[1] & mask) >> shift;

nvt_read_register_exit:
	return ret;
}


//thp start 6.8
#define MAX_DATA_BUF 256
static int16_t integer_conver(u8* value)
{
    char* p;
    int16_t result = 0;
	NVT_LOG("nvt integer is %s\n", value);
	for (p = value; *p != '\0'; p++) {
		if (*p >= '0' && *p <= '9') {
			result = result * 10 + (*p - '0');
		} else {
			NVT_LOG("integer is %d\n", result);
            return result;
        }
    }
	NVT_LOG("integer is %d\n", result);
    return result;
}

static void nvt_parse_open_data(u8* buf, u8* input)
{
	const char* p = buf;
	bool new_data = false;
	int i = 0;
	int para_cnt = 0;
	int temp = 0;

	memset(input, 0x00, MAX_DATA_BUF);
	for (p = buf; *p != '\0'; p++) {
		if (*p >= '0' && *p <= '9') {
			temp = input[i] * 10 + (*p - '0');
			if (temp > 255)
				temp = 255;
			input[i] = temp;
			if (!new_data) {
				new_data = true;
				para_cnt++;
			}
		} else if (*p == ',' || *p == ' ') {
			if (new_data) {
				i++;
				new_data = false;
			}
		} else {
			break;
		}
	}

	for (i = 0; i < para_cnt; i++)
		NVT_LOG("zhangyanan input[i:%d]:%d \n", i, input[i]);

}

extern int32_t nvt_set_extend_custom_cmd(uint8_t sub_cmd, int16_t value);
static int nvt_thp_ic_write_interfaces(u_int32_t addr, u8* value, int value_len)
{
    int i;
	int ret = 1;
	u8 input_data[MAX_DATA_BUF] = {0};
	int16_t input = 0;

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	NVT_LOG("++\n");
	mutex_lock(&ts->lock);

    if (htc_ic_mode == IC_MODE_44) {
		if (check_address(addr)) {
			nvt_set_page(addr);
		} else {
			ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
		}
		nvt_parse_open_data(value, input_data);
		for(i = 0; i < value_len; i++)
			NVT_LOG("zhangyanan input[i:%d]:%d \n", i, input_data[i]);
		ret = CTP_SPI_WRITE(ts->client, input_data, value_len);
    } else {
		input = integer_conver(value);
		NVT_LOG("cmd: 0x%x input:%d\n", addr & 0xFF, input);
		nvt_set_extend_custom_cmd(addr & 0xFF, input);
	}

	mutex_unlock(&ts->lock);
	NVT_LOG("--\n");
    return ret;
}

static int nvt_thp_ic_read_interfaces(u_int32_t addr, u8* value, int value_len)
{
    int ret;

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

    if (htc_ic_mode != IC_MODE_44) {
        value_len = 2;
    }

	NVT_LOG("++\n");
	mutex_lock(&ts->lock);

	if (check_address(addr)) {
		nvt_set_page(addr);
	} else {
		ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	}

    ret = CTP_SPI_READ(ts->client, value, (int16_t)value_len);	//read data_len history

    //print all data
    NVT_LOG("read spi common command addr: 0x%x data_len:%d \n", addr, value_len);
    if (ret < 0) {
        NVT_LOG("thp ic read is failed!!\n");
        ret = -1;
		goto out;
    }

    NVT_LOG("mode:%d, addr:%x, readbuf[0]:%x, readbuf[1]:%x\n", htc_ic_mode, addr, value[0], value[1]);

out:
	mutex_unlock(&ts->lock);
	NVT_LOG("--\n");

    return ret;
}

int nvt_htc_ic_setModeValue(common_data_t *common_data)
{
	int i = 0;
	int mode = common_data->mode;
	u8 *value = (u8*) common_data->data_buf;
	int value_len = common_data->data_len;
	u_int32_t addr;
	int ret = 0;

	addr = ts->mmap->EVENT_BUF_ADDR;
    NVT_LOG("mode:%d, value:%s, value_len:%d", mode, value, value_len);
    for (i = 0; i < value_len; i++) {
        NVT_LOG("value[i:%d]:%d", i, value[i]);
    }
    if (mode < 3000) {
        NVT_ERR("mode is error!!\n");
        return -1;
    }

    htc_ic_mode = mode;
    switch (mode)
    {
    case IC_MODE_0:
        ret = nvt_thp_ic_write_interfaces(0x02, value, value_len);
        break;
    case IC_MODE_1:
        ret = nvt_thp_ic_write_interfaces(0xff, value, value_len);
        break;
    case IC_MODE_2:
        ret = nvt_thp_ic_write_interfaces(0xff, value, value_len);
        break;
    case IC_MODE_3:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_4:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_5:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_6:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_7:
        ret = nvt_thp_ic_write_interfaces(0x09, value, value_len);
        break;
    case IC_MODE_8:
        ret = nvt_thp_ic_write_interfaces(0x08, value, value_len);
        break;
    case IC_MODE_9:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_10:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_11:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_12:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_13:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_14:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_15:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_16:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_17:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_18:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_19:
        ret = nvt_thp_ic_write_interfaces(0x10, value, value_len);
        break;
    case IC_MODE_20:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_21:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_22:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_23:
        ret = nvt_thp_ic_write_interfaces(0x11, value, value_len);
        break;
    case IC_MODE_24:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_25:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_26:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_27:
        ret = nvt_thp_ic_write_interfaces(0x05, value, value_len);
        break;
    case IC_MODE_28:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_29:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_30:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_31:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_32:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_33:
        ret = nvt_thp_ic_write_interfaces(0x13, value, value_len);
        break;
    case IC_MODE_34:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_35:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_36:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_37:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_38:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_39:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_40:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_41:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    case IC_MODE_42:
        ret = nvt_thp_ic_write_interfaces(0x14, value, value_len);
        break;
    case IC_MODE_43:
        ret = nvt_thp_ic_write_interfaces(0x06, value, value_len);
        break;
    case IC_MODE_44:
        ret = nvt_thp_ic_write_interfaces(addr, value, value_len);
        break;
    default:
        break;
    }

    return ret;
}

int nvt_htc_ic_getModeValue(common_data_t *common_data)
{
    u_int32_t addr;
	int mode = common_data->mode;
	u8 *value = (u8*) common_data->data_buf;
	int value_len = common_data->data_len;

	addr = ts->mmap->EVENT_BUF_ADDR;
	NVT_LOG("mode:%d, value:%s, value_len:%d", mode, value, value_len);
    if (mode < 3000) {
        NVT_ERR("mode is error!!\n");
        return -1;
    }
    htc_ic_mode = mode;
    switch (mode)
    {
    case IC_MODE_0:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_1:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_2:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_3:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_4:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_5:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_6:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_7:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_8:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_9:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_10:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_11:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_12:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_13:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_14:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_15:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_16:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_17:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_18:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_19:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_20:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_21:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_22:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_23:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_24:
        nvt_thp_ic_read_interfaces(0x12, value, value_len);
        break;
    case IC_MODE_25:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_26:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_27:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_28:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_29:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_30:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_31:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_32:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_33:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_34:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_35:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_36:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_37:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_38:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_39:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_40:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_41:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_42:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_43:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    case IC_MODE_44:
        nvt_thp_ic_read_interfaces(addr, value, value_len);
        break;
    default:
        break;
    }

    return 0;
}
//thp end 6.8

/*******************************************************
Description:
	Novatek touchscreen clear status & enable fw crc function.

return:
	N/A.
*******************************************************/
void nvt_fw_crc_enable(void)
{
	uint8_t buf[8] = {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	//---clear fw reset status---
	buf[0] = EVENT_MAP_RESET_COMPLETE & (0x7F);
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x00;
	CTP_SPI_WRITE(ts->client, buf, 7);

	//---enable fw crc---
	buf[0] = EVENT_MAP_HOST_CMD & (0x7F);
	buf[1] = 0xAE;	//enable fw crc command
	buf[2] = 0x00;
	CTP_SPI_WRITE(ts->client, buf, 3);
}

/*******************************************************
Description:
	Novatek touchscreen set boot ready function.

return:
	N/A.
*******************************************************/
void nvt_boot_ready(void)
{
	//---write BOOT_RDY status cmds---
	nvt_write_addr(ts->mmap->BOOT_RDY_ADDR, 1);

	mdelay(5);

	if (ts->hw_crc == HWCRC_NOSUPPORT) {
		//---write BOOT_RDY status cmds---
		nvt_write_addr(ts->mmap->BOOT_RDY_ADDR, 0);

		//---write POR_CD cmds---
		nvt_write_addr(ts->mmap->POR_CD_ADDR, 0xA0);
	}
}

/*******************************************************
Description:
	Novatek touchscreen enable auto copy mode function.

return:
	N/A.
*******************************************************/
void nvt_tx_auto_copy_mode(void)
{
	if (ts->auto_copy == CHECK_SPI_DMA_TX_INFO) {
		//---write TX_AUTO_COPY_EN cmds---
		nvt_write_addr(ts->mmap->TX_AUTO_COPY_EN, 0x69);
	} else if (ts->auto_copy == CHECK_TX_AUTO_COPY_EN) {
		//---write SPI_MST_AUTO_COPY cmds---
		nvt_write_addr(ts->mmap->TX_AUTO_COPY_EN, 0x56);
	}

	NVT_ERR("tx auto copy mode %d enable\n", ts->auto_copy);
}

/*******************************************************
Description:
	Novatek touchscreen check spi dma tx info function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
int32_t nvt_check_spi_dma_tx_info(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 200;

	if (ts->mmap->SPI_DMA_TX_INFO == 0) {
		NVT_ERR("error, SPI_DMA_TX_INFO = 0\n");
		return -1;
	}

	for (i = 0; i < retry; i++) {
		//---set xdata index to SPI_DMA_TX_INFO---
		nvt_set_page(ts->mmap->SPI_DMA_TX_INFO);

		//---read spi dma status---
		buf[0] = ts->mmap->SPI_DMA_TX_INFO & 0x7F;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check tx auto copy state function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
int32_t nvt_check_tx_auto_copy(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 200;

	if (ts->mmap->TX_AUTO_COPY_EN == 0) {
		NVT_ERR("error, TX_AUTO_COPY_EN = 0\n");
		return -1;
	}

	for (i = 0; i < retry; i++) {
		//---set xdata index to SPI_MST_AUTO_COPY---
		nvt_set_page(ts->mmap->TX_AUTO_COPY_EN);

		//---read auto copy status---
		buf[0] = ts->mmap->TX_AUTO_COPY_EN & 0x7F;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen wait auto copy finished function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
int32_t nvt_wait_auto_copy(void)
{
	if (ts->auto_copy == CHECK_SPI_DMA_TX_INFO) {
		return nvt_check_spi_dma_tx_info();
	} else if (ts->auto_copy == CHECK_TX_AUTO_COPY_EN) {
		return nvt_check_tx_auto_copy();
	} else {
		NVT_ERR("failed, not support mode %d!\n", ts->auto_copy);
		return -1;
	}
}

/*******************************************************
Description:
	Novatek touchscreen eng reset cmd
    function.

return:
	n.a.
*******************************************************/
void nvt_eng_reset(void)
{
	//---eng reset cmds to ENG_RST_ADDR---
	nvt_write_addr(ENG_RST_ADDR, 0x5A);

	mdelay(1);	//wait tMCU_Idle2TP_REX_Hi after TP_RST
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU
    function.

return:
	n.a.
*******************************************************/
void nvt_sw_reset(void)
{
	//---software reset cmds to SWRST_SIF_ADDR---
	nvt_write_addr(ts->swrst_sif_addr, 0x55);

	msleep(10);
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU then into idle mode
    function.

return:
	n.a.
*******************************************************/
void nvt_sw_reset_idle(void)
{
	//---MCU idle cmds to SWRST_SIF_ADDR---
	nvt_write_addr(ts->swrst_sif_addr, 0xAA);

	msleep(15);
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU (boot) function.

return:
	n.a.
*******************************************************/
void nvt_bootloader_reset(void)
{
	//---reset cmds to SWRST_SIF_ADDR---
	nvt_write_addr(ts->swrst_sif_addr, 0x69);

	mdelay(5);	//wait tBRST2FR after Bootload RST

	if (SPI_RD_FAST_ADDR) {
		/* disable SPI_RD_FAST */
		nvt_write_addr(SPI_RD_FAST_ADDR, 0x00);
	}

	NVT_LOG("end\n");
}

/*******************************************************
Description:
	Novatek touchscreen clear FW status function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
int32_t nvt_clear_fw_status(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 20;

	for (i = 0; i < retry; i++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		//---clear fw status---
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_WRITE(ts->client, buf, 2);

		//---read fw status---
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check FW status function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
int32_t nvt_check_fw_status(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 50;

	usleep_range(20000, 20000);

	for (i = 0; i < retry; i++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		//---read fw status---
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 2);

		if ((buf[1] & 0xF0) == 0xA0)
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check FW reset state function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;
	int32_t retry = 0;
	int32_t retry_max = (check_reset_state == RESET_STATE_INIT) ? 10 : 50;

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE);

	while (1) {
		//---read reset state---
		buf[0] = EVENT_MAP_RESET_COMPLETE;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 6);

		if ((buf[1] >= check_reset_state) && (buf[1] <= RESET_STATE_MAX)) {
			ret = 0;
			break;
		}

		retry++;
		if(unlikely(retry > retry_max)) {
			NVT_ERR("error, retry=%d, buf[1]=0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
				retry, buf[1], buf[2], buf[3], buf[4], buf[5]);
			ret = -1;
			break;
		}

		usleep_range(10000, 10000);
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen get firmware related information
	function.

return:
	Executive outcomes. 0---success. -1---fail.
*******************************************************/
int32_t nvt_get_fw_info(void)
{
	uint8_t buf[64] = {0};
	uint32_t retry_count = 0;
	int32_t ret = 0;

info_retry:
	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);

	//---read fw info---
	buf[0] = EVENT_MAP_FWINFO;
	CTP_SPI_READ(ts->client, buf, 39);
	if ((buf[1] + buf[2]) != 0xFF) {
		NVT_ERR("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n", buf[1], buf[2]);
		if (retry_count < 3) {
			retry_count++;
			NVT_ERR("retry_count=%d\n", retry_count);
			goto info_retry;
		} else {
			ts->fw_ver = 0;
			ts->max_button_num = TOUCH_KEY_NUM;
			NVT_ERR("Set default fw_ver=%d, max_button_num=%d!\n",
					ts->fw_ver, ts->max_button_num);
			ret = -1;
			goto out;
		}
	}
	ts->fw_ver = buf[1];
	ts->x_num = buf[3];
	ts->y_num = buf[4];
	ts->max_button_num = buf[11];
	ts->fw_type = buf[14];
	ts->nvt_pid = (uint16_t)((buf[36] << 8) | buf[35]);
	if (ts->pen_support) {
		ts->x_gang_num = buf[37];
		ts->y_gang_num = buf[38];
	}
	NVT_LOG("fw_ver=0x%02X, fw_type=0x%02X, PID=0x%04X\n", ts->fw_ver, ts->fw_type, ts->nvt_pid);

	ret = 0;
out:

	return ret;
}

//thp start 6.8
/*******************************************************
Description:
	get xiaomi thp poll info 

return:
*******************************************************/
#if TOUCH_THP_SUPPORT
#define XM_HTC_POLL_INFO_LEN 39
int32_t nvt_get_xm_htc_poll_info(void)
{
	int32_t ret;
	uint8_t buf[XM_HTC_POLL_INFO_LEN + 1 + DUMMY_BYTES] = {0};
	int32_t i;

	nvt_set_page(ts->mmap->XM_HTC_POLL_INFO_ADDR);
	buf[0] = ts->mmap->XM_HTC_POLL_INFO_ADDR & 0x7F;
	ret = CTP_SPI_READ(ts->client, buf, XM_HTC_POLL_INFO_LEN + 1);
	if (ret) {
		NVT_ERR("CTP_SPI_READ failed!(%d)\n", ret);
		goto out;
	}
	// ToDo: memcpy(xm_htc_poll_info_buf, buf + 1, sizeof(struct xm_htc_poll_info));

	ts->frame_len = *((uint16_t *)(buf + 1 + 2));
	if (ts->frame_len > XM_HTC_DEFAULT_FRAME_LEN) {
		NVT_ERR("get frame_len = %d > %d! set frame_len = %d.\n",
				ts->frame_len, XM_HTC_DEFAULT_FRAME_LEN, XM_HTC_DEFAULT_FRAME_LEN);
		ts->frame_len = XM_HTC_DEFAULT_FRAME_LEN;
	}

	// temporarily debug print
	NVT_LOG("nvt-ts: frame_len = %d  Xiaomi Host Touch Polling Info \n", ts->frame_len);
	for (i = 0; i < XM_HTC_POLL_INFO_LEN; i++) {
		if ((i + 1) % 8 == 0)
			NVT_LOG("%02X  ", buf[1 + i]);
		else
			NVT_LOG("%02X  ", buf[1 + i]);
	}
	NVT_LOG("\n");
	// temporarily debug print

	ret = 0;
out:
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	return ret;
}
#endif /* #if TOUCH_THP_SUPPORT */
//thp end 6.8

/*******************************************************
  Create Device Node (Proc Entry)
*******************************************************/
#if NVT_TOUCH_PROC
static struct proc_dir_entry *NVT_proc_entry;
#define DEVICE_NAME	"NVTSPI"

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI read function.

return:
	Executive outcomes. 2---succeed. -5,-14---failed.
*******************************************************/
static ssize_t nvt_flash_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	uint8_t *str = NULL;
	int32_t ret = 0;
	int32_t retries = 0;
	int8_t spi_wr = 0;
	uint8_t *buf;

	if ((count > NVT_TRANSFER_LEN + 3) || (count < 3)) {
		NVT_ERR("invalid transfer len!\n");
		return -EFAULT;
	}

	/* allocate buffer for spi transfer */
	str = (uint8_t *)kzalloc((count), GFP_KERNEL);
	if(str == NULL) {
		NVT_ERR("kzalloc for buf failed!\n");
		ret = -ENOMEM;
		goto kzalloc_failed;
	}

	buf = (uint8_t *)kzalloc((count), GFP_KERNEL | GFP_DMA);
	if(buf == NULL) {
		NVT_ERR("kzalloc for buf failed!\n");
		ret = -ENOMEM;
		kfree(str);
		str = NULL;
		goto kzalloc_failed;
	}

	if (copy_from_user(str, buff, count)) {
		NVT_ERR("copy from user error\n");
		ret = -EFAULT;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	/*
	 * stop esd check work to avoid case that 0x77 report righ after here to enable esd check again
	 * finally lead to trigger esd recovery bootloader reset
	 */
	cancel_delayed_work_sync(&nvt_esd_check_work);
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	spi_wr = str[0] >> 7;
	memcpy(buf, str+2, ((str[0] & 0x7F) << 8) | str[1]);

	if (spi_wr == NVTWRITE) {	//SPI write
		while (retries < 20) {
			ret = CTP_SPI_WRITE(ts->client, buf, ((str[0] & 0x7F) << 8) | str[1]);
			if (!ret)
				break;
			else
				NVT_ERR("error, retries=%d, ret=%d\n", retries, ret);

			retries++;
		}

		if (unlikely(retries == 20)) {
			NVT_ERR("error, ret = %d\n", ret);
			ret = -EIO;
			goto out;
		}
	} else if (spi_wr == NVTREAD) {	//SPI read
		while (retries < 20) {
			ret = CTP_SPI_READ(ts->client, buf, ((str[0] & 0x7F) << 8) | str[1]);
			if (!ret)
				break;
			else
				NVT_ERR("error, retries=%d, ret=%d\n", retries, ret);

			retries++;
		}

		memcpy(str+2, buf, ((str[0] & 0x7F) << 8) | str[1]);
		// copy buff to user if spi transfer
		if (retries < 20) {
			if (copy_to_user(buff, str, count)) {
				ret = -EFAULT;
				goto out;
			}
		}

		if (unlikely(retries == 20)) {
			NVT_ERR("error, ret = %d\n", ret);
			ret = -EIO;
			goto out;
		}
	} else {
		NVT_ERR("Call error, str[0]=%d\n", str[0]);
		ret = -EFAULT;
		goto out;
	}

out:
	kfree(str);
    kfree(buf);
kzalloc_failed:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI open function.

return:
	Executive outcomes. 0---succeed. -12---failed.
*******************************************************/
static int32_t nvt_flash_open(struct inode *inode, struct file *file)
{
	struct nvt_flash_data *dev;

	dev = kmalloc(sizeof(struct nvt_flash_data), GFP_KERNEL);
	if (dev == NULL) {
		NVT_ERR("Failed to allocate memory for nvt flash data\n");
		return -ENOMEM;
	}

	ts->nvt_tool_in_use = true;

	rwlock_init(&dev->lock);
	file->private_data = dev;

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI close function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_flash_close(struct inode *inode, struct file *file)
{
	struct nvt_flash_data *dev = file->private_data;

	if (dev)
		kfree(dev);

	ts->nvt_tool_in_use = false;

	return 0;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_flash_fops = {
	.proc_open = nvt_flash_open,
	.proc_release = nvt_flash_close,
	.proc_read = nvt_flash_read,
};
#else
static const struct file_operations nvt_flash_fops = {
	.owner = THIS_MODULE,
	.open = nvt_flash_open,
	.release = nvt_flash_close,
	.read = nvt_flash_read,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI initial function.

return:
	Executive outcomes. 0---succeed. -12---failed.
*******************************************************/
static int32_t nvt_flash_proc_init(void)
{
	NVT_proc_entry = proc_create(DEVICE_NAME, 0444, NULL,&nvt_flash_fops);
	if (NVT_proc_entry == NULL) {
		NVT_ERR("Failed!\n");
		return -ENOMEM;
	} else {
		NVT_LOG("Succeeded!\n");
	}

	NVT_LOG("============================================================\n");
	NVT_LOG("Create /proc/%s\n", DEVICE_NAME);
	NVT_LOG("============================================================\n");

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI deinitial function.

return:
	n.a.
*******************************************************/
static void nvt_flash_proc_deinit(void)
{
	if (NVT_proc_entry != NULL) {
		remove_proc_entry(DEVICE_NAME, NULL);
		NVT_proc_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", DEVICE_NAME);
	}
}
#endif

#if WAKEUP_GESTURE
#define GESTURE_WORD_C          12
#define GESTURE_WORD_W          13
#define GESTURE_WORD_V          14
#define GESTURE_DOUBLE_CLICK    15
#define GESTURE_WORD_Z          16
#define GESTURE_WORD_M          17
#define GESTURE_WORD_O          18
#define GESTURE_WORD_e          19
#define GESTURE_WORD_S          20
#define GESTURE_SLIDE_UP        21
#define GESTURE_SLIDE_DOWN      22
#define GESTURE_SLIDE_LEFT      23
#define GESTURE_SLIDE_RIGHT     24
#define GESTURE_SINGLE_CLICK    25
#define PEN_GESTURE_SINGLE_CLICK 25
/* customized gesture id */
#define DATA_PROTOCOL           30

/* function page definition */
#define FUNCPAGE_GESTURE         1

/*******************************************************
Description:
	Novatek touchscreen wake up gesture key report function.

return:
	n.a.
*******************************************************/
void nvt_ts_wakeup_gesture_report(uint8_t gesture_id, uint8_t *data)
{
	uint32_t keycode = 0;
	uint8_t func_type = data[2];
	uint8_t func_id = data[3];

	/* support fw specifal data protocol */
	if ((gesture_id == DATA_PROTOCOL) && (func_type == FUNCPAGE_GESTURE)) {
		gesture_id = func_id;
	} else if (gesture_id > DATA_PROTOCOL) {
		//NVT_ERR("gesture_id %d is invalid, func_type=%d, func_id=%d\n", gesture_id, func_type, func_id);
		return;
	}

	//NVT_LOG("gesture_id = %d\n", gesture_id);

	switch (gesture_id) {
		case GESTURE_WORD_C:
			NVT_LOG("Gesture : Word-C.\n");
			keycode = gesture_key_array[0];
			break;
		case GESTURE_WORD_W:
			NVT_LOG("Gesture : Word-W.\n");
			keycode = gesture_key_array[1];
			break;
		case GESTURE_WORD_V:
			NVT_LOG("Gesture : Word-V.\n");
			keycode = gesture_key_array[2];
			break;
		case GESTURE_DOUBLE_CLICK:
			NVT_LOG("Gesture : Double Click.\n");
			if (ts->gesture_command & 0x01) {
				keycode = gesture_key_array[3];
			} else {
				NVT_LOG("Gesture : Double Click Not Enable.\n");
				keycode = 0;
			}
			break;
		case GESTURE_WORD_Z:
			NVT_LOG("Gesture : Word-Z.\n");
			keycode = gesture_key_array[4];
			break;
		case GESTURE_WORD_M:
			NVT_LOG("Gesture : Word-M.\n");
			keycode = gesture_key_array[5];
			break;
		case GESTURE_WORD_O:
			NVT_LOG("Gesture : Word-O.\n");
			keycode = gesture_key_array[6];
			break;
		case GESTURE_WORD_e:
			NVT_LOG("Gesture : Word-e.\n");
			keycode = gesture_key_array[7];
			break;
		case GESTURE_WORD_S:
			NVT_LOG("Gesture : Word-S.\n");
			keycode = gesture_key_array[8];
			break;
		case GESTURE_SLIDE_UP:
			NVT_LOG("Gesture : Slide UP.\n");
			keycode = gesture_key_array[9];
			break;
		case GESTURE_SLIDE_DOWN:
			NVT_LOG("Gesture : Slide DOWN.\n");
			keycode = gesture_key_array[10];
			break;
		case GESTURE_SLIDE_LEFT:
			NVT_LOG("Gesture : Slide LEFT.\n");
			keycode = gesture_key_array[11];
			break;
		case GESTURE_SLIDE_RIGHT:
			NVT_LOG("Gesture : Slide RIGHT.\n");
			keycode = gesture_key_array[12];
			break;
		case GESTURE_SINGLE_CLICK:
			NVT_LOG("Gesture : Finger Single Click.\n");
			if (ts->gesture_command & 0x04) {
				keycode = gesture_key_array[13];
			} else {
				NVT_LOG("Gesture : Finger Single Click Not Enable.\n");
				keycode = 0;
			}
			break;
		default:
			break;
	}

	if (keycode > 0) {
		input_report_key(ts->input_dev, keycode, 1);
		input_sync(ts->input_dev);
		input_report_key(ts->input_dev, keycode, 0);
		input_sync(ts->input_dev);
	}
}

void report_single_click_pen_input(struct input_dev *dev){
	//pen down
	input_report_abs(dev, ABS_X, 1);
	input_report_abs(dev, ABS_Y, 1);
	input_report_abs(dev, ABS_PRESSURE, 1);
	input_report_key(dev, BTN_TOUCH, 1);
	input_report_abs(dev, ABS_TILT_X, 1);
	input_report_abs(dev, ABS_TILT_Y, 1);
	input_report_abs(dev, ABS_DISTANCE, 0);
	input_report_key(dev, BTN_TOOL_PEN, 1);
	input_sync(dev);
	//pen release
	input_report_abs(dev, ABS_X, 0);
	input_report_abs(dev, ABS_Y, 0);
	input_report_abs(dev, ABS_PRESSURE, 0);
	input_report_abs(dev, ABS_TILT_X, 0);
	input_report_abs(dev, ABS_TILT_Y, 0);
	input_report_abs(dev, ABS_DISTANCE, 1);
	input_report_key(dev, BTN_TOUCH, 0);
	input_report_key(dev, BTN_TOOL_PEN, 0);
	input_sync(dev);
}

void nvt_ts_pen_gesture_report(uint8_t pen_gesture_id)
{
	switch (pen_gesture_id) {
		case PEN_GESTURE_SINGLE_CLICK:
			NVT_LOG("Gesture : Pen Single Click.\n");
			if (ts->gesture_command & 0x02) {
				if(ts->cur_pen == PEN_M80P)
					report_single_click_pen_input(ts->pen_input_dev_m80p);
				else if (ts->cur_pen == PEN_P81C)
					report_single_click_pen_input(ts->pen_input_dev_p81c);
			} else {
				NVT_LOG("Gesture : Pen Click Not Enable.\n");
			}
			break;
		default:
			break;
	}
}
#endif

/*******************************************************
Description:
	Novatek touchscreen parse device tree function.

return:
	n.a.
*******************************************************/
#ifdef CONFIG_OF
static int32_t nvt_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	int32_t ret = 0;

#if NVT_TOUCH_SUPPORT_HW_RST
	ts->reset_gpio = of_get_named_gpio(np, "novatek,reset-gpio", 0);
	NVT_LOG("novatek,reset-gpio=%d\n", ts->reset_gpio);
#endif
	ts->irq_gpio = of_get_named_gpio(np, "novatek,irq-gpio", 0);
	NVT_LOG("novatek,irq-gpio=%d\n", ts->irq_gpio);

	ts->pen_support = of_property_read_bool(np, "novatek,pen-support");
	NVT_LOG("novatek,pen-support=%d\n", ts->pen_support);

	ts->lcd_id_gpio = of_get_named_gpio(np, "novatek,lcd_id-gpio", 0);
	NVT_LOG("novatek,lcd_id-gpio=%d\n", ts->lcd_id_gpio);
	gpio_direction_input(ts->lcd_id_gpio);
	/* read id pin value, 0 is CSOT, 1 is BOE */
	ts->lcd_id_value = gpio_get_value(ts->lcd_id_gpio);
	NVT_LOG("lcd_id_value = %d, this panel is %s", ts->lcd_id_value, ts->lcd_id_value ? "BOE" : "CSOT");

	ret = of_property_read_u32(np, "novatek,spi-rd-fast-addr", &SPI_RD_FAST_ADDR);
	if (ret) {
		NVT_LOG("not support novatek,spi-rd-fast-addr\n");
		SPI_RD_FAST_ADDR = 0;
		ret = 0;
	} else {
		NVT_LOG("SPI_RD_FAST_ADDR=0x%06X\n", SPI_RD_FAST_ADDR);
	}

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
	/* gamemode setup */
	ret = of_property_read_u32_array(np, "novatek,touch-game-param-config1", ts->gamemode_config[0], 5);
	if (ret) {
		NVT_LOG("Failed to get touch-game-param-config1\n");
		ret = 0;
	} else {
		NVT_LOG("read touch gamemode parameter config1:[%d, %d, %d, %d, %d]",
		ts->gamemode_config[0][0], ts->gamemode_config[0][1],
		ts->gamemode_config[0][2], ts->gamemode_config[0][3],
		ts->gamemode_config[0][4]);
	}

	ret = of_property_read_u32_array(np, "novatek,touch-game-param-config2", ts->gamemode_config[1], 5);
	if (ret) {
		NVT_LOG("Failed to get touch-game-param-config2\n");
		ret = 0;
	} else {
		NVT_LOG("read touch gamemode parameter config2:[%d, %d, %d, %d, %d]",
		ts->gamemode_config[1][0], ts->gamemode_config[1][1],
		ts->gamemode_config[1][2], ts->gamemode_config[1][3],
		ts->gamemode_config[1][4]);
	}

	ret = of_property_read_u32_array(np, "novatek,touch-game-param-config3", ts->gamemode_config[2], 5);
	if (ret) {
		NVT_LOG("Failed to get touch-game-param-config3\n");
		ret = 0;
	} else {
		NVT_LOG("read touch gamemode parameter config3:[%d, %d, %d, %d, %d]",
		ts->gamemode_config[2][0], ts->gamemode_config[2][1],
		ts->gamemode_config[2][2], ts->gamemode_config[2][3],
		ts->gamemode_config[2][4]);
	}
	/* gamemode setup end */
#endif	/* #ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */

	return ret;
}
#else
static int32_t nvt_parse_dt(struct device *dev)
{
#if NVT_TOUCH_SUPPORT_HW_RST
	ts->reset_gpio = NVTTOUCH_RST_PIN;
#endif
	ts->irq_gpio = NVTTOUCH_INT_PIN;
	ts->pen_support = false;
	return 0;
}
#endif

/*******************************************************
Description:
	Novatek touchscreen config and request gpio

return:
	Executive outcomes. 0---succeed. not 0---failed.
*******************************************************/
static int nvt_gpio_config(struct nvt_ts_data *ts)
{
	int32_t ret = 0;

#if NVT_TOUCH_SUPPORT_HW_RST
	/* request RST-pin (Output/High) */
	if (gpio_is_valid(ts->reset_gpio)) {
		ret = gpio_request_one(ts->reset_gpio, GPIOF_OUT_INIT_LOW, "NVT-tp-rst");
		if (ret) {
			NVT_ERR("Failed to request NVT-tp-rst GPIO\n");
			goto err_request_reset_gpio;
		}
	}
#endif

	/* request INT-pin (Input) */
	if (gpio_is_valid(ts->irq_gpio)) {
		ret = gpio_request_one(ts->irq_gpio, GPIOF_IN, "NVT-int");
		if (ret) {
			NVT_ERR("Failed to request NVT-int GPIO\n");
			goto err_request_irq_gpio;
		}
	}

	return ret;

err_request_irq_gpio:
#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_free(ts->reset_gpio);
err_request_reset_gpio:
#endif
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen deconfig gpio

return:
	n.a.
*******************************************************/
static void nvt_gpio_deconfig(struct nvt_ts_data *ts)
{
	if (gpio_is_valid(ts->irq_gpio))
		gpio_free(ts->irq_gpio);
#if NVT_TOUCH_SUPPORT_HW_RST
	if (gpio_is_valid(ts->reset_gpio))
		gpio_free(ts->reset_gpio);
#endif
}

#if !TOUCH_THP_SUPPORT
static uint8_t nvt_fw_recovery(uint8_t *point_data)
{
	uint8_t i = 0;
	uint8_t detected = true;

	/* check pattern */
	for (i=1 ; i<7 ; i++) {
		if (point_data[i] != 0x77) {
			detected = false;
			break;
		}
	}

	return detected;
}
#endif

#if NVT_TOUCH_ESD_PROTECT
void nvt_esd_check_enable(uint8_t enable)
{
	/* update interrupt timer */
	irq_timer = jiffies;
	/* clear esd_retry counter, if protect function is enabled */
	esd_retry = enable ? 0 : esd_retry;
	/* enable/disable esd check flag */
	esd_check = enable;
}

static void nvt_esd_check_func(struct work_struct *work)
{
	unsigned int timer = jiffies_to_msecs(jiffies - irq_timer);

	//NVT_LOG("esd_check = %d (retry %d)\n", esd_check, esd_retry);	//DEBUG

	if ((timer > NVT_TOUCH_ESD_CHECK_PERIOD) && esd_check) {
		mutex_lock(&ts->lock);
		NVT_ERR("do ESD recovery, timer = %d, retry = %d\n", timer, esd_retry);
		/* do esd recovery, reload fw */
		nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
		mutex_unlock(&ts->lock);
		/* update interrupt timer */
		irq_timer = jiffies;
		/* update esd_retry counter */
		esd_retry++;
	}

	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
}
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#define PEN_DATA_LEN 14
#if CHECK_PEN_DATA_CHECKSUM
static int32_t nvt_ts_pen_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;

	// Calculate checksum
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i];
	}
	checksum = (~checksum + 1);

	// Compare ckecksum and dump fail data
	if (checksum != buf[length - 1]) {
		NVT_ERR("pen packet checksum not match. (buf[%d]=0x%02X, checksum=0x%02X)\n",
			length - 1, buf[length - 1], checksum);
		//--- dump pen buf ---
		for (i = 0; i < length; i++) {
			printk("%02X ", buf[i]);
		}
		printk("\n");

		return -1;
	}

	return 0;
}
#endif // #if CHECK_PEN_DATA_CHECKSUM

#if NVT_TOUCH_WDT_RECOVERY
static uint8_t recovery_cnt = 0;
static uint8_t nvt_wdt_fw_recovery(uint8_t *point_data)
{
   uint32_t recovery_cnt_max = 10;
   uint8_t recovery_enable = false;
   uint8_t i = 0;

   recovery_cnt++;

   /* check pattern */
   for (i=1 ; i<7 ; i++) {
       if ((point_data[i] != 0xFD) && (point_data[i] != 0xFE)) {
           recovery_cnt = 0;
           break;
       }
   }

   if (recovery_cnt > recovery_cnt_max){
       recovery_enable = true;
       recovery_cnt = 0;
   }

   return recovery_enable;
}

void nvt_clear_aci_error_flag(void)
{
	if (ts->mmap->ACI_ERR_CLR_ADDR == 0)
		return;

	nvt_write_addr(ts->mmap->ACI_ERR_CLR_ADDR, 0xA5);

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}
#endif	/* #if NVT_TOUCH_WDT_RECOVERY */

void nvt_read_fw_history(uint32_t fw_history_addr)
{
	uint8_t i = 0;
	uint8_t buf[65];
	char str[128];

	if (fw_history_addr == 0)
		return;

	nvt_set_page(fw_history_addr);

	buf[0] = (uint8_t) (fw_history_addr & 0x7F);
	CTP_SPI_READ(ts->client, buf, 64+1);	//read 64bytes history

	//print all data
	NVT_LOG("fw history 0x%X: \n", fw_history_addr);
	for (i = 0; i < 4; i++) {
		snprintf(str, sizeof(str),
				"%02X %02X %02X %02X %02X %02X %02X %02X  "
				"%02X %02X %02X %02X %02X %02X %02X %02X\n",
				buf[1+i*16], buf[2+i*16], buf[3+i*16], buf[4+i*16],
				buf[5+i*16], buf[6+i*16], buf[7+i*16], buf[8+i*16],
				buf[9+i*16], buf[10+i*16], buf[11+i*16], buf[12+i*16],
				buf[13+i*16], buf[14+i*16], buf[15+i*16], buf[16+i*16]);
		NVT_LOG("%s", str);
	}

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

void nvt_read_fw_history_all(void) {

	/* ICM History */
	nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT0);
	nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT1);

	/* ICS History */
	if (ts->is_cascade) {
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT0_ICS);
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT1_ICS);
	}
}

#define FW_FLOW_DEBUG_MESSAGE_LEN 176
void nvt_read_print_fw_flow_debug_message(void) {
	uint8_t buf[FW_FLOW_DEBUG_MESSAGE_LEN + 2];
	uint32_t fw_flow_debug_message_addr;
	uint8_t *data;
	uint32_t data_len;
	int x_num;
	int y_num;
	int i, j;
	char *tmp_log = NULL;
	int data_char_num;

	if (ts->mmap->FW_FLOW_DEBUG_MESSAGE_ADDR == 0) {
		NVT_ERR("fw flow debug message not support\n");
		return;
	}
	fw_flow_debug_message_addr = ts->mmap->FW_FLOW_DEBUG_MESSAGE_ADDR;
	nvt_set_page(fw_flow_debug_message_addr);
	buf[0] = fw_flow_debug_message_addr & 0xFF;
	CTP_SPI_READ(ts->client, buf, FW_FLOW_DEBUG_MESSAGE_LEN + 1);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	data = buf + 1;
	data_len = FW_FLOW_DEBUG_MESSAGE_LEN;
	x_num = 32;
	y_num = data_len / x_num;
	data_char_num = 3;
	tmp_log = (char *)kzalloc(x_num * data_char_num + 1, GFP_KERNEL);
	if (!tmp_log) {
		NVT_ERR("kzalloc for tmp_log failed!\n ");
		return;
	}
	NVT_LOG("fw flow debug message 0x%X:\n", fw_flow_debug_message_addr);
	for (j = 0; j < y_num; j++) {
		for (i = 0; i < x_num; i++) {
			sprintf(tmp_log + i * data_char_num, "%02X ", data[j * x_num + i]);
		}
		tmp_log[x_num * data_char_num] = '\0';
		NVT_LOG("%s\n", tmp_log);
		memset(tmp_log, 0, x_num * data_char_num + 1);
	}
	if (data_len % x_num) {
		for (i = 0; i < (data_len % x_num); i++) {
			sprintf(tmp_log + i * data_char_num, "%02X ", data[y_num * x_num + i]);
		}
		tmp_log[(data_len % x_num) * data_char_num] = '\0';
		NVT_LOG("%s\n", tmp_log);
		memset(tmp_log, 0, x_num * data_char_num + 1);
	}

	if (tmp_log) {
		kfree(tmp_log);
		tmp_log = NULL;
	}
}

#if POINT_DATA_CHECKSUM
static int32_t nvt_ts_point_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;

	// Generate checksum
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i + 1];
	}
	checksum = (~checksum + 1);

	// Compare ckecksum and dump fail data
	if (checksum != buf[length]) {
		NVT_ERR("i2c/spi packet checksum not match. (point_data[%d]=0x%02X, checksum=0x%02X)\n",
				length, buf[length], checksum);

		for (i = 0; i < 10; i++) {
			NVT_LOG("%02X %02X %02X %02X %02X %02X\n",
					buf[1 + i*6], buf[2 + i*6], buf[3 + i*6], buf[4 + i*6], buf[5 + i*6], buf[6 + i*6]);
		}

		NVT_LOG("%02X %02X %02X %02X %02X\n", buf[61], buf[62], buf[63], buf[64], buf[65]);

		return -1;
	}

	return 0;
}
#endif /* POINT_DATA_CHECKSUM */

//thp start 6.8
#if TOUCH_THP_SUPPORT
// #define POINT_DATA_LEN 65
uint16_t nvt_xm_htc_cal_checksum(uint16_t u16_message[], int data_len)
{
	int i;
	uint32_t tmp_val = 0;
	uint16_t checksum;

	for (i = 0; i < data_len; i++) {
		tmp_val += u16_message[i];
	}

	checksum = ((~tmp_val) + 1) & 0xFFFF;

	return checksum;
}

#define CRC32_POLYNOMIAL 0xE89061DB
inline int32_t thp_crc32_check(int s32_message[], int s32_len)
{
	int i;
	int j;
	int k;
	int s32_remainder;
	unsigned char u8_byteData;
	s32_remainder = 0UL;

	for (i = 0; i < s32_len; i++) {
		for (j = 3; j >= 0; j--) {
			u8_byteData = s32_message[i] >> (j * 8);
			s32_remainder ^= (u8_byteData << 24);
			for (k = 8; k > 0; --k) {
				//Try to divide the current data bit
				if (s32_remainder & (1UL << 31)) {
					s32_remainder = (s32_remainder << 1) ^ CRC32_POLYNOMIAL;
				} else {
					s32_remainder = (s32_remainder << 1);
				}
			}
		}
	}
	return s32_remainder;
}

#define FRAME_DATA_PACKET_DEBUG_PRINT 0
#if FRAME_DATA_PACKET_DEBUG_PRINT
static void nvt_xm_htc_print_data_log_in_one_line(int16_t *data, uint8_t x_num, uint8_t y_num)
{
	int i, j;
	char *tmp_log = NULL;

	tmp_log = (char *)kzalloc(x_num * 6 + 1, GFP_KERNEL);
	if (!tmp_log) {
		printk("nvt-ts:     kzalloc for tmp_log failed!\n ");
		return;
	}

	for (j = 0; j < y_num; j++) {
		for (i = 0; i < x_num; i++) {
			sprintf(tmp_log + i * 6, "%5d ", data[i * y_num + j]);
		}
		tmp_log[x_num * 6] = '\0';
		printk("nvt-ts:     %s", tmp_log);
		memset(tmp_log, 0, x_num * 6);
		printk("\n");
	}

	if (tmp_log) {
		kfree(tmp_log);
		tmp_log = NULL;
	}

	return;
}
#endif
static uint64_t thp_cnt = 0;

static inline int32_t nvt_crc32_check(int s32_message[], int s32_len)
{
    int i;
    int j;
    int k;
    int s32_remainder;
    unsigned char u8_byteData;

    s32_remainder = 0UL;

    for (i = 0; i < s32_len; i++) {
        for (j = 3; j >= 0; j--) {
            //Get the correct byte ordering
            u8_byteData = s32_message[i] >> (j * 8);
            //Bring the next byte into the remainder
            s32_remainder ^= (u8_byteData << 24);
            //Perform modulo-2 division, a bit at a time
            for (k = 8; k > 0; --k) {
                //Try to divide the current data bit
                if (s32_remainder & (1UL << 31)) {
                    s32_remainder = (s32_remainder << 1) ^ CRC32_POLYNOMIAL;
                } else {
                    s32_remainder = (s32_remainder << 1);
                }
            }
        }
    }

    return s32_remainder;
}

#if FRAME_DATA_PACKET_DEBUG_PRINT
static uint32_t debug_print_count = 0; // for debug print
static bool print_debug_data;
static uint8_t hand_data_tmp[50*50*2];
uint16_t hand_data_tmp_offset = 0;
#endif /* #define FRAME_DATA_PACKET_DEBUG_PRINT */
#endif
//thp end 6.8

#define NVT_CRC_DATA_ENABLE 0
#define NVT_PARSE_DATA_ENABLE 0

#if NVT_PARSE_DATA_ENABLE
static void nvt_ts_prase_data_func(void *data)
{
	int32_t ret = -1;
	uint8_t *point_data;
	uint8_t input_id = 0;
	uint8_t pen_format_id;
	uint8_t *frame_data_packet;
	uint16_t head_count;
	uint8_t scan_saturation_state;
	uint8_t scan_mode;
	uint16_t frame_no;
	uint16_t drop_frame_no;
	uint16_t noise_r0;
	uint16_t noise_r1;
	uint16_t noise_r2;
	uint16_t noise_r3;
	uint8_t numCol;
	uint8_t numRow;
	uint16_t ic_ms_time;
	uint16_t scan_rate;
	uint8_t scan_freq_index;
	uint8_t write_cmd_cnt;
	uint8_t frame_data_type;
	uint16_t write_cmd;
	uint8_t *frame_data;
	// pen
	uint16_t pen_drop_frame_no;
	uint16_t pen_frame_no;
	uint8_t pen_numCol1;
	uint8_t pen_numRow1;
	uint8_t pen_numCol2;
	uint8_t pen_numRow2;
	uint8_t hand_data_packet_no;
	uint16_t hand_data_packet_len;
	uint8_t pen_hover_status;
	uint16_t pen_scan_rate;
	uint16_t pen_scan_freq;
	uint16_t pen_noise_r0;
	uint16_t pen_noise_r1;
	uint32_t pen_pressure;
	uint32_t pen_btn1;
	uint32_t pen_btn2;
	uint32_t pen_battery;
#if FRAME_DATA_PACKET_DEBUG_PRINT
	uint8_t *pen_data;
	uint16_t offset;
	uint8_t *hand_data;
#endif /* #if FRAME_DATA_PACKET_DEBUG_PRINT */
#if NVT_CRC_DATA_ENABLE
	uint16_t checksum;
	uint16_t checksum_bar;
	uint16_t checksum_cal;
	bool checksum_correct;
	int32_t crc_len;
	int32_t crc_len_bar;
	int32_t crc;
	int32_t crc_cal;
	uint16_t checksum2;
	uint16_t checksum2_bar;
	uint16_t checksum2_cal;
	bool checksum2_correct;
	int32_t crc2_len;
	int32_t crc2_len_bar;
	int32_t crc2;
	int32_t crc2_cal;
#endif //NVT_CRC_DATA_ENABLE

	frame_data_packet = data;
#if NVT_CRC_DATA_ENABLE
	checksum = *((uint16_t *)(frame_data_packet + 4));
	crc_len = *((int32_t *)(frame_data_packet + 8));
	checksum_bar = *((uint16_t *)(frame_data_packet + 12));
	crc_len_bar = *((int32_t *)(frame_data_packet + 16));
	checksum_cal = nvt_xm_htc_cal_checksum((uint16_t *)(frame_data_packet + 20), crc_len * 4 / 2);
	checksum_correct = true;
	if ((checksum_bar != ((uint16_t)(~checksum))) || (crc_len_bar != ~crc_len) || (checksum_cal != checksum)) {
		printk("nvt-ts:     checksum wrong(checksum=0x%04X, crc_len=0x%08X, checksum_bar=0x%04X, ~crc_len=0x%08X, checksum_cal=0x%04X)!\n",
				checksum, crc_len, checksum_bar, crc_len_bar, checksum_cal);
		checksum_correct = false;
		goto XFER_ERROR;
	}
	printk("nvt-ts:     frame_data_type=%d, checksum=0x%04X, checksum_cal=0x%04X, checksum_bar=0x%04X, ~checksum=0x%04X", 
			frame_data_packet[56], checksum, checksum_cal, checksum_bar, (uint16_t)(~checksum));
#endif //NVT_CRC_DATA_ENABLE
	head_count = *((uint16_t *)(frame_data_packet + 2));
	scan_saturation_state = frame_data_packet[20];
	scan_mode = frame_data_packet[24];
	frame_no = *((uint16_t *)(frame_data_packet + 28));
	drop_frame_no = *((uint16_t *)(frame_data_packet + 30));
	noise_r0 = *((uint16_t *)(frame_data_packet + 32));
	noise_r1 = *((uint16_t *)(frame_data_packet + 34));
	noise_r2 = *((uint16_t *)(frame_data_packet + 36));
	noise_r3 = *((uint16_t *)(frame_data_packet + 38));
	numCol = frame_data_packet[48];
	numRow = frame_data_packet[49];
	ic_ms_time = *((uint16_t *)(frame_data_packet + 50));
	scan_rate = *((uint16_t *)(frame_data_packet + 52));
	scan_freq_index = frame_data_packet[54];
	write_cmd_cnt = frame_data_packet[55];
	frame_data_type = frame_data_packet[56];
	write_cmd = *((uint16_t *)(frame_data_packet + 62));
	frame_data = frame_data_packet + 64;
	#if FRAME_DATA_PACKET_DEBUG_PRINT
	printk("nvt-ts:      frame_data_type=%d, head_count=%d, scan_mode=%d, frame_no=%d, drop_frame_no=%d, checksum=0x%04X, crc_len=0x%08X, checksum_bar=0x%04X, ~crc_len=0x%08X, checksum_cal=0x%04X\n",
			frame_data_type, head_count, scan_mode, frame_no, drop_frame_no, checksum, crc_len, checksum_bar, crc_len_bar,  checksum_cal);
	printk("nvt-ts:      noise_r0=%d, noise_r1=%d, noise_r2=%d, noise_r3=%d, ic_ms_time=%d, scan_rate=%d, scan_freq_index=%d, scan_saturation_state=0x%02X, write_cmd_cnt=%d, write_cmd=%d\n",
		noise_r0, noise_r1, noise_r2, noise_r3, ic_ms_time, scan_rate, scan_freq_index, scan_saturation_state, write_cmd_cnt, write_cmd);
	debug_print_count++; // for temporarily debug print
	print_debug_data = ((debug_print_count%30 == 0) || (debug_print_count%30 == 1) || (debug_print_count%30 == 2) || (debug_print_count%30 == 3));
	print_debug_data = ((debug_print_count%60 == 0) || (debug_print_count%60 == 1) || (debug_print_count%60 == 2) || (debug_print_count%60 == 3) || (debug_print_count%60 == 4) || (debug_print_count%60 == 5));
	print_debug_data = ((debug_print_count%60 == 0) || (debug_print_count%60 == 1) || (debug_print_count%60 == 2) || (debug_print_count%60 == 3) || (debug_print_count%60 == 4) || (debug_print_count%60 == 5)) || ((head_cnt >= 1) && (head_cnt <= 6)) || ((frame_no >= 1) && (frame_no <= 6));
	#endif /* #if FRAME_DATA_PACKET_DEBUG_PRINT */
	if (frame_data_type == 3) {
		#if FRAME_DATA_PACKET_DEBUG_PRINT
		if (print_debug_data) {
			printk("nvt-ts:     frame_no=%d, numCol x numRow = %d x %d\n", frame_no, numCol, numRow);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)frame_data, numRow, numCol);
		}
		#endif /* #if FRAME_DATA_PACKET_DEBUG_PRINT */
	} else if (frame_data_type == 6 || frame_data_type == 7 || frame_data_type == 9) {
		pen_drop_frame_no = *((uint16_t *)frame_data);
		pen_frame_no = *((uint16_t *)(frame_data + 2));
		pen_pressure = *((uint16_t *)(frame_data + 4));
		pen_btn1 = frame_data[6];
		pen_numCol1 = frame_data[7];
		pen_numRow1 = frame_data[8];
		pen_numCol2 = frame_data[9];
		pen_numRow2 = frame_data[10];
		hand_data_packet_no = frame_data[11];
		hand_data_packet_len = *((uint16_t *)(frame_data + 12));
		pen_btn2 = frame_data[14];
		pen_battery = frame_data[15];
		pen_hover_status = frame_data[16];
		#if NVT_CRC_DATA_ENABLE
		checksum2 = *((uint16_t *)(frame_data + 26 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 4));
		checksum2_bar = *((uint16_t *)(frame_data + 26 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 8));
		crc2_len = (44 + 26 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 4) / 4;
		checksum2_cal = nvt_xm_htc_cal_checksum((uint16_t *)(frame_data_packet + 20), crc2_len * 4 / 2);
		checksum2_correct = true;
		if ((checksum2_bar != ((uint16_t)(~checksum2))) || (checksum2_cal != checksum2)) {
			printk("nvt-ts:     checksum2 wrong(checksum2=0x%04X, crc2_len=0x%08X, checksum2_bar=0x%04X, checksum2_cal=0x%04X)!\n",
					checksum2, crc2_len, checksum2_bar, checksum2_cal);
			checksum2_correct = false;
			goto XFER_ERROR;
		}
		#endif //NVT_CRC_DATA_ENABLE
		#if FRAME_DATA_PACKET_DEBUG_PRINT
		printk("nvt-ts:     PEN: drop_frame_no=%d, frame_no=%d, pressure=%d, btn1=%d, btn2=%d, battery=%d, hover=%d. checksum2=0x%04X, crc2_len=0x%08X, checksum2_bar=0x%04X, checksum2_cal=0x%04X\n",
				pen_drop_frame_no, pen_frame_no, pen_pressure, pen_btn1, pen_btn2, pen_battery, pen_hover_status, checksum2, crc2_len, checksum2_bar, checksum2_cal);
		/* pen data */
		pen_data = frame_data + 26;
		offset = 0;
		if (print_debug_data) {
			printk("nvt-ts:     Tip numCol1 x numRow1 = %d x %d\n", pen_numCol1, pen_numRow1);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow1, pen_numCol1);
		}
		offset += (pen_numCol1 * pen_numRow1) * 2;
		if (print_debug_data) {
			printk("nvt-ts:     Tip numCol2 x numRow2 = %d x %d\n", pen_numCol2, pen_numRow2);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow2, pen_numCol2);
		}
		offset += (pen_numCol2 * pen_numRow2) * 2;
		if (print_debug_data) {
			printk("nvt-ts:     Ring numCol1 x numRow1 = %d x %d\n", pen_numCol1, pen_numRow1);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow1, pen_numCol1);
		}
		offset += (pen_numCol1 * pen_numRow1) * 2;
		if (print_debug_data) {
			printk("nvt-ts:     Ring numCol2 x numRow2 = %d x %d\n", pen_numCol2, pen_numRow2);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow2, pen_numCol2);
		}
		if (frame_data_type == 6) {
			/* no hand data, only pen data */
		} else if (frame_data_type == 7) {
			/* hand data */
			printk("nvt-ts:     hand_data_packet_no=%d, hand_data_packet_len=%d\n", hand_data_packet_no, hand_data_packet_len);
			hand_data = frame_data + 26 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 12;
			memset(hand_data_tmp, 0, numCol * numRow * 2);
			memcpy(hand_data_tmp, hand_data, hand_data_packet_len);
			if (print_debug_data) {
				nvt_xm_htc_print_data_log_in_one_line((int16_t *)hand_data_tmp, numRow, numCol);
			}
		} else if (frame_data_type == 9) {
			/* 1/4 hand data */
			printk("nvt-ts:     hand_data_packet_no=%d, hand_data_packet_len=%d\n", hand_data_packet_no, hand_data_packet_len);
			hand_data = frame_data + 26 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 12;
			if (hand_data_packet_no == 1) {
				memset(hand_data_tmp, 0, numCol * numRow * 2);
			}
			if ((hand_data_packet_no < 1) || (hand_data_packet_no > 4)) {
				printk("nvt-ts:     invalid hand_data_packet_no(%d)!\n", hand_data_packet_no);
				goto XFER_ERROR;
			}
			hand_data_tmp_offset = (hand_data_packet_no - 1) * ((numCol * numRow * 2) / 4);
			if ((hand_data_tmp_offset + hand_data_packet_len) > (numCol * numRow * 2)) {
				printk("nvt-ts:     (hand_data_tmp_offset(%d) + hand_data_packet_len(%d)) > numCol(%d) * numRow(%d) * 2!\n",
						hand_data_tmp_offset, hand_data_packet_len, numCol, numRow);
				memset(hand_data_tmp, 0, numCol * numRow * 2);
				goto XFER_ERROR;
			}
			memcpy(hand_data_tmp + hand_data_tmp_offset, hand_data, hand_data_packet_len);
			if (print_debug_data) {
				nvt_xm_htc_print_data_log_in_one_line((int16_t *)hand_data_tmp, numRow, numCol);
			}
			if (hand_data_packet_no == 4) {
				hand_data_tmp_offset = 0;
				memset(hand_data_tmp, 0, numCol * numRow * 2);
			}
		}
		#endif /* #if FRAME_DATA_PACKET_DEBUG_PRINT */
		#if NVT_CRC_DATA_ENABLE
		if (checksum2_correct) {
			// re-calculate crc2 using xm's original thp_crc32_check(), and put them to crc2, ~crc2, fields
			crc2_cal = thp_crc32_check((int32_t *)(frame_data_packet + 20), crc2_len);
			*((int32_t *)(frame_data + 26 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 4)) = crc2_cal;
			*((int32_t *)(frame_data + 26 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 8)) = ~crc2_cal;
			crc2 = crc2_cal;
		}
		#endif //NVT_CRC_DATA_ENABLE
	} else if (frame_data_type == 17 || frame_data_type == 29) {
		pen_drop_frame_no = *((uint16_t *)frame_data);
		pen_frame_no = *((uint16_t *)(frame_data + 2));
		pen_pressure = *((uint16_t *)(frame_data + 4));
		pen_btn1 = frame_data[6];
		pen_numCol1 = frame_data[7];
		pen_numRow1 = frame_data[8];
		pen_numCol2 = frame_data[9];
		pen_numRow2 = frame_data[10];
		hand_data_packet_no = frame_data[11];
		hand_data_packet_len = *((uint16_t *)(frame_data + 12));
		pen_btn2 = frame_data[14];
		pen_battery = frame_data[15];
		pen_hover_status = frame_data[16];
		pen_scan_rate = *((uint16_t *)(frame_data + 18));
		pen_scan_freq = *((uint16_t *)(frame_data + 20));
		pen_noise_r0 = *((uint16_t *)(frame_data + 22));
		pen_noise_r1 = *((uint16_t *)(frame_data + 24));
		#if NVT_CRC_DATA_ENABLE
		checksum2 = *((uint16_t *)(frame_data + 36 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 4));
		crc2_len = *((uint32_t *)(frame_data + 36 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 8));
		checksum2_bar = *((uint16_t *)(frame_data + 36 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 12));
		crc2_len_bar = *((uint32_t *)(frame_data + 36 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 16));
		checksum2_cal = nvt_xm_htc_cal_checksum((uint16_t *)(frame_data_packet + 20), crc2_len * 4 / 2);
		checksum2_correct = true;
		if ((checksum2_bar != ((uint16_t)(~checksum2))) || (crc2_len_bar != ~crc2_len) || (checksum2_cal != checksum2)) {
			printk("nvt-ts:     checksum2 wrong(checksum2=0x%04X, crc2_len=0x%08X, checksum2_bar=0x%04X, ~crc2_len=0x%08X, checksum2_cal=0x%04X)!\n",
					checksum2, crc2_len, checksum2_bar, crc2_len_bar, checksum2_cal);
			checksum2_correct = false;
			goto XFER_ERROR;
		}
		#endif //NVT_CRC_DATA_ENABLE
		#if FRAME_DATA_PACKET_DEBUG_PRINT
		printk("nvt-ts:     PEN: drop_frame_no=%d, frame_no=%d, pressure=%d, btn1=%d, btn2=%d, battery=%d, hover=%d. checksum2=0x%04X, crc2_len=0x%08X, checksum2_bar=0x%04X, ~crc2_len=0x%08X, checksum2_cal=0x%04X\n",
				pen_drop_frame_no, pen_frame_no, pen_pressure, pen_btn1, pen_btn2, pen_battery, pen_hover_status, checksum2, crc2_len, checksum2_bar, crc2_len_bar, checksum2_cal);
		printk("nvt-ts:     PEN: pen_scan_rate=%d, pen_scan_freq=%d, pen_noise_r0=%d, pen_noise_r1=%d\n", pen_scan_rate, pen_scan_freq, pen_noise_r0, pen_noise_r1);
		/* pen data */
		pen_data = frame_data + 36;
		offset = 0;
		if (print_debug_data) {
			printk("nvt-ts:     Tip numCol1 x numRow1 = %d x %d\n", pen_numCol1, pen_numRow1);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow1, pen_numCol1);
		}
		offset += (pen_numCol1 * pen_numRow1) * 2;
		if (print_debug_data) {
			printk("nvt-ts:     Tip numCol2 x numRow2 = %d x %d\n", pen_numCol2, pen_numRow2);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow2, pen_numCol2);
		}
		offset += (pen_numCol2 * pen_numRow2) * 2;
		if (print_debug_data) {
			printk("nvt-ts:     Ring numCol1 x numRow1 = %d x %d\n", pen_numCol1, pen_numRow1);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow1, pen_numCol1);
		}
		offset += (pen_numCol1 * pen_numRow1) * 2;
		if (print_debug_data) {
			printk("nvt-ts:     Ring numCol2 x numRow2 = %d x %d\n", pen_numCol2, pen_numRow2);
			nvt_xm_htc_print_data_log_in_one_line((int16_t *)(pen_data + offset), pen_numRow2, pen_numCol2);
		}
		if (frame_data_type == 17) {
			/* no hand data, only pen data */
		} else if (frame_data_type == 29) {
			/* 1/4 hand data */
			printk("nvt-ts:     hand_data_packet_no=%d, hand_data_packet_len=%d\n", hand_data_packet_no, hand_data_packet_len);
			hand_data = pen_data + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 20;
			if (hand_data_packet_no == 1) {
				memset(hand_data_tmp, 0, numCol * numRow * 2);
			}
			if ((hand_data_packet_no < 1) || (hand_data_packet_no > 4)) {
				printk("nvt-ts:     invalid hand_data_packet_no(%d)!\n", hand_data_packet_no);
				goto XFER_ERROR;
			}
			hand_data_tmp_offset = (hand_data_packet_no - 1) * ((numCol * numRow * 2) / 4);
			if ((hand_data_tmp_offset + hand_data_packet_len) > (numCol * numRow * 2)) {
				printk("nvt-ts:     (hand_data_tmp_offset(%d) + hand_data_packet_len(%d)) > numCol(%d) * numRow(%d) * 2!\n",
						hand_data_tmp_offset, hand_data_packet_len, numCol, numRow);
				memset(hand_data_tmp, 0, numCol * numRow * 2);
				goto XFER_ERROR;
			}
			memcpy(hand_data_tmp + hand_data_tmp_offset, hand_data, hand_data_packet_len);
			if (print_debug_data) {
				nvt_xm_htc_print_data_log_in_one_line((int16_t *)hand_data_tmp, numRow, numCol);
			}
			if (hand_data_packet_no == 4) {
				hand_data_tmp_offset = 0;
				memset(hand_data_tmp, 0, numCol * numRow * 2);
			}
		}
		#endif /* #if FRAME_DATA_PACKET_DEBUG_PRINT */
		#if NVT_CRC_DATA_ENABLE
		if (checksum2_correct) {
			// re-calculate crc2 using xm's original thp_crc32_check(), and put them to crc2, ~crc2, fields
			crc2_cal = thp_crc32_check((int32_t *)(frame_data_packet + 20), crc2_len);
			*((int32_t *)(frame_data + 36 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 4)) = crc2_cal;
			*((int32_t *)(frame_data + 36 + (pen_numCol1 * pen_numRow1 + pen_numCol2 * pen_numRow2) * 2 * 2 + 8)) = ~crc2_cal;
			crc2 = crc2_cal;
		}
		#endif //NVT_CRC_DATA_ENABLE
	} else { // other frame data type
		printk("nvt-ts:     frame_data_type = %d\n", frame_data_type);
	}
	#if NVT_CRC_DATA_ENABLE
	if (checksum_correct) {
		// re-calculate crc using xm's original thp_crc32_check(), and put them to crc, ~crc fields
		crc_cal = thp_crc32_check((int32_t *)(frame_data_packet + 20), crc_len);
		*((int32_t *)(frame_data_packet + 4)) = crc_cal;
		*((int32_t *)(frame_data_packet + 12)) = ~crc_cal;
		crc = crc_cal;
	}
	#endif //NVT_CRC_DATA_ENABLE
	#if FRAME_DATA_PACKET_DEBUG_PRINT
	if (print_debug_data) {
		if (frame_data_type == 3)
			printk("nvt-ts:     crc_cal=0x%08X, crc=0x%08X\n", crc_cal, crc);
		else if (frame_data_type == 6 || frame_data_type == 7 || frame_data_type == 9 || frame_data_type == 17 || frame_data_type == 29)
			printk("nvt-ts:     crc2_cal=0x%08X, crc2=0x%08X, crc_cal=0x%08X, crc=0x%08X\n", crc2_cal, crc2, crc_cal, crc);
	}
	#endif /* #if FRAME_DATA_PACKET_DEBUG_PRINT */
}
#endif //NVT_PARSE_DATA_ENABLE

#define XM_HTC_DEFAULT_FINGER_PACKET_LEN   0x04C3
#define XM_HTC_DEFAULT_STYLUS_PACKET_LEN   0x050D

static irqreturn_t nvt_ts_work_func_irq_handler(int irq, void *data)
{
	// printk("nvt-ts:         ------------ -nvt_ts_work_func_irq_handler  CTP_SPI_READ enter!\n");
    return IRQ_WAKE_THREAD;
}

/*******************************************************
Description:
	Novatek touchscreen work function.

return:
	n.a.
*******************************************************/
static irqreturn_t nvt_ts_work_func(int irq, void *data)
{
	int32_t ret = -1;
	// uint8_t point_data[POINT_DATA_LEN + PEN_DATA_LEN + 1 + DUMMY_BYTES] = {0};
	uint8_t *point_data;
	uint8_t input_id = 0;
	uint8_t pen_format_id;


#if TOUCH_THP_SUPPORT
	uint16_t protocol_type;
	uint16_t crc;  	// check packet crc  crc_len  crc_r  crc_r_len
	uint16_t crc_r;
	int32_t crc_len;
	int32_t crc_r_len;
	struct tp_frame *tp_frame = (struct tp_frame *)get_raw_data_base(0);
	struct timespec64 time;
	uint8_t *frame_data_packet;
#endif 

	static struct task_struct *touch_task = NULL;
	struct sched_param par = { .sched_priority = MAX_RT_PRIO - 1 };
	struct cpumask irq_thread_cpu_mask;

	if (touch_task == NULL) {
		/* touch prio improve */
		touch_task = current;
		printk("nvt-ts:     touch_irq_thread prio improve to %d", MAX_RT_PRIO - 1);
		sched_setscheduler_nocheck(touch_task, SCHED_FIFO, &par);

		cpumask_clear(&irq_thread_cpu_mask);
		for (int cpu = 0; cpu <= 2; cpu++) {
			cpumask_set_cpu(cpu, &irq_thread_cpu_mask);
		}
		set_cpus_allowed_ptr(touch_task, &irq_thread_cpu_mask);
		printk("nvt-ts:     touch_irq_thread CPU affinity %d ", touch_task->nr_cpus_allowed);
	}

	if (ts->ts_selftest_process) {
		printk("nvt-ts:      ts_selftest_process is true");
		return IRQ_HANDLED;
	}

	mutex_lock(&ts->lock);

	#if WAKEUP_GESTURE
	if (bTouchIsAwake == 0) {
		pm_wakeup_event(&ts->client->dev, 5000);
	}
	#endif

	/* gesture mode setup */
	if (ts->dev_pm_suspend) {
		ret = wait_for_completion_timeout(&ts->dev_pm_suspend_completion, msecs_to_jiffies(500));
		if (!ret) {
			printk("nvt-ts:     system(spi) can't finished resuming procedure, skip it\n");
			goto XFER_ERROR;
		}
	}

	/* gesture mode setup end */
	point_data = ts->data_buf;
	ts->data_buf[0] = 0x00;

	#if WAKEUP_GESTURE
	if (bTouchIsAwake == 0) {
		ret = CTP_SPI_READ(ts->client, ts->data_buf, 256 + 1);
		if (ret < 0) {
			printk("nvt-ts:     CTP_SPI_READ failed!(%d)\n", ret);
			goto XFER_ERROR;
		}
		#if NVT_TOUCH_WDT_RECOVERY
		/* ESD protect by WDT */
		if (nvt_wdt_fw_recovery(point_data)) {
			printk("nvt-ts:  [DIS-TF-TOUCH] Recover for fw reset, %02X\n", point_data[1]);
			if (point_data[1] == 0xFE) {
				nvt_sw_reset_idle();
				nvt_clear_aci_error_flag();
			}
			nvt_read_fw_history_all();
			nvt_read_print_fw_flow_debug_message();
			nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
			nvt_read_fw_history_all();
			nvt_fw_reload_recovery();
			goto XFER_ERROR;
		}
		#endif /* #if NVT_TOUCH_WDT_RECOVERY */
		ret = nvt_ts_point_data_checksum(point_data, POINT_DATA_CHECKSUM_LEN);
		if (ret) {
			goto XFER_ERROR;
		}
		input_id = (uint8_t)(point_data[1] >> 3);
		nvt_ts_wakeup_gesture_report(input_id, point_data);
		if (ts->pen_support) {
			pen_format_id = point_data[66];
			nvt_ts_pen_gesture_report(pen_format_id);
		}
		goto XFER_ERROR;
	}
	#endif
	ret = CTP_SPI_READ(ts->client, ts->data_buf, 256 + ts->frame_len + 1);
	if (ret < 0) {
		printk("nvt-ts:     CTP_SPI_READ failed!(%d)\n", ret);
		goto XFER_ERROR;
	}

	#if NVT_TOUCH_WDT_RECOVERY
	/* ESD protect by WDT */
	if (nvt_wdt_fw_recovery(point_data)) {
		printk("nvt-ts:  [DIS-TF-TOUCH]  Recover for fw reset, %02X\n", point_data[1]);
		if (point_data[1] == 0xFE) {
			nvt_sw_reset_idle();
			nvt_clear_aci_error_flag();
		}
		nvt_read_fw_history_all();
		nvt_read_print_fw_flow_debug_message();
		nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
		nvt_read_fw_history_all();
		nvt_fw_reload_recovery();
		goto XFER_ERROR;
	}
	#endif /* #if NVT_TOUCH_WDT_RECOVERY */

	//thp start 6.8
	if (NULL == tp_frame) {
		printk("nvt-ts:     tp_frame is null, check get_raw_data_base");
		goto XFER_ERROR;
	}
	// goto XFER_ERROR;
	memcpy(ts->eventbuf_debug, ts->data_buf + 1, EVENTBUF_DEBUG_LEN);
	frame_data_packet = ts->data_buf + 1 + 256;
	// check packet  crc  crc_len  crc_r  crc_r_len
	protocol_type =  *((uint8_t *)(frame_data_packet));
	crc = *((uint16_t *)(frame_data_packet + 4));
	crc_len = *((int32_t *)(frame_data_packet + 8));
	crc_r = *((uint16_t *)(frame_data_packet + 12));
	crc_r_len = *((int32_t *)(frame_data_packet + 16));
	if ((crc_len != XM_HTC_DEFAULT_FINGER_PACKET_LEN && crc_len != XM_HTC_DEFAULT_STYLUS_PACKET_LEN) || (crc_r != ((uint16_t)(~crc))) || (crc_r_len != ~crc_len)) {
		printk("nvt-ts:  packet  wrong(crc=0x%04X, crc_len=0x%08X, crc_r=0x%04X, crc_r_len=0x%08X)!\n",
				crc, crc_len, crc_r, crc_r_len);
		goto XFER_ERROR;
	}
	/* copy data */
	ktime_get_real_ts64(&time);
	memcpy(tp_frame->tp_raw, frame_data_packet, ts->frame_len);
	tp_frame->time_ns = timespec64_to_ns(&time);
	tp_frame->fod_pressed = 0;
	tp_frame->fod_trackingId = 0;
	tp_frame->frame_cnt = thp_cnt++;
	notify_raw_data_update(0);
	#if NVT_PARSE_DATA_ENABLE
	nvt_ts_prase_data_func(frame_data_packet)
	#endif //NVT_PARSE_DATA_ENABLE
	// ToDo: memcpy(xm_htc_frame_data_buf, frame_data_packet, sizeof(struct xm_htc_frame_data));

	//thp end 6.8
XFER_ERROR:

	mutex_unlock(&ts->lock);

	return IRQ_HANDLED;
}


/*******************************************************
Description:
	Novatek touchscreen check chip version trim function.

return:
	Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int32_t nvt_ts_check_chip_ver_trim(struct nvt_ts_hw_reg_addr_info hw_regs)
{
	uint8_t buf[8] = {0};
	int32_t retry = 0;
	int32_t list = 0;
	int32_t i = 0;
	int32_t found_nvt_chip = 0;
	int32_t ret = -1;
	uint8_t enb_casc = 0;

	/* hw reg mapping */
	ts->chip_ver_trim_addr = hw_regs.chip_ver_trim_addr;
	ts->swrst_sif_addr = hw_regs.swrst_sif_addr;
	ts->bld_spe_pups_addr = hw_regs.bld_spe_pups_addr;

	NVT_LOG("check chip ver trim with chip_ver_trim_addr=0x%06x, "
			"swrst_sif_addr=0x%06x, bld_spe_pups_addr=0x%06x\n",
			ts->chip_ver_trim_addr, ts->swrst_sif_addr, ts->bld_spe_pups_addr);

	//---Check for 5 times---
	for (retry = 5; retry > 0; retry--) {

		nvt_bootloader_reset();

		nvt_set_page(ts->chip_ver_trim_addr);

		buf[0] = ts->chip_ver_trim_addr & 0x7F;
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		buf[6] = 0x00;
		CTP_SPI_WRITE(ts->client, buf, 7);

		buf[0] = ts->chip_ver_trim_addr & 0x7F;
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		buf[6] = 0x00;
		CTP_SPI_READ(ts->client, buf, 7);
		NVT_LOG("buf[1]=0x%02X, buf[2]=0x%02X, buf[3]=0x%02X, buf[4]=0x%02X, buf[5]=0x%02X, buf[6]=0x%02X\n",
			buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

		// compare read chip id on supported list
		for (list = 0; list < (sizeof(trim_id_table) / sizeof(struct nvt_ts_trim_id_table)); list++) {
			found_nvt_chip = 0;

			// compare each byte
			for (i = 0; i < NVT_ID_BYTE_MAX; i++) {
				if (trim_id_table[list].mask[i]) {
					if (buf[i + 1] != trim_id_table[list].id[i])
						break;
				}
			}

			if (i == NVT_ID_BYTE_MAX) {
				found_nvt_chip = 1;
			}

			if (found_nvt_chip) {
				NVT_LOG("This is NVT touch IC\n");
				if (trim_id_table[list].mmap->ENB_CASC_REG.addr) {
					/* check single or cascade */
					nvt_read_reg(trim_id_table[list].mmap->ENB_CASC_REG, &enb_casc);
					/* NVT_LOG("ENB_CASC=0x%02X\n", enb_casc); */
					if (enb_casc & 0x01) {
						NVT_LOG("Single Chip\n");
						ts->mmap = trim_id_table[list].mmap;
						ts->is_cascade = false;
					} else {
						NVT_LOG("Cascade Chip\n");
						ts->mmap = trim_id_table[list].mmap_casc;
						ts->is_cascade = true;
					}
				} else {
					/* for chip that do not have ENB_CASC */
					ts->mmap = trim_id_table[list].mmap;
				}
				/* hw info */
				ts->hw_crc = trim_id_table[list].hwinfo->hw_crc;
				ts->auto_copy = trim_id_table[list].hwinfo->auto_copy;
				ts->bld_multi_header = trim_id_table[list].hwinfo->bld_multi_header;

				/* hw reg re-mapping */
				ts->chip_ver_trim_addr = trim_id_table[list].hwinfo->hw_regs->chip_ver_trim_addr;
				ts->swrst_sif_addr = trim_id_table[list].hwinfo->hw_regs->swrst_sif_addr;
				ts->bld_spe_pups_addr = trim_id_table[list].hwinfo->hw_regs->bld_spe_pups_addr;

				NVT_LOG("set reg chip_ver_trim_addr=0x%06x, "
						"swrst_sif_addr=0x%06x, bld_spe_pups_addr=0x%06x\n",
						ts->chip_ver_trim_addr, ts->swrst_sif_addr, ts->bld_spe_pups_addr);

				ret = 0;
				goto out;
			} else {
				ts->mmap = NULL;
				ret = -1;
			}
		}

		msleep(10);
	}

out:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen check chip version trim loop
	function. Check chip version trim via hw regs table.

return:
	Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int32_t nvt_ts_check_chip_ver_trim_loop(void) {
    uint8_t i = 0;
	int32_t ret = 0;

	struct nvt_ts_hw_reg_addr_info hw_regs_table[] = {
		hw_reg_addr_info,
		hw_reg_addr_info_old_w_isp,
		hw_reg_addr_info_legacy_w_isp
	};

    for (i = 0; i < (sizeof(hw_regs_table) / sizeof(struct nvt_ts_hw_reg_addr_info)); i++) {
        //---check chip version trim---
        ret = nvt_ts_check_chip_ver_trim(hw_regs_table[i]);
		if (!ret) {
			break;
		}
    }

	return ret;
}

/* MIPP Start */

static int32_t nvt_set_pen_hopping_ack(void)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;
	int32_t i = 0;
	int32_t retry = 5;

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	NVT_LOG("++\n");

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	//---set xdata index to EVENT BUF ADDR---
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("Set event buffer index fail!\n");
		goto out;
	}

	for (i = 0; i < retry; i++) {
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x7C;

		ret = CTP_SPI_WRITE(ts->client, buf, 2);
		if (ret < 0) {
			NVT_ERR("spi write fail!\n");
			goto out;
		}

		usleep_range(20000, 20000);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);
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
		ret = 0;
	}

out:
	NVT_LOG("--\n");
	return ret;
}

/* MIPP End */

//thp start 6.8
int nvt_enable_touch_raw(int en)
{
	// if (en) {
		//cleanUp(0);
		ts->enable_touch_raw = true;
		add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_63, 1, &ts->is_usb_exist);
	// } else {
	// 	//cleanUp(1);
	// 	ts->enable_touch_raw = false;
	// }
	// ts->need_update_firmware = true;
	// nvt_match_fw();
	mutex_lock(&ts->lock);
	nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	mutex_unlock(&ts->lock);
	return 0;
}
//thp end 6.8

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
// static struct xiaomi_touch_interface xiaomi_touch_interfaces;
static hardware_operation_t hardware_operation;
static hardware_param_t hardware_param;
#define VALUE_TYPE_SIZE 5
static int touch_mode[DATA_MODE_35][VALUE_TYPE_SIZE];

static void nvt_init_touchmode_data(void)
{
	int i;

	NVT_LOG("%s,ENTER\n", __func__);
	/* Touch Game Mode Switch */
	touch_mode[DATA_MODE_0][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_0][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_0][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_0][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_0][GET_CUR_VALUE] = 0;

	/* Acitve Mode */
	touch_mode[DATA_MODE_1][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_1][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_1][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_1][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_1][GET_CUR_VALUE] = 0;

	/* Tap Sensivity */
	touch_mode[DATA_MODE_2][GET_MAX_VALUE] = 4;
	touch_mode[DATA_MODE_2][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_2][GET_DEF_VALUE] = 2;
	touch_mode[DATA_MODE_2][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_2][GET_CUR_VALUE] = 0;

	/* Touch Tolerance */
	touch_mode[DATA_MODE_3][GET_MAX_VALUE] = 4;
	touch_mode[DATA_MODE_3][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_3][GET_DEF_VALUE] = 2;
	touch_mode[DATA_MODE_3][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_3][GET_CUR_VALUE] = 0;

	/* Touch Aim Sensitivity */
	touch_mode[DATA_MODE_4][GET_MAX_VALUE] = 4;
	touch_mode[DATA_MODE_4][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_4][GET_DEF_VALUE] = 2;
	touch_mode[DATA_MODE_4][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_4][GET_CUR_VALUE] = 0;

	/* [Touch Tap Stability */
	touch_mode[DATA_MODE_5][GET_MAX_VALUE] = 4;
	touch_mode[DATA_MODE_5][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_5][GET_DEF_VALUE] = 2;
	touch_mode[DATA_MODE_5][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_5][GET_CUR_VALUE] = 0;

	touch_mode[DATA_MODE_6][GET_MAX_VALUE] = 3;
	touch_mode[DATA_MODE_6][GET_MIN_VALUE] = 1;
	touch_mode[DATA_MODE_6][GET_DEF_VALUE] = 1;
	touch_mode[DATA_MODE_6][SET_CUR_VALUE] = 1;
	touch_mode[DATA_MODE_6][GET_CUR_VALUE] = 1;

	/* Panel orientation*/
	touch_mode[DATA_MODE_8][GET_MAX_VALUE] = 3;
	touch_mode[DATA_MODE_8][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_8][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_8][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_8][GET_CUR_VALUE] = 0;

	/* Edge filter area*/
	touch_mode[DATA_MODE_7][GET_MAX_VALUE] = 3;
	touch_mode[DATA_MODE_7][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_7][GET_DEF_VALUE] = 2;
	touch_mode[DATA_MODE_7][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_7][GET_CUR_VALUE] = 0;

	/* Resist RF interference*/
	touch_mode[DATA_MODE_12][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_12][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_12][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_12][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_12][GET_CUR_VALUE] = 0;

	/* DATA_MODE_20 */
	touch_mode[DATA_MODE_20][GET_MAX_VALUE] = 0x18;
	touch_mode[DATA_MODE_20][GET_MIN_VALUE] = -1;
	touch_mode[DATA_MODE_20][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_20][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_20][GET_CUR_VALUE] = 0;

	/* DATA_MODE_14 */
	touch_mode[DATA_MODE_14][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_14][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_14][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_14][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_14][GET_CUR_VALUE] = 0;

	/* DATA_MODE_22 */
	touch_mode[DATA_MODE_22][GET_MAX_VALUE] = 0xFFFF;
	touch_mode[DATA_MODE_22][GET_MIN_VALUE] = 1;
	touch_mode[DATA_MODE_22][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_22][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_22][GET_CUR_VALUE] = 0;

	/* DATA_MODE_24 */
	touch_mode[DATA_MODE_24][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_24][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_24][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_24][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_24][GET_CUR_VALUE] = 0;

	/* DATA_MODE_33 */
	touch_mode[DATA_MODE_33][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_33][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_33][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_33][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_33][GET_CUR_VALUE] = 0;

	/* DATA_MODE_29 */
	touch_mode[DATA_MODE_29][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_29][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_29][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_29][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_29][GET_CUR_VALUE] = 0;

	for (i = 0; i < DATA_MODE_35; i++) {
		NVT_LOG("mode:%d, set cur:%d, get cur:%d, def:%d min:%d max:%d\n",
			i,
			touch_mode[i][SET_CUR_VALUE],
			touch_mode[i][GET_CUR_VALUE],
			touch_mode[i][GET_DEF_VALUE],
			touch_mode[i][GET_MIN_VALUE],
			touch_mode[i][GET_MAX_VALUE]);
	}

	return;
}

static void nvt_set_gesture_mode(int value)
{
	if (!ts) {
		NVT_ERR("Driver data is not initialized");
		return;
	}

	if (ts->ic_state <= NVT_IC_RESUME_IN && ts->ic_state != NVT_IC_INIT ) {
		ts->gesture_command_delayed = value;
		NVT_LOG("Screen off, don't set gesture flag(%02x), ic state is %d", value, ts->ic_state);
	} else if (ts->ic_state >= NVT_IC_RESUME_OUT) {
		NVT_LOG("Screen on, set gesture flag(%02x), ic state is %d", value, ts->ic_state);
		ts->gesture_command = value;
		dsi_panel_gesture_enable(!!ts->gesture_command);
	}
}

static int nvt_enable_gesture_mode(int value)
{
	int32_t ret = 0;
	uint8_t doubletap_enable = 0;
	uint8_t pen_shorthand_enable = 0;
	uint8_t singletap_enable = 0;
	uint8_t buf[4] = {0};

	// set gesture enable/disable
	nvt_xm_htc_set_gesture_switch(ts->gesture_command & 0xFFFF);

	if (value) {
		msleep(35);
		/*---write command to enter "wakeup gesture mode"---*/
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x13;
		ret = CTP_SPI_WRITE(ts->client, buf, 2);
		if (ret < 0) {
			NVT_ERR("set cmd failed!\n");
		}
		doubletap_enable = ts->gesture_command & 0x01;
		pen_shorthand_enable = ts->gesture_command & 0x02;
		singletap_enable = ts->gesture_command & 0x04;
		NVT_LOG("Gesture mode on, %s doubletap gesture, %s pen shorthand gesture, %s singletap gesture\n",
				doubletap_enable ? "Enable" : "Disable", pen_shorthand_enable ? "Enable" : "Disable", singletap_enable ? "Enable" : "Disable");
	} else {
		/*---write command to enter "deep sleep mode"---*/
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x11;
		ret = CTP_SPI_WRITE(ts->client, buf, 2);
		if (ret < 0) {
			NVT_ERR("set cmd failed!\n");
		}
		/* SPI Pinctrl setup */
		if (ts->ts_pinctrl) {
			ret = pinctrl_select_state(ts->ts_pinctrl, ts->pinctrl_state_suspend);
			if (ret < 0) {
				NVT_ERR("Failed to select %s pinstate %d\n",
					PINCTRL_STATE_SUSPEND, ret);
			}
		} else {
			NVT_ERR("Failed to init pinctrl\n");
		}
		/* SPI Pinctrl setup end */

		NVT_LOG("Enter deep sleep mode\n");
	}

	return ret;
}

//thp start 6.8
static int nvt_enable_game_mode(int value)
{
	int ret;
	uint8_t buf[8] = {0};

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return -EBUSY;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return -EBUSY;
	}

	NVT_LOG("++\n");
	/* ---set xdata index to EVENT BUF ADDR--- */
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("Set event buffer index fail!\n");
		goto  nvt_enable_game_mode_err;
	}

	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0xBF;	//0x50
	buf[2] = 0x0F;	//0x51
	buf[3] = 0x00; // 0x52 write:0x00 read:0x01
	buf[4] = value & 0xFF;	//0x53	value_l
	buf[5] = 0x00;	//0x54  value_h

	ret = CTP_SPI_WRITE(ts->client, buf, 6);
	if (ret < 0) {
		NVT_ERR("Write sensitivity switch command fail!\n");
		goto nvt_enable_game_mode_err;
	}

nvt_enable_game_mode_err:
	NVT_LOG("--\n");
	return ret;
}

static int nvt_enable_thp_onoff(u32 value)
{
	ts->enable_touch_raw = !!value;
	return 1;
}

static int nvt_enable_thp_selfcap_scan(int en)
{
	return 0;
}

//thp end 6.8

/**
 *  stylus enable mode ture table:
 *  bluetooth_connect|charge_connect   |whitelist_game   |gamemode_enable  |enable
 *  0                |0/1              |0/1              |0/1              |0
 *  1                |1                |0/1              |0/1              |0
 *  1                |0                |1                |0/1              |1
 *  1                |0                |0                |1                |0
 *  1                |0                |0                |0                |1
 */
int update_pen_status(bool enforce_send_cmd) {
	int32_t ret = 0;
	int enable = 0;
	int enable_stylus_in_gamemode = 0;
	static int enable_last_time = -1;

	NVT_LOG("++, enforce_send_cmd:%d, bluetooth:%d, charge:%d, gamemode:%d, game_in_whitelist:%d, enable_last_time:%d\n",
		enforce_send_cmd, ts->pen_bluetooth_connect, ts->pen_charge_connect,
		ts->gamemode_enable, ts->game_in_whitelist, enable_last_time);

	mutex_lock(&ts->pen_switch_lock);
	if (!bTouchIsAwake || !ts) {
		NVT_LOG("touch suspend, stop switch");
		goto nvt_set_pen_enable_out;
	}

#ifdef CONFIG_FACTORY_BUILD
	/* pen_bluetooth_connect enable by default */
	ts->pen_bluetooth_connect = 1;
	NVT_LOG("This is factory mode, set pen_bluetooth_connect as 1");
#endif // CONFIG_FACTORY_BUILD
	enable_stylus_in_gamemode = ts->game_in_whitelist ? 1 : ts->gamemode_enable ? 0 : 1;
	enable = (ts->pen_bluetooth_connect) && (!(ts->pen_charge_connect) && enable_stylus_in_gamemode);
	if(enable_last_time == enable) {
		if(!enforce_send_cmd) {
			goto nvt_set_pen_enable_out;
		}
	}

	ret = nvt_xm_htc_set_stylus_enable(!!enable);
	if (ret < 0) {
		NVT_ERR("nvt_xm_htc_set_stylus_enable fail! ret=%d\n", ret);
		goto nvt_set_pen_enable_out;
	}

#ifdef TOUCH_STYLUS_SUPPORT
	/* notify surfaceflinger */
	if(enforce_send_cmd && (enable_last_time == enable) ) {
		/* skip notify surfaceflinger */
		NVT_LOG("enable_last_time = %d, enable = %d, enforce_send_cmd = %d, skip notify surfaceflinger", enable_last_time, enable, enforce_send_cmd);
		goto nvt_set_pen_enable_out;
	}
	update_stylus_connect_status_value(!!enable);
#endif

#if TOUCH_THP_SUPPORT
	add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_142, 1, &enable);
#endif

	enable_last_time = enable;

nvt_set_pen_enable_out:
#ifdef CONFIG_FACTORY_BUILD
	ret = nvt_set_pen_switch(SUPPORT_M80P);
#else
	if (enable)
		ret = nvt_set_pen_switch(ts->pen_switch);
#endif
	if (ret < 0) {
		NVT_ERR("nvt_set_pen_switch fail! ret=%d\n", ret);
	}
	mutex_unlock(&ts->pen_switch_lock);
	NVT_LOG("--\n");

	return ret;
}

void report_release_pen_event(struct input_dev *dev) {
	input_report_abs(dev, ABS_X, 0);
	input_report_abs(dev, ABS_Y, 0);
	input_report_abs(dev, ABS_PRESSURE, 0);
	input_report_abs(dev, ABS_TILT_X, 0);
	input_report_abs(dev, ABS_TILT_Y, 0);
	input_report_abs(dev, ABS_DISTANCE, 0);
	input_report_key(dev, BTN_TOUCH, 0);
	input_report_key(dev, BTN_TOOL_PEN, 0);
	input_sync(dev);
}

static void release_pen_event(void) {
	if (ts && ts->pen_input_dev_m80p && ts->pen_input_dev_p81c) {
		report_release_pen_event(ts->pen_input_dev_m80p);
		report_release_pen_event(ts->pen_input_dev_p81c);
	}
}

void nvt_fw_reload_recovery(void)
{
	int i = 0;

	NVT_LOG("++\n");

	/* release all touches */
	if(ts && ts->input_dev) {
#if MT_PROTOCOL_B
		for (i = 0; i < ts->max_touch_num; i++) {
			input_mt_slot(ts->input_dev, i);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
		}
#endif
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
#if !MT_PROTOCOL_B
		input_mt_sync(ts->input_dev);
#endif
		input_sync(ts->input_dev);
	}
	/* release pen event */
	release_pen_event();
	/* reload fw should update_pen_status again */
	if (ts->pen_support)
		update_pen_status(true);
	/* reload fw should setup power_supply mode again */
	ts->is_usb_exist = -1;
	queue_work(ts->event_wq, &ts->power_supply_work);
	/* reload gesture cmd when open gesture*/
	if (!(!ts) && (!bTouchIsAwake) && (ts->gesture_command)) {
		nvt_enable_gesture_mode(true);
	}
	/* reload fw should update touchfeature again */
	if(!ts->gamemode_enable) {
		touch_mode[DATA_MODE_8][GET_CUR_VALUE] =
			touch_mode[DATA_MODE_8][GET_DEF_VALUE];

		touch_mode[DATA_MODE_12][GET_CUR_VALUE] =
			touch_mode[DATA_MODE_12][GET_DEF_VALUE];
	} else {
		for(i = 0; i < DATA_MODE_13; i++) {
			touch_mode[i][GET_CUR_VALUE] =
				touch_mode[i][GET_DEF_VALUE];
		}
	}
	// queue_work(ts->set_touchfeature_wq, &ts->set_touchfeature_work);

	NVT_LOG("--\n");
}

static void nvt_ic_switch_mode(u8 gesture_type)
{
	int gesture_command = 0;
	if (gesture_type & GESTURE_DOUBLETAP_EVENT)
		gesture_command |= 0x01;
	if ((gesture_type & GESTURE_STYLUS_SINGLETAP_EVENT) && ts->pen_bluetooth_connect)
		gesture_command |= 0x02;
	if (gesture_type & GESTURE_PAD_SINGLETAP_EVENT)
		gesture_command |= 0x04;
	nvt_set_gesture_mode(gesture_command);
}

/* THP scheme this function will not be called */
static void nvt_cmd_mode_update(long mode_update_flag, int mode_value[DATA_MODE_35])
{
	int nvt_value;

	if (mode_update_flag & (1 << DATA_MODE_0)) {
		nvt_value = mode_value[DATA_MODE_0];
		NVT_LOG("Touch gamemode %s", nvt_value ? "on" : "off");
		mutex_lock(&ts->lock);
		nvt_enable_game_mode(nvt_value);
		mutex_unlock(&ts->lock);
		ts->gamemode_enable = !!nvt_value;
	}
}

static void nvt_set_cur_value(int mode, int *value)
{
	int nvt_mode = mode;
	int nvt_value = value[0];

	if (nvt_mode < DATA_MODE_35) {
		if (nvt_value > touch_mode[nvt_mode][GET_MAX_VALUE]) {
			nvt_value = touch_mode[nvt_mode][GET_MAX_VALUE];
		} else if (nvt_value < touch_mode[nvt_mode][GET_MIN_VALUE]) {
			nvt_value = touch_mode[nvt_mode][GET_MIN_VALUE];
		}
		touch_mode[nvt_mode][SET_CUR_VALUE] = nvt_value;
	}

	if (nvt_mode == IC_MODE_57 && ts) {
		if (nvt_value == 1) {
			ts->gamemode_enable = 1;
		} else if (nvt_value == 0) {
			ts->gamemode_enable = 0;
			NVT_LOG("exit game mode, stylus game_in_whitelist changed from %d to %d ",ts->game_in_whitelist, ts->game_in_whitelist_bak);
			ts->game_in_whitelist = ts->game_in_whitelist_bak;
		}
		if (ts->pen_support) {
			mutex_lock(&ts->lock);
			update_pen_status(false);
			mutex_unlock(&ts->lock);
		}
		return;
	}

	if (nvt_mode == DATA_MODE_20 && ts) {
		/* cover system server crash */
		if (nvt_value == -1) {
			ts->pen_count = 0;
			release_pen_event();
			return;
		}

		if (!!(nvt_value >> 4)) {
			/* connect logic */
			if ((nvt_value & 0x0F) == 3) {
				ts->pen_count ++;
				ts->cur_pen = PEN_M80P;
				ts->pen_switch = SUPPORT_M80P;
				NVT_LOG("Xiaomi stylus m80p");
			} else if ((nvt_value & 0x0F) == 1 || (nvt_value & 0x0F) == 2) {
				ts->pen_shield_flag = 1;//shield K81P/M81P
				NVT_LOG("Xiaomi stylus Generation one connect, sheild pen connection");
			} else if ((nvt_value & 0x0F) == 8) { //P81C
				ts->pen_count ++;
				ts->cur_pen = PEN_P81C;
				ts->pen_switch = SUPPORT_P81C;
				NVT_LOG("Xiaomi stylus p81c");
			}
		} else {
			/* disconnect logic */
			if ((nvt_value & 0x0F) == 3) {
				ts->pen_count -- ;
				NVT_LOG("disconnect m80p");
			} else if ((nvt_value & 0x0F) == 1 || (nvt_value & 0x0F) == 2) {
				ts->pen_shield_flag = 0;//open it
				NVT_LOG("Xiaomi stylus Generation one disconnect, open pen connection");
			}if ((nvt_value & 0x0F) == 8) {
				ts->pen_count -- ;
				NVT_LOG("disconnect p81c");
			}
		}

		if (ts->pen_shield_flag){
			/* sheild pen connection */
			ts->pen_bluetooth_connect = 0;
		} else {
			if (!!ts->pen_count) {
				/* M80P connect num >= 1 */
				ts->pen_bluetooth_connect = 1;
			} else {
				/* M80P connect num = 0 */
				ts->pen_bluetooth_connect = 0;
			}
		}
		NVT_LOG("nvt_value is 0x%02X, pen status is %s, pen id is %d, pen_bluetooth_connect is %d, pen_count is %d",
					nvt_value, (nvt_value >> 4) ? "connect":"disconnect", nvt_value & 0x0F, ts->pen_bluetooth_connect, ts->pen_count);
		nvt_set_gesture_mode((ts->pen_bluetooth_connect && driver_get_touch_mode(TOUCH_ID, DATA_MODE_24)) ?
							(ts->gesture_command | 0x02) : (ts->gesture_command & 0xFD));
		if (ts->pen_support) {
			mutex_lock(&ts->lock);
			update_pen_status(false);
			mutex_unlock(&ts->lock);
		}
		release_pen_event();
		return;
	}
	if (nvt_mode == DATA_MODE_29 && ts) {
		ts->pen_static_status = nvt_value;
		NVT_LOG("ts->pen_static_status:%d", ts->pen_static_status);
		if (bTouchIsAwake) {
			mutex_lock(&ts->lock);
			nvt_set_active_pen_stationary(ts->pen_static_status);
			mutex_unlock(&ts->lock);
		}
		return;
	}
	/* MIPP Start */
	if (nvt_mode == DATA_MODE_22 && ts && nvt_value >= 0) {
		if (ts->pen_bluetooth_connect == 0) {
			NVT_LOG("MIPP stylus is disconnect, skip hopping frequency");
			return;
		}

		NVT_LOG("MIPP stylus hopping mode received: %d", nvt_value);
		if ((nvt_value & 0xFF) == MIPP_PEN_FREQUENCY) {
			if (ts->need_send_hopping_ack) {
				mutex_lock(&ts->lock);
				nvt_set_pen_hopping_ack();
				mutex_unlock(&ts->lock);
			}
		} else if ((nvt_value & 0xFF) == MIPP_PEN_VOLTAGE) {
			NVT_LOG("MIPP stylus voltage has mofified");
		} else if ((nvt_value & 0xFF) >= MIPP_BOTH_HOPPING_OFFSET &&
			(nvt_value & 0xFF) < MIPP_PEN_HOPPING_OFFSET) {
			ts->need_send_hopping_ack = true;
			nvt_pen_hopping_frequency(0, (nvt_value & 0xFF) - MIPP_BOTH_HOPPING_OFFSET);
		} else if ((nvt_value & 0xFF) >= MIPP_PEN_HOPPING_OFFSET &&
			(nvt_value & 0xFF) < MIPP_PEN_FREQUENCY) {
			ts->need_send_hopping_ack = false;
			nvt_pen_hopping_frequency(0, (nvt_value & 0xFF) - MIPP_PEN_HOPPING_OFFSET);
		} else {
			NVT_LOG("MIPP stylus id update %d", (nvt_value >> 8) & 0xFF);
		}
		return;
	}
	/* MIPP End */

	if (nvt_mode == DATA_MODE_176) {
		NVT_LOG("DATA_MODE_176 %d", nvt_value);
		nvt_pen_AG_update(0, nvt_value);
		return;
	}

	if (nvt_mode == DATA_MODE_177) {
		// move: 1, disable stylus button; hover/up: 0，enable stylus button
		NVT_LOG("MIPP stylus report status update %d", nvt_value);
		nvt_pen_report_status_update(0, nvt_value);
		return;
	}

	if (nvt_mode == DATA_MODE_6) {
		NVT_LOG("This is Expert Mode, mode is %d", nvt_value);
		touch_mode[DATA_MODE_2][SET_CUR_VALUE] =
			ts->gamemode_config[nvt_value - 1][0];
		touch_mode[DATA_MODE_3][SET_CUR_VALUE] =
			ts->gamemode_config[nvt_value - 1][1];
		touch_mode[DATA_MODE_4][SET_CUR_VALUE] =
			ts->gamemode_config[nvt_value - 1][2];
		touch_mode[DATA_MODE_5][SET_CUR_VALUE] =
			ts->gamemode_config[nvt_value - 1][3];
		touch_mode[DATA_MODE_7][SET_CUR_VALUE] =
			ts->gamemode_config[nvt_value - 1][4];
	}

	if (nvt_mode == DATA_MODE_33) {
		ts->game_in_whitelist_bak = !!nvt_value;
		NVT_LOG("MIPP stylus update game_in_whitelist_bak: %d ", ts->game_in_whitelist_bak);
	    if (ts->gamemode_enable && !nvt_value) {
			return;
		}

		if (ts->game_in_whitelist != ts->game_in_whitelist_bak) {
			NVT_LOG("stylus game_in_whitelist changed from %d to %d ",ts->game_in_whitelist, ts->game_in_whitelist_bak);
			ts->game_in_whitelist = ts->game_in_whitelist_bak;
			if (ts->pen_support) {
				mutex_lock(&ts->lock);
				update_pen_status(false);
				mutex_unlock(&ts->lock);
			}
		}

		return;
	}

#ifdef TOUCH_THP_SUPPORT
	if (nvt_mode == DATA_MODE_19 && ts && nvt_value >= 0) {
		NVT_LOG("DATA_MODE_19 value %d", nvt_value);
		if (nvt_value) {
			flush_workqueue(ts->event_wq);
			queue_work(ts->event_wq, &ts->resume_work);
		} else {
			nvt_ts_suspend(&ts->client->dev);
		}
		return;
	}

	if (nvt_mode == DATA_MODE_53 && ts && nvt_value >= 0) {
		if (ts->ts_selftest_process) {
			ts->ts_selftest_process_cmd = true;
			NVT_ERR("ts_selftest_process in true, delay!");
			return;
		}
		if (ts->enable_touch_raw && ts->ic_state >= NVT_IC_RESUME_OUT) {
			mutex_lock(&ts->lock);
			if (ts->ts_probe_start) {
				//disable_pen_input_device(ts->pen_input_dev_enable);
				if (ts->pen_support)
					update_pen_status(ts->pen_bluetooth_connect);
				ts->ts_probe_start = false;
			}
			switch (nvt_value) {
				case 0: //active
					nvt_xm_htc_set_op_mode(1);
					break;
				case 1: //idle
					nvt_xm_htc_set_op_mode(2);
					break;
				default:
					NVT_LOG("DATA_MODE_53 not support value %d", nvt_value);
					break;
			}
			mutex_unlock(&ts->lock);
		}
		return;
	}

	if (nvt_mode == DATA_MODE_124 && ts && nvt_value >= 0) {
		if (ts->ts_selftest_process) {
			ts->ts_selftest_process_cmd = true;
			NVT_ERR("ts_selftest_process in true, delay!");
			return;
		}
		if (ts->enable_touch_raw && ts->ic_state >= NVT_IC_RESUME_OUT) {
			mutex_lock(&ts->lock);
			nvt_xm_htc_set_idle_baseline_update();
			mutex_unlock(&ts->lock);
		}
		return;
	}

	if (nvt_mode == IC_MODE_58 && nvt_value >= 0) {
		if (bTouchIsAwake) {
			mutex_lock(&ts->lock);
			nvt_xm_htc_set_stylus_pressure(nvt_value);
			mutex_unlock(&ts->lock);
		}
		return;
	}

	if (nvt_mode == DATA_MODE_55 && nvt_value >= 0) {
		if (ts->enable_touch_raw)
			nvt_enable_thp_selfcap_scan(nvt_value);
		return;
	}
	if (nvt_mode == DATA_MODE_56 && ts && nvt_value >= 0) {
		nvt_enable_thp_onoff(nvt_value);
		return;
	}
	if (nvt_mode == DATA_MODE_57 && ts && nvt_value >= 0) {
		return;
	}

	if (nvt_mode == DATA_MODE_46 && ts && nvt_value >= 0) {
		mutex_lock(&ts->lock);
		nvt_xm_htc_set_idle_high_base_en(nvt_value);
		mutex_unlock(&ts->lock);
		return;
	}

	if (nvt_mode == DATA_MODE_64 && ts && nvt_value >= 0) {
		return;
	}
#endif

	return;
}

static int nvt_pen_charge_state_notifier_callback(struct notifier_block *self, unsigned long event, void *data) {
	ts->pen_charge_connect = !!event;
	NVT_LOG("pen_charge_connect is %d", ts->pen_charge_connect);
	release_pen_event();
	schedule_work(&ts->pen_charge_state_change_work);
	return 0;
}

static void nvt_pen_charge_state_change_work(struct work_struct *work)
{
	if (ts->pen_support) {
		mutex_lock(&ts->lock);
		update_pen_status(false);
		mutex_unlock(&ts->lock);
	}
}

static void nvt_set_charge_state(int state)
{
	int32_t ret;
	uint8_t buf[3] = {0};

	if (!ts) {
		NVT_ERR("Driver data is not initialized");
		return;
	}

	if (ts->xm_htc_sw_reset) {
		NVT_ERR("ts->xm_htc_sw_reset=%d\n", ts->xm_htc_sw_reset);
		return;
	}

	if (ts->nvt_tool_in_use) {
		NVT_ERR("NVT tool in use.\n");
		return;
	}

	mutex_lock(&ts->lock);
	if (bTouchIsAwake) {
		ts->is_usb_exist = !!state;
		NVT_LOG("Power supply state:%d", ts->is_usb_exist);

		buf[0] = EVENT_MAP_HOST_CMD;
		if (ts->is_usb_exist) {
			buf[1] = 0x53;
		} else {
			buf[1] = 0x51;
		}
		buf[2] = 0x00;

		ret = CTP_SPI_WRITE(ts->client, buf, 3);
		if (ret) {
			NVT_ERR("USB status set failed, ret = %d!", ret);
		}
	}
	mutex_unlock(&ts->lock);
}

static void nvt_power_supply_work(struct work_struct *work)
{
	int is_usb_exist = 0;

	is_usb_exist = !!power_supply_is_system_supplied();
	nvt_set_charge_state(is_usb_exist);
}

static char nvt_touch_vendor_read(void)
{
	char value = '4';
	NVT_LOG("%s Get touch vendor: %c\n", __func__, value);
	return value;
}


static u8 nvt_panel_vendor_read(void)
{
	char value = '0';
	int ret = 0;
	if (!ts)
		return value;
	if (ts->lkdown_readed) {
		value = ts->lockdown_info[0];
	} else {
		ret = mi_dsi_panel_lockdown_info_read(ts->lockdown_info);
		if (ret <= 0) {
			NVT_ERR("can't get lockdown info");
			return value;
		}
		value = ts->lockdown_info[0];
		ts->lkdown_readed = true;
	}
	NVT_LOG("%s Get panel vendor: %d\n", __func__, value);

	return value;
}


static u8 nvt_panel_color_read(void)
{
	char value = '2';
	return value;
}


static u8 nvt_panel_display_read(void)
{
	char value = '0';
	int ret = 0;
	if (!ts)
		return value;

	if (ts->lkdown_readed) {
		value = ts->lockdown_info[1];
	} else {
		ret = mi_dsi_panel_lockdown_info_read(ts->lockdown_info);
		if (ret <= 0) {
			NVT_ERR("can't get lockdown info");
			return value;
		}
		value = ts->lockdown_info[1];
		ts->lkdown_readed = true;
	}
	NVT_LOG("%s Get panel display: %d\n", __func__, value);
	return value;
}

static int nvt_ic_self_test(char *type, int *result)
{
	int32_t ret = 0;
	//todo nvt_ic_self_test
	// nvt_selftest_open
	if (!ts) {
		NVT_LOG("ts data is null");
		return -EIO;
	}
	NVT_LOG("type = %s", type);

	if (!strncmp("short", type, 5)) {
		ret = nvt_ic_self_test_short();
			if(ret){
				NVT_LOG("[DIS-TF-TOUCH] ts selfcheck short fail");
			}
	} else if (!strncmp("open", type, 4)) {
		ret = nvt_ic_self_test_open();
			if(ret){
				NVT_LOG("[DIS-TF-TOUCH] ts selfcheck open fail");
			}
	} else if (!strncmp("i2c", type, 3)) {
		ret = nvt_get_fw_info();
	} else {
		ret = 1;
	}

	if (ret == 0) {
		*result = 2;//NVT_RESULT_PASS;
	} else {
		*result = 1;//NVT_RESULT_FAIL;
	}

	return 0;
}

/*
 *  nvt_touch_doze_analysis 用于冻屏
 *
*/
static int nvt_touch_doze_analysis(int input)
{
	int ret = 0;
	int irq_status = -1;
	NVT_LOG("input:%d", input);
	if (!ts){
		NVT_LOG("ts is null!!\n");
		return -EINVAL;
	}
	switch(input) {
		case POWER_RESET:
			flush_workqueue(ts->event_wq);
			ts->doze_test = 1;
			nvt_ts_suspend(&ts->client->dev);
			queue_work(ts->event_wq, &ts->resume_work);
			flush_workqueue(ts->event_wq);
			ts->doze_test = 0;
			NVT_LOG("POWER_RESET success");
			break;
		case RELOAD_FW:
			mutex_lock(&ts->lock);
			ret = nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
			mutex_unlock(&ts->lock);
			if (ret) {
				NVT_ERR("nvt_update_firmware failed. ret = %d\n", ret);
			}
			NVT_LOG("RELOAD_FW success");
			break;
		case ENABLE_IRQ:
			nvt_irq_enable(true);
			NVT_LOG("ENABLE_IRQ success");
			break;
		case DISABLE_IRQ:
			nvt_irq_enable(false);
			NVT_LOG("DISABLE_IRQ success");
			break;
		case REGISTER_IRQ:
			if (ts->client->irq) {
				ret = request_threaded_irq(ts->client->irq, nvt_ts_work_func_irq_handler, nvt_ts_work_func,
						ts->int_trigger_type | IRQF_ONESHOT, "xiaomi_tp", ts);
				if (ret != 0) {
					NVT_ERR("request irq failed. ret=%d\n", ret);
				} else {
					nvt_irq_enable(true);
					NVT_LOG("REGISTER_IRQ %d succeed\n", ts->client->irq);
				}
			}
			break;
		case IRQ_PIN_LEVEL:
			irq_status = gpio_get_value(ts->irq_gpio) == 0 ? 0 : 1;
			NVT_LOG("IRQ_PIN_LEVEL %d succeed\n", irq_status);
			break;
		case ENTER_SUSPEND:
			nvt_ts_suspend(&ts->client->dev);
			NVT_LOG("POWER_RESET success");
			break;
		case ENTER_RESUME:
			flush_workqueue(ts->event_wq);
			queue_work(ts->event_wq, &ts->resume_work);
			NVT_LOG("POWER_RESET success");
			break;
		default:
			NVT_LOG("touch_doze_analysis dont supported,failed with(input:%d)\n", input);
	}
	return irq_status;
}

static int nvt_lockdown_info_read(u8 *lockdown_info_buf)
{
	char value = '0';
	int ret = 0;
	if (!ts)
		return value;

	if (ts->lkdown_readed) {
		lockdown_info_buf[0] = ts->lockdown_info[0];
		lockdown_info_buf[1] = ts->lockdown_info[1];
		lockdown_info_buf[2] = ts->lockdown_info[2];
		lockdown_info_buf[3] = ts->lockdown_info[3];
		lockdown_info_buf[4] = ts->lockdown_info[4];
		lockdown_info_buf[5] = ts->lockdown_info[5];
		lockdown_info_buf[6] = ts->lockdown_info[6];
		lockdown_info_buf[7] = ts->lockdown_info[7];
	} else {
		/* todo */
		ret = mi_dsi_panel_lockdown_info_read(ts->lockdown_info);
		if (ret <= 0) {
			NVT_ERR("can't get lockdown info");
			return value;
		}
		lockdown_info_buf[0] = ts->lockdown_info[0];
		lockdown_info_buf[1] = ts->lockdown_info[1];
		lockdown_info_buf[2] = ts->lockdown_info[2];
		lockdown_info_buf[3] = ts->lockdown_info[3];
		lockdown_info_buf[4] = ts->lockdown_info[4];
		lockdown_info_buf[5] = ts->lockdown_info[5];
		lockdown_info_buf[6] = ts->lockdown_info[6];
		lockdown_info_buf[7] = ts->lockdown_info[7];
		ts->lkdown_readed = true;
	}
	NVT_LOG("end\n");
	return 0;
}

static int nvt_ic_get_fw_version(char *fw_version_buf)
{
	int cnt = 0;
	if (nvt_get_fw_info()) {
		NVT_ERR("nvt_get_fw_info ERROR\n");
		return -EAGAIN;
	}
	cnt = snprintf(&fw_version_buf[cnt], 64 - cnt, "fw_ver:0x%02X  ", ts->fw_ver);
	cnt += snprintf(&fw_version_buf[cnt], 64 - cnt, "fw_type:0x%02X  ", ts->fw_type);
	cnt += snprintf(&fw_version_buf[cnt], 64 - cnt, "nvt_pid:0x%04X\n", ts->nvt_pid);
	NVT_LOG("end\n");

	return 0;
}

/*
static int nvt_fw_version_info_read(char *fw_version_buf)
{
	int cnt = 0;
	cnt = snprintf(&fw_version_buf[cnt], 64 - cnt, "fw_ver:0x%02X  ", ts->fw_ver);
	cnt += snprintf(&fw_version_buf[cnt], 64 - cnt, "fw_type:0x%02X  ", ts->fw_type);
	cnt += snprintf(&fw_version_buf[cnt], 64 - cnt, "nvt_pid:0x%04X\n", ts->nvt_pid);
	NVT_LOG("end\n");

	return 0;
}
*/
#endif /*#if CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE*/

#ifdef CONFIG_TOUCHSCREEN_NVT_DEBUG_FS

void nvt_set_dbgfw_status(bool enable)
{
	ts->fw_debug = enable;
}

static void tpdbg_shutdown(struct nvt_ts_data *ts_core, bool enable)
{
	int ret = 0;
	mutex_lock(&ts->lock);
	if (enable) {
		ret = nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD, 0x1A) ;
		if (ret < 0) {
			NVT_ERR("disable tp sensor failed!");
		}
	} else {
		ret = nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD, 0x15) ;
		if (ret < 0) {
			NVT_ERR("enable tp sensor failed!");
		}
	}
	mutex_unlock(&ts->lock);
}

static void tpdbg_suspend(struct nvt_ts_data *ts_core, bool enable)
{
	if (enable)
		nvt_ts_suspend(&ts_core->client->dev);
	else
		nvt_ts_resume(&ts_core->client->dev);
}

static int tpdbg_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t tpdbg_read(struct file *file, char __user *buf, size_t size,
			  loff_t *ppos)
{
	const char *str = "cmd support as below:\n \
				echo \"irq-disable\" or \"irq-enable\" to ctrl irq\n \
				echo \"tp-sd-en\" or \"tp-sd-off\" to ctrl panel on or off sensor\n \
				echo \"tp-suspend-en\" or \"tp-suspend-off\" to ctrl panel in or off suspend status\n \
				echo \"fw-debug-on\" or \"fw-debug-off\" to on or off fw debug function\n";

	loff_t pos = *ppos;
	int len = strlen(str);

	if (pos < 0)
		return -EINVAL;
	if (pos >= len)
		return 0;

	if (copy_to_user(buf, str, len))
		return -EFAULT;

	*ppos = pos + len;

	return len;
}

static ssize_t tpdbg_write(struct file *file, const char __user *buf,
			   size_t size, loff_t *ppos)
{
	struct nvt_ts_data *ts_core = file->private_data;
	char *cmd = kzalloc(size + 1, GFP_KERNEL);
	int ret = size;

	if (!cmd)
		return -ENOMEM;

	if (copy_from_user(cmd, buf, size)) {
		ret = -EFAULT;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	cancel_delayed_work_sync(&nvt_esd_check_work);
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	cmd[size] = '\0';

	if (!strncmp(cmd, "irq-disable", 11))
		nvt_irq_enable(false);
	else if (!strncmp(cmd, "irq-enable", 10))
		nvt_irq_enable(true);
	else if (!strncmp(cmd, "tp-sd-en", 8))
		tpdbg_shutdown(ts_core, true);
	else if (!strncmp(cmd, "tp-sd-off", 9))
		tpdbg_shutdown(ts_core, false);
	else if (!strncmp(cmd, "tp-suspend-en", 13))
		tpdbg_suspend(ts_core, true);
	else if (!strncmp(cmd, "tp-suspend-off", 14))
		tpdbg_suspend(ts_core, false);
	else if (!strncmp(cmd, "fw-debug-on", 11))
		nvt_set_dbgfw_status(true);
	else if (!strncmp(cmd, "fw-debug-off", 12))
		nvt_set_dbgfw_status(false);
out:
	kfree(cmd);

	return ret;
}

static int tpdbg_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static ssize_t pen_hopping_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *pos)
{
	int retval = -1;
	int8_t cmd[4] = {0};
	uint8_t pen_id = 0x01;
	uint8_t pen_hopping_frequency = 0x03;

	if (copy_from_user(cmd, buf, min(sizeof(cmd), count))) {
		retval = -EFAULT;
		goto out;
	}

	/*pen_id: 0x00, 0x01 0x02, 0x03*/
	if(cmd[0] >= '1' && cmd[0] <= '4') {
		pen_id = cmd[0] - '1';
	}

	/*pen_hopping_frequency: 0x00, 0x01 0x02, 0x03*/
	if(cmd[1] >= '1' && cmd[1] <= '4') {
		pen_hopping_frequency = cmd[1] - '1';
	}

	ts->need_send_hopping_ack = false;
	nvt_pen_hopping_frequency(pen_id, pen_hopping_frequency);
	NVT_LOG("MIPP send pen_id:%x, pen_hopping_frequency:%x", pen_id, pen_hopping_frequency);
	retval = count;

out:
	return retval;
}

static const struct file_operations tpdbg_ops = {
	.owner = THIS_MODULE,
	.open = tpdbg_open,
	.read = tpdbg_read,
	.write = tpdbg_write,
	.release = tpdbg_release,
};

static const struct file_operations pen_hopping_frequency_ops = {
	.owner = THIS_MODULE,
	.write = pen_hopping_write,
};

static void nvt_resume_work(struct work_struct *work)
{
	struct nvt_ts_data *ts_core = container_of(work, struct nvt_ts_data, resume_work);
	nvt_ts_resume(&ts_core->client->dev);
}

static int nvt_pinctrl_init(struct nvt_ts_data *nvt_data)
{
	int retval = 0;
	/* Get pinctrl if target uses pinctrl */
	nvt_data->ts_pinctrl = devm_pinctrl_get(&nvt_data->client->dev);
	NVT_LOG("%s Enter\n", __func__);
	if (IS_ERR_OR_NULL(nvt_data->ts_pinctrl)) {
		retval = PTR_ERR(nvt_data->ts_pinctrl);
		NVT_ERR("Target does not use pinctrl %d\n", retval);
		goto err_pinctrl_get;
	}

	nvt_data->pinctrl_state_active
		= pinctrl_lookup_state(nvt_data->ts_pinctrl, PINCTRL_STATE_ACTIVE);

	if (IS_ERR_OR_NULL(nvt_data->pinctrl_state_active)) {
		retval = PTR_ERR(nvt_data->pinctrl_state_active);
		NVT_ERR("Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_ACTIVE, retval);
		goto err_pinctrl_lookup;
	}

	nvt_data->pinctrl_state_suspend
		= pinctrl_lookup_state(nvt_data->ts_pinctrl, PINCTRL_STATE_SUSPEND);

	if (IS_ERR_OR_NULL(nvt_data->pinctrl_state_suspend)) {
		retval = PTR_ERR(nvt_data->pinctrl_state_suspend);
		NVT_ERR("Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_SUSPEND, retval);
		goto err_pinctrl_lookup;
	}

	return 0;
err_pinctrl_lookup:
	devm_pinctrl_put(nvt_data->ts_pinctrl);
err_pinctrl_get:
	nvt_data->ts_pinctrl = NULL;
	return retval;
}

#endif


/*******************************************************
Description:
	Novatek touchscreen driver probe function.

return:
	Executive outcomes. 0---succeed. negative---failed
*******************************************************/
static int32_t nvt_ts_probe(struct spi_device *client)
{
	int32_t ret = 0;
#if ((TOUCH_KEY_NUM > 0) || WAKEUP_GESTURE)
	int32_t retry = 0;
#endif
	/*set priority to 99*/
	struct sched_param par = { .sched_priority = MAX_RT_PRIO -1 };

	NVT_LOG("start\n");

	/*set spi controller rt thread priority to 99*/
	if(client->controller) {
		ret = sched_setscheduler_nocheck(client->controller->kworker->task, SCHED_FIFO, &par);
		if(ret < 0) {
			NVT_ERR("Failed to improve SPI thread prio\n");
		} else {
			NVT_LOG("spi controller thread PID is %d, set prio to 99", client->controller->kworker->task->pid);
		}
	}

	ts = (struct nvt_ts_data *)kzalloc(sizeof(struct nvt_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		NVT_ERR("failed to allocated memory for nvt ts data\n");
		return -ENOMEM;
	}

	ts->xbuf = (uint8_t *)kzalloc(NVT_XBUF_LEN, GFP_KERNEL);
	if (ts->xbuf == NULL) {
		NVT_ERR("kzalloc for xbuf failed!\n");
		ret = -ENOMEM;
		goto err_malloc_xbuf;
	}

	ts->rbuf = (uint8_t *)kzalloc(NVT_READ_LEN, GFP_KERNEL);
	if(ts->rbuf == NULL) {
		NVT_ERR("kzalloc for rbuf failed!\n");
		ret = -ENOMEM;
		goto err_malloc_rbuf;
	}

	#if TOUCH_THP_SUPPORT
	ts->data_buf = (uint8_t *)kzalloc(DATA_BUF_LEN, GFP_KERNEL);
	if(ts->data_buf == NULL) {
		NVT_ERR("kzalloc for data_buf failed!\n");
		ret = -ENOMEM;
		goto err_malloc_data_buf;
	}

	ts->eventbuf_debug = (uint8_t *)kzalloc(EVENTBUF_DEBUG_LEN , GFP_KERNEL);
	if(ts->eventbuf_debug == NULL) {
		NVT_ERR("kzalloc for eventbuf_debug failed!\n");
		ret = -ENOMEM;
		goto err_malloc_eventbuf_debug;
	}

	ts->frame_len = XM_HTC_DEFAULT_FRAME_LEN;
	#endif

	/* MIPP Start */
#if !TOUCH_THP_SUPPORT
	ts->pen_pdata = NULL;
#endif
	/* MIPP End */
	ts->client = client;
	spi_set_drvdata(client, ts);

	//---prepare for spi parameter---
	if (ts->client->master->flags & SPI_MASTER_HALF_DUPLEX) {
		NVT_ERR("Full duplex not supported by master\n");
		ret = -EIO;
		goto err_ckeck_full_duplex;
	}
	ts->client->bits_per_word = 8;
	ts->client->mode = SPI_MODE_0;

#ifdef SPI_CS_DELAY
	// cs setup time 300ns, cs hold time 300ns
	ts->client->cs_setup.value = 300;
	ts->client->cs_setup.unit = SPI_DELAY_UNIT_NSECS;
	ts->client->cs_hold.value = 300;
	ts->client->cs_hold.unit = SPI_DELAY_UNIT_NSECS;
	NVT_LOG("set cs_setup %d (uint %d) and cs_hold %d (uint %d)\n",
			ts->client->cs_setup.value, ts->client->cs_setup.unit,
			ts->client->cs_hold.value, ts->client->cs_hold.unit);
#endif

	ret = spi_setup(ts->client);
	if (ret < 0) {
		NVT_ERR("Failed to perform SPI setup\n");
		goto err_spi_setup;
	}

#ifdef CONFIG_MTK_SPI
    /* old usage of MTK spi API */
    memcpy(&ts->spi_ctrl, &spi_ctrdata, sizeof(struct mt_chip_conf));
    ts->client->controller_data = (void *)&ts->spi_ctrl;
#endif

#ifdef CONFIG_SPI_MT65XX
    /* new usage of MTK spi API */
    memcpy(&ts->spi_ctrl, &spi_ctrdata, sizeof(struct mtk_chip_config));
    ts->client->controller_data = (void *)&ts->spi_ctrl;
#endif

	NVT_LOG("mode=%d, max_speed_hz=%d\n", ts->client->mode, ts->client->max_speed_hz);

	//---parse dts---
	ret = nvt_parse_dt(&client->dev);
	if (ret) {
		NVT_ERR("[DIS-TF-TOUCH] parse dt error\n");
		goto err_spi_setup;
	}
	/* SPI Pinctrl setup */
	ret = nvt_pinctrl_init(ts);
	if (!ret && ts->ts_pinctrl) {
		ret = pinctrl_select_state(ts->ts_pinctrl, ts->pinctrl_state_active);
		if (ret < 0) {
			NVT_ERR("Failed to select %s pinstate %d\n",
				PINCTRL_STATE_ACTIVE, ret);
		}
	} else {
		NVT_ERR("Failed to init pinctrl\n");
	}
	/* SPI Pinctrl setup end */

	//---request and config GPIOs---
	ret = nvt_gpio_config(ts);
	if (ret) {
		NVT_ERR("[DIS-TF-TOUCH] gpio config error!\n");
		goto err_gpio_config_failed;
	}

	mutex_init(&ts->lock);
	mutex_init(&ts->xbuf_lock);

	//---eng reset before TP_RESX high
	nvt_eng_reset();

#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_set_value(ts->reset_gpio, 1);
#endif

	// need 10ms delay after POR(power on reset)
	msleep(10);

	//---check chip version trim---
	ret = nvt_ts_check_chip_ver_trim_loop();
	if (ret) {
		NVT_ERR("[DIS-TF-TOUCH] chip is not identified\n");
		ret = -EINVAL;
		goto err_chipvertrim_failed;
	}

	//---allocate input device---
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		NVT_ERR("allocate input device failed\n");
		ret = -ENOMEM;
		goto err_input_dev_alloc_failed;
	}

	ts->max_touch_num = TOUCH_MAX_FINGER_NUM;

#if TOUCH_KEY_NUM > 0
	ts->max_button_num = TOUCH_KEY_NUM;
#endif

	ts->int_trigger_type = INT_TRIGGER_TYPE;

	//thp start 6.8
	ts->ts_probe_start = true;
	//thp end 6.8

	//---set input device info.---
	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	ts->input_dev->keybit[BIT_WORD(BTN_TOOL_FINGER)] |= BIT_MASK(BTN_TOOL_FINGER);
	ts->input_dev->propbit[0] = BIT(INPUT_PROP_DIRECT);

#if MT_PROTOCOL_B
	input_mt_init_slots(ts->input_dev, ts->max_touch_num, 0);
#endif

	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, TOUCH_FORCE_NUM, 0, 0);    //pressure = TOUCH_FORCE_NUM

#if TOUCH_MAX_FINGER_NUM > 1

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, TOUCH_MAJOR_MAX_VALUE, 0, 0);
#else
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);    //area = 255
#endif // CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE

	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, TOUCH_MAX_WIDTH*SUPER_RESOLUTION_FACOTR - 1, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, TOUCH_MAX_HEIGHT*SUPER_RESOLUTION_FACOTR - 1, 0, 0);
#if MT_PROTOCOL_B
	// no need to set ABS_MT_TRACKING_ID, input_mt_init_slots() already set it
#else
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, ts->max_touch_num, 0, 0);
#endif //MT_PROTOCOL_B
#endif //TOUCH_MAX_FINGER_NUM > 1

#if TOUCH_KEY_NUM > 0
	for (retry = 0; retry < ts->max_button_num; retry++) {
		input_set_capability(ts->input_dev, EV_KEY, touch_key_array[retry]);
	}
#endif

#if WAKEUP_GESTURE
	for (retry = 0; retry < (sizeof(gesture_key_array) / sizeof(gesture_key_array[0])); retry++) {
		input_set_capability(ts->input_dev, EV_KEY, gesture_key_array[retry]);
	}
#endif

	sprintf(ts->phys, "input/ts");
	ts->input_dev->name = NVT_TS_NAME;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_SPI;

	//---register input device---
	ret = input_register_device(ts->input_dev);
	if (ret) {
		NVT_ERR("register input device (%s) failed. ret=%d\n", ts->input_dev->name, ret);
		goto err_input_register_device_failed;
	}

	/* MIPP Start */
#if !TOUCH_THP_SUPPORT
	ret = nvt_pen_device_register(ts);
	if(ret < 0) {
		NVT_ERR("register pen device failed. ret=%d\n", ret);
		goto err_pen_register_device_failed;
	}
#endif
	/* MIPP End */

	if (ts->pen_support) {
		//---allocate pen input device--- m80p
		ts->pen_input_dev_m80p = input_allocate_device();
		if (ts->pen_input_dev_m80p == NULL) {
			NVT_ERR("allocate pen input device failed\n");
			ret = -ENOMEM;
			goto err_pen_input_dev_alloc_failed;
		}

		//---set pen input device info.---
		ts->pen_input_dev_m80p->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		ts->pen_input_dev_m80p->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
		ts->pen_input_dev_m80p->keybit[BIT_WORD(BTN_TOOL_PEN)] |= BIT_MASK(BTN_TOOL_PEN);
		//ts->pen_input_dev_m80p->keybit[BIT_WORD(BTN_TOOL_RUBBER)] |= BIT_MASK(BTN_TOOL_RUBBER);
		ts->pen_input_dev_m80p->propbit[0] = BIT(INPUT_PROP_DIRECT);

		input_set_abs_params(ts->pen_input_dev_m80p, ABS_X, 0, PEN_MAX_WIDTH*SUPER_RESOLUTION_FACOTR - 1, 0, 0);
		input_set_abs_params(ts->pen_input_dev_m80p, ABS_Y, 0, PEN_MAX_HEIGHT*SUPER_RESOLUTION_FACOTR - 1, 0, 0);
		input_set_abs_params(ts->pen_input_dev_m80p, ABS_PRESSURE, 0, PEN_M80P_PRESSURE_MAX, 0, 0);
		input_set_abs_params(ts->pen_input_dev_m80p, ABS_DISTANCE, 0, PEN_DISTANCE_MAX, 0, 0);
		input_set_abs_params(ts->pen_input_dev_m80p, ABS_TILT_X, PEN_TILT_MIN, PEN_TILT_MAX, 0, 0);
		input_set_abs_params(ts->pen_input_dev_m80p, ABS_TILT_Y, PEN_TILT_MIN, PEN_TILT_MAX, 0, 0);

#if WAKEUP_GESTURE
		input_set_capability(ts->pen_input_dev_m80p, EV_KEY, KEY_WAKEUP);
#endif

		sprintf(ts->pen_phys, "input/pen");
		ts->pen_input_dev_m80p->name = NVT_M80P_PEN_NAME;
		ts->pen_input_dev_m80p->phys = ts->pen_phys;
		ts->pen_input_dev_m80p->id.bustype = BUS_SPI;
		//---improve evdev buffer size to 1024---
		ts->pen_input_dev_m80p->hint_events_per_packet = 128;

		//---register pen input device---
		ret = input_register_device(ts->pen_input_dev_m80p);
		if (ret) {
			NVT_ERR("register pen input device (%s) failed. ret=%d\n", ts->pen_input_dev_m80p->name, ret);
			goto err_pen_input_register_device_failed;
		}

		//---allocate pen input device--- p81c
		ts->pen_input_dev_p81c = input_allocate_device();
		if (ts->pen_input_dev_p81c == NULL) {
			NVT_ERR("allocate pen input device failed\n");
			ret = -ENOMEM;
			goto err_pen_input_dev_alloc_failed;
		}

		//---set pen input device info.---
		ts->pen_input_dev_p81c->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		ts->pen_input_dev_p81c->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
		ts->pen_input_dev_p81c->keybit[BIT_WORD(BTN_TOOL_PEN)] |= BIT_MASK(BTN_TOOL_PEN);
		ts->pen_input_dev_p81c->keybit[BIT_WORD(BTN_JOYSTICK)] |= BIT_MASK(BTN_JOYSTICK);
		//ts->pen_input_dev_p81c->keybit[BIT_WORD(BTN_TOOL_RUBBER)] |= BIT_MASK(BTN_TOOL_RUBBER);
		ts->pen_input_dev_p81c->propbit[0] = BIT(INPUT_PROP_DIRECT);

		input_set_abs_params(ts->pen_input_dev_p81c, ABS_X, 0, PEN_MAX_WIDTH*SUPER_RESOLUTION_FACOTR - 1, 0, 0);
		input_set_abs_params(ts->pen_input_dev_p81c, ABS_Y, 0, PEN_MAX_HEIGHT*SUPER_RESOLUTION_FACOTR - 1, 0, 0);
		input_set_abs_params(ts->pen_input_dev_p81c, ABS_PRESSURE, 0, PEN_P81C_PRESSURE_MAX, 0, 0);
		input_set_abs_params(ts->pen_input_dev_p81c, ABS_DISTANCE, 0, PEN_DISTANCE_MAX, 0, 0);
		input_set_abs_params(ts->pen_input_dev_p81c, ABS_TILT_X, PEN_TILT_MIN, PEN_TILT_MAX, 0, 0);
		input_set_abs_params(ts->pen_input_dev_p81c, ABS_TILT_Y, PEN_TILT_MIN, PEN_TILT_MAX, 0, 0);
		input_set_abs_params(ts->pen_input_dev_p81c, ABS_BRAKE, 0, 360, 0, 0);

#if WAKEUP_GESTURE
		input_set_capability(ts->pen_input_dev_p81c, EV_KEY, KEY_WAKEUP);
#endif

		sprintf(ts->pen_p81c_phys, "input/pen_p81c");
		ts->pen_input_dev_p81c->name = NVT_P81C_PEN_NAME;
		ts->pen_input_dev_p81c->phys = ts->pen_p81c_phys;
		ts->pen_input_dev_p81c->id.bustype = BUS_SPI;
		//---improve evdev buffer size to 1024---
		ts->pen_input_dev_p81c->hint_events_per_packet = 128;

		//---register pen input device---
		ret = input_register_device(ts->pen_input_dev_p81c);
		if (ret) {
			NVT_ERR("register pen input device (%s) failed. ret=%d\n", ts->pen_input_dev_p81c->name, ret);
			goto err_pen_input_register_device_failed;
		}


	} /* if (ts->pen_support) */

	//---set int-pin & request irq---
	client->irq = gpio_to_irq(ts->irq_gpio);
	if (client->irq) {
		NVT_LOG("int_trigger_type=%d\n", ts->int_trigger_type);
		ts->irq_enabled = true;
		ret = request_threaded_irq(client->irq, nvt_ts_work_func_irq_handler, nvt_ts_work_func,
				ts->int_trigger_type | IRQF_ONESHOT, "xiaomi_tp", ts);
		if (ret != 0) {
			NVT_ERR("request irq failed. ret=%d\n", ret);
			goto err_int_request_failed;
		} else {
			nvt_irq_enable(false);
			NVT_LOG("request irq %d succeed\n", client->irq);
		}
	}

#if WAKEUP_GESTURE
	device_init_wakeup(&ts->client->dev, 1);
#endif

	ts->nvt_tool_in_use = false;
	ts->limit_version = 0;
#ifdef TOUCH_THP_SUPPORT
	ts->xm_htc_sw_reset = true;
#endif
#if BOOT_UPDATE_FIRMWARE
	nvt_fwu_wq = alloc_workqueue("nvt_fwu_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!nvt_fwu_wq) {
		NVT_ERR("nvt_fwu_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_nvt_fwu_wq_failed;
	}
	INIT_DELAYED_WORK(&ts->nvt_fwu_work, Boot_Update_Firmware);
	// please make sure boot update start after display reset(RESX) sequence
	queue_delayed_work(nvt_fwu_wq, &ts->nvt_fwu_work, msecs_to_jiffies(3000));
#endif

	NVT_LOG("NVT_TOUCH_ESD_PROTECT is %d\n", NVT_TOUCH_ESD_PROTECT);
#if NVT_TOUCH_ESD_PROTECT
	INIT_DELAYED_WORK(&nvt_esd_check_work, nvt_esd_check_func);
	nvt_esd_check_wq = alloc_workqueue("nvt_esd_check_wq", WQ_MEM_RECLAIM, 1);
	if (!nvt_esd_check_wq) {
		NVT_ERR("nvt_esd_check_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_nvt_esd_check_wq_failed;
	}
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	//---set device node---
#if NVT_TOUCH_PROC
	ret = nvt_flash_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt flash proc init failed. ret=%d\n", ret);
		goto err_flash_proc_init_failed;
	}
#endif

#if NVT_TOUCH_EXT_PROC
	ret = nvt_extra_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt extra proc init failed. ret=%d\n", ret);
		goto err_extra_proc_init_failed;
	}
#endif

#if NVT_TOUCH_MP
	ret = nvt_mp_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt mp proc init failed. ret=%d\n", ret);
		goto err_mp_proc_init_failed;
	}
#endif
/* todo register display notifer */
#if defined(_MSM_DRM_NOTIFY_H_)
	ts->drm_notif.notifier_call = nvt_drm_notifier_callback;
	ret = msm_drm_register_client(&ts->drm_notif);
	if(ret) {
		NVT_ERR("register drm_notifier failed. ret=%d\n", ret);
		goto err_register_drm_notif_failed;
	}
#elif defined(CONFIG_FB)
	ts->fb_notif.notifier_call = nvt_fb_notifier_callback;
	ret = fb_register_client(&ts->fb_notif);
	if(ret) {
		NVT_ERR("register fb_notifier failed. ret=%d\n", ret);
		goto err_register_fb_notif_failed;
	}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = nvt_ts_early_suspend;
	ts->early_suspend.resume = nvt_ts_late_resume;
	ret = register_early_suspend(&ts->early_suspend);
	if(ret) {
		NVT_ERR("register early suspend failed. ret=%d\n", ret);
		goto err_register_early_suspend_failed;
	}
#endif

#ifdef CONFIG_TOUCHSCREEN_NVT_DEBUG_FS
	ts->fw_debug = false;
#endif

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
	/* lockdown info read */
	ts->lkdown_readed = false;
	ts->xm_htc_polled = false;
	ts->ts_selftest_process = false;
	ts->ts_selftest_process_cmd = false;

	/* event_wq setup */
	ts->event_wq = alloc_workqueue("nvt-event-queue",
		WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!ts->event_wq) {
		NVT_ERR("Can not create work thread for suspend/resume!!");
		ret = -ENOMEM;
		goto err_alloc_work_thread_failed;
	}
	/* event_wq setup end */

	/* power supply */
	ts->is_usb_exist = -1;
	INIT_WORK(&ts->power_supply_work, nvt_power_supply_work);
	/* power supply end*/
	//thp start 6.8
	#if TOUCH_THP_SUPPORT
	ts->xm_htc_report_coordinate = false;
	ts->enable_touch_raw = true;
	#endif
	//thp end 6.8

	/* gesture mode setup */
	ts->gesture_command = 0;
	ts->ic_state = NVT_IC_INIT;
	ts->dev_pm_suspend = false;
	init_completion(&ts->dev_pm_suspend_completion);
	ts->gesture_command_delayed = -1;
	/* gesture mode setup end */
	/* pen_connect_strategy setup */
	ts->pen_bluetooth_connect = 0;
	ts->pen_count = 0;
	ts->pen_shield_flag = 0;
	ts->gamemode_enable = 0;
	ts->cur_pen = PEN_P81C;
	ts->pen_switch = SUPPORT_P81C;
	ts->game_in_whitelist = 0;
	ts->game_in_whitelist_bak = 0;
	mutex_init(&ts->pen_switch_lock);
	INIT_WORK(&ts->pen_charge_state_change_work, nvt_pen_charge_state_change_work);	//use system_wq (schedule_work)
	ts->pen_charge_connect = false;
	ts->pen_charge_state_notifier.notifier_call = nvt_pen_charge_state_notifier_callback;
	ret = pen_charge_state_notifier_register_client(&ts->pen_charge_state_notifier);
	if(ret) {
		NVT_ERR("register pen_connect_status change notifier failed. ret=%d\n", ret);
		goto err_register_pen_charge_state_failed;
	}
	/* pen_connect_strategy end */

	/* resume use work queue setup */
	INIT_WORK(&ts->resume_work, nvt_resume_work);
	/* resume use work queue setup end */

	//nvt hardware_param
	memset(hardware_param.config_file_name, 0, 64);
	if (!ts->lcd_id_value) {
		memcpy(hardware_param.config_file_name, BOOT_UPDATE_CONFIG_NAME_CSOT, strlen(BOOT_UPDATE_CONFIG_NAME_CSOT));
	} else {
		memcpy(hardware_param.config_file_name, BOOT_UPDATE_CONFIG_NAME_BOE, strlen(BOOT_UPDATE_CONFIG_NAME_BOE));
	}

	memset(hardware_param.driver_version, 0, 64);
	memcpy(hardware_param.driver_version, NVT36532_DRIVER_VERSION, strlen(NVT36532_DRIVER_VERSION));
	nvt_lockdown_info_read(hardware_param.lockdown_info);
	// nvt_fw_version_info_read(hardware_param.fw_version);
	hardware_param.x_resolution = TOUCH_MAX_WIDTH;
	hardware_param.y_resolution = TOUCH_MAX_HEIGHT;
	hardware_param.super_resolution_factor = SUPER_RESOLUTION_FACOTR;
	hardware_param.rx_num = TOUCH_RX_NUM;
	hardware_param.tx_num = TOUCH_TX_NUM;
	hardware_param.frame_data_page_size = 2;
	hardware_param.frame_data_buf_size = 10;
	hardware_param.raw_data_page_size = 8;
	hardware_param.raw_data_buf_size = 5;

	NVT_LOG("hardware_param.config_file_name=%s\n", hardware_param.config_file_name);
	NVT_LOG("hardware_param.driver_version=%s\n", hardware_param.driver_version);
	NVT_LOG("hardware_param.fw_version=%s\n", hardware_param.fw_version);
	NVT_LOG("hardware_param.x_resolution=%d\n", hardware_param.x_resolution);
	NVT_LOG("hardware_param.y_resolution=%d\n", hardware_param.y_resolution);
	NVT_LOG("hardware_param.rx_num=%d, ts->x_num=%d\n", hardware_param.rx_num, ts->x_num);
	NVT_LOG("hardware_param.tx_num=%d, ts->y_num=%d\n", hardware_param.tx_num, ts->y_num);
	NVT_LOG("hardware_param.super_resolution_factor=%d\n", hardware_param.super_resolution_factor);
	//nvt  hardware_operation
	memset(&hardware_operation, 0, sizeof(hardware_operation_t));
	hardware_operation.ic_resume_suspend = nvt_ts_resume_suspend;
	hardware_operation.ic_self_test = nvt_ic_self_test;
	hardware_operation.ic_data_collect = NULL;
	hardware_operation.ic_get_lockdown_info = nvt_lockdown_info_read;
	hardware_operation.ic_set_charge_state = nvt_set_charge_state;
	hardware_operation.ic_get_fw_version = nvt_ic_get_fw_version;
	hardware_operation.ic_switch_mode = nvt_ic_switch_mode;
	hardware_operation.set_mode_value = nvt_set_cur_value;
	hardware_operation.set_mode_long_value = NULL;
	hardware_operation.palm_sensor_write = NULL;
	hardware_operation.cmd_update_func = nvt_cmd_mode_update;
	hardware_operation.panel_vendor_read = nvt_panel_vendor_read;
	hardware_operation.panel_color_read = nvt_panel_color_read;
	hardware_operation.panel_display_read = nvt_panel_display_read;
	hardware_operation.touch_vendor_read = nvt_touch_vendor_read;
	hardware_operation.get_touch_ic_buffer = NULL;
	hardware_operation.touch_doze_analysis = nvt_touch_doze_analysis;
	#if TOUCH_THP_SUPPORT
	hardware_operation.enable_touch_raw = nvt_enable_touch_raw;
	hardware_operation.htc_ic_setModeValue = nvt_htc_ic_setModeValue;
	hardware_operation.htc_ic_getModeValue = nvt_htc_ic_getModeValue;
	#endif /*TOUCH_THP_SUPPORT*/
	nvt_init_touchmode_data();
	register_touch_panel(&client->dev, TOUCH_ID, &hardware_param, &hardware_operation);
	xiaomi_register_panel_notifier(&client->dev, TOUCH_ID,
		PANEL_EVENT_NOTIFICATION_PRIMARY, PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH);
#endif /* #if CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */

#ifdef CONFIG_TOUCHSCREEN_NVT_DEBUG_FS
	ts->debugfs = debugfs_create_dir("tp_debug", NULL);
	if (ts->debugfs) {
		debugfs_create_file("switch_state", 0660, ts->debugfs, ts, &tpdbg_ops);
		debugfs_create_file("pen_hopping_frequency", 0660, ts->debugfs, ts, &pen_hopping_frequency_ops);
	}
#endif

	bTouchIsAwake = 1;

	nvt_irq_enable(true);

	ret = mi_dsi_panel_lockdown_info_read(ts->lockdown_info);
	if (ret < 0) {
		NVT_ERR("can't get lockdown info");
	} else {
		NVT_LOG("Lockdown:0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x\n",
		ts->lockdown_info[0], ts->lockdown_info[1], ts->lockdown_info[2], ts->lockdown_info[3],
		ts->lockdown_info[4], ts->lockdown_info[5], ts->lockdown_info[6], ts->lockdown_info[7]);
		ts->lkdown_readed = true;
		NVT_LOG("READ LOCKDOWN!!!");
	}
	// send edge filter params
	add_common_data_to_buf(0, SET_LONG_VALUE, DATA_MODE_15, 96, (int *)edge_filter_params);

	NVT_LOG("end\n");

	return 0;

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
err_register_pen_charge_state_failed:
	if (pen_charge_state_notifier_unregister_client(&ts->pen_charge_state_notifier)) {
		NVT_ERR("Error occurred while unregistering pen status switch state notifier.\n");
	}
err_alloc_work_thread_failed:
	if(ts->event_wq){
		cancel_work_sync(&ts->power_supply_work);
		destroy_workqueue(ts->event_wq);
		ts->event_wq = NULL;
	}
	mutex_destroy(&ts->pen_switch_lock);
#endif /* #if CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */

#if defined(_MSM_DRM_NOTIFY_H_)
	if (msm_drm_unregister_client(&ts->drm_notif))
		NVT_ERR("Error occurred while unregistering drm_notifier.\n");
err_register_drm_notif_failed:
#elif defined(CONFIG_FB)
	if (fb_unregister_client(&ts->fb_notif))
		NVT_ERR("Error occurred while unregistering fb_notifier.\n");
err_register_fb_notif_failed:
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
err_register_early_suspend_failed:
#endif
#if NVT_TOUCH_MP
	nvt_mp_proc_deinit();
err_mp_proc_init_failed:
#endif
#if NVT_TOUCH_EXT_PROC
	nvt_extra_proc_deinit();
err_extra_proc_init_failed:
#endif
#if NVT_TOUCH_PROC
	nvt_flash_proc_deinit();
err_flash_proc_init_failed:
#endif
#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
err_create_nvt_esd_check_wq_failed:
#endif
#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
err_create_nvt_fwu_wq_failed:
#endif
#if WAKEUP_GESTURE
	device_init_wakeup(&ts->client->dev, 0);
#endif
	free_irq(client->irq, ts);
err_int_request_failed:
	if (ts->pen_support) {
		if (ts->pen_input_dev_m80p) {
			input_unregister_device(ts->pen_input_dev_m80p);
			ts->pen_input_dev_m80p = NULL;
		}
		if (ts->pen_input_dev_p81c) {
			input_unregister_device(ts->pen_input_dev_p81c);
			ts->pen_input_dev_p81c = NULL;
		}
	}
err_pen_input_register_device_failed:
	if (ts->pen_support) {
		if (ts->pen_input_dev_m80p) {
			input_free_device(ts->pen_input_dev_m80p);
			ts->pen_input_dev_m80p = NULL;
		}
		if (ts->pen_input_dev_p81c) {
			input_free_device(ts->pen_input_dev_p81c);
			ts->pen_input_dev_p81c = NULL;
		}
	}
err_pen_input_dev_alloc_failed:
#if !TOUCH_THP_SUPPORT
err_pen_register_device_failed:
#endif
	input_unregister_device(ts->input_dev);
	ts->input_dev = NULL;
err_input_register_device_failed:
	if (ts->input_dev) {
		input_free_device(ts->input_dev);
		ts->input_dev = NULL;
	}
err_input_dev_alloc_failed:
err_chipvertrim_failed:
	mutex_destroy(&ts->xbuf_lock);
	mutex_destroy(&ts->lock);
	nvt_gpio_deconfig(ts);
err_gpio_config_failed:
err_spi_setup:
err_ckeck_full_duplex:
	spi_set_drvdata(client, NULL);
//thp start 6.8
#if TOUCH_THP_SUPPORT
	if (ts->eventbuf_debug) {
		kfree(ts->eventbuf_debug);
		ts->eventbuf_debug = NULL;
	}
err_malloc_eventbuf_debug:
	if (ts->data_buf) {
		kfree(ts->data_buf);
		ts->data_buf = NULL;
	}
err_malloc_data_buf:
#endif /* #if TOUCH_THP_SUPPORT */
//thp end 6.8
	if (ts->rbuf) {
		kfree(ts->rbuf);
		ts->rbuf = NULL;
	}
err_malloc_rbuf:
	if (ts->xbuf) {
		kfree(ts->xbuf);
		ts->xbuf = NULL;
	}
err_malloc_xbuf:
	if (ts) {
		kfree(ts);
		ts = NULL;
	}
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen driver release function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static void nvt_ts_remove(struct spi_device *client)
{
	NVT_LOG("Removing driver...\n");

#if defined(_MSM_DRM_NOTIFY_H_)
	if (msm_drm_unregister_client(&ts->drm_notif))
		NVT_ERR("Error occurred while unregistering drm_notifier.\n");
#elif defined(CONFIG_FB)
	if (fb_unregister_client(&ts->fb_notif))
		NVT_ERR("Error occurred while unregistering fb_notifier.\n");
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#endif

#if NVT_TOUCH_MP
	nvt_mp_proc_deinit();
#endif
#if NVT_TOUCH_EXT_PROC
	nvt_extra_proc_deinit();
#endif
#if NVT_TOUCH_PROC
	nvt_flash_proc_deinit();
#endif

#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		nvt_esd_check_enable(false);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
#endif

#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
#endif

#if WAKEUP_GESTURE
	device_init_wakeup(&ts->client->dev, 0);
#endif

	nvt_irq_enable(false);
	free_irq(client->irq, ts);

	mutex_destroy(&ts->xbuf_lock);
	mutex_destroy(&ts->lock);

	nvt_gpio_deconfig(ts);

	if (ts->pen_support) {
		if (ts->pen_input_dev_m80p) {
			input_unregister_device(ts->pen_input_dev_m80p);
			ts->pen_input_dev_m80p = NULL;
		}
		if (ts->pen_input_dev_p81c) {
			input_unregister_device(ts->pen_input_dev_p81c);
			ts->pen_input_dev_p81c = NULL;
		}
	}

	if (ts->input_dev) {
		input_unregister_device(ts->input_dev);
		ts->input_dev = NULL;
	}

	spi_set_drvdata(client, NULL);

	if (ts->xbuf) {
		kfree(ts->xbuf);
		ts->xbuf = NULL;
	}

	/* MIPP Start */
#if !TOUCH_THP_SUPPORT
	if(ts->pen_pdata) {
		nvt_pen_device_release(ts);
		ts->pen_pdata = NULL;
	}
#endif
	/* MIPP End */

	if (ts) {
		kfree(ts);
		ts = NULL;
	}

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
	/* todo */
	if (pen_charge_state_notifier_unregister_client(&ts->pen_charge_state_notifier)) {
		NVT_ERR("Error occurred while unregistering pen status switch state notifier.\n");
	}
	// if (ts->set_touchfeature_wq)
	// 	destroy_workqueue(ts->set_touchfeature_wq);
	if(ts->event_wq){
		cancel_work_sync(&ts->power_supply_work);
		destroy_workqueue(ts->event_wq);
		ts->event_wq = NULL;
	}

#endif /* #ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */

//thp start 6.8
#if TOUCH_THP_SUPPORT
	if (ts->data_buf) {
		kfree(ts->data_buf);
		ts->data_buf = NULL;
	}
#endif /* #if TOUCH_THP_SUPPORT */
//thp end 6.8
}

static void suspend_mode_proc(enum suspend_state state)
{
	int suspend_status = state;
	add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_27, 1, &suspend_status);
}

static int32_t nvt_ts_resume_suspend(bool is_resume, u8 gesture_type)
{
	int result = 0;

	result = is_resume ? nvt_ts_resume(&ts->client->dev) : nvt_ts_suspend(&ts->client->dev);
	return result;
}

/*******************************************************
Description:
	Novatek touchscreen driver suspend function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_ts_suspend(struct device *dev)
{
	ts->nvt_tool_in_use = false;

	if (!bTouchIsAwake) {
		NVT_LOG("Touch is already suspend\n");
		return 0;
	}

	NVT_LOG("start, gesture_command:0x%02x\n", ts->gesture_command);
	pm_stay_awake(dev);
	ts->ic_state = NVT_IC_SUSPEND_IN;

	/* gesture mode setup */
	if (!ts->gesture_command)
			nvt_irq_enable(false);
	/* gesture mode setup end */

#if NVT_TOUCH_ESD_PROTECT
	NVT_LOG("cancel delayed work sync\n");
	cancel_delayed_work_sync(&nvt_esd_check_work);
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	mutex_lock(&ts->lock);

	/* gesture mode setup */
	if (ts->gesture_command) {
		nvt_enable_gesture_mode(true);
	} else {
		nvt_enable_gesture_mode(false);
	}
	/* gesture mode setup end */

	msleep(50);

	mutex_unlock(&ts->lock);

#if TOUCH_THP_SUPPORT
	if (ts->enable_touch_raw)
		suspend_mode_proc(XIAOMI_DATA_MODE_27);
#endif
	ts->gamemode_enable = 0;  /*suspend, gamemode to false, resume wait thp update*/

	if (ts->pen_bluetooth_connect) {
		ts->need_send_hopping_ack = false;
		nvt_pen_hopping_frequency(0, 0);
		NVT_LOG("MIPP reset stylus to main freq");
	}

	bTouchIsAwake = 0;

	if (likely(ts->ic_state == NVT_IC_SUSPEND_IN)) {
		ts->ic_state = NVT_IC_SUSPEND_OUT;
	} else {
		NVT_ERR("IC state may error,caused by suspend/resume flow, please CHECK!!");
	}

	pm_relax(dev);

	NVT_LOG("end\n");

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen driver resume function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_ts_resume(struct device *dev)
{
	int ret = 0;
	bool pm_wake_status = 0;

	ts->nvt_tool_in_use = false;

	if (bTouchIsAwake) {
		NVT_LOG("Touch is already resume\n");
		return 0;
	}

	NVT_LOG("start\n");
	if (ts->dev_pm_suspend) {
		pm_stay_awake(dev);
		pm_wake_status = true;
	}
	ts->ic_state = NVT_IC_RESUME_IN;

	mutex_lock(&ts->lock);

	/* SPI Pinctrl setup */
	if (!ts->gesture_command) {
		if (ts->ts_pinctrl) {
			ret = pinctrl_select_state(ts->ts_pinctrl, ts->pinctrl_state_active);
			if (ret < 0) {
				NVT_ERR("Failed to select %s pinstate %d\n",
					PINCTRL_STATE_ACTIVE, ret);
			}
		} else {
			NVT_ERR("Failed to init pinctrl\n");
		}
	}
	/* SPI Pinctrl setup end */

	// please make sure display reset(RESX) sequence and mipi dsi cmds sent before this
#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_set_value(ts->reset_gpio, 1);
#endif
	if (nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME)) {
		NVT_ERR("download firmware failed, ignore check fw state\n");
	} else {
		nvt_check_fw_reset_state(RESET_STATE_REK);
	}

	/* gesture mode setup */
	if (!ts->gesture_command) {
		nvt_irq_enable(true);
	}
	/* gesture mode setup end */

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	bTouchIsAwake = 1;

	mutex_unlock(&ts->lock);

#if TOUCH_THP_SUPPORT
	if (ts->enable_touch_raw)
		suspend_mode_proc(XIAOMI_TOUCH_RESUME);
#endif

	if (likely(ts->ic_state == NVT_IC_RESUME_IN)) {
		ts->ic_state = NVT_IC_RESUME_OUT;
	} else {
		NVT_ERR("IC state may error,caused by suspend/resume flow, please CHECK!!");
	}

#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE
	/* resume should update_pen_status again */
	if (ts->pen_support) {
		mutex_lock(&ts->lock);
		update_pen_status(true);
		mutex_unlock(&ts->lock);
	}
	//add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_143, 1, &ts->pen_static_status);

	queue_work(ts->event_wq, &ts->power_supply_work);

	if (ts->gesture_command_delayed >= 0) {
		nvt_set_gesture_mode(ts->gesture_command_delayed);
		ts->gesture_command_delayed = -1;
	}
#endif /* #ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE */

	if (pm_wake_status)
		pm_relax(dev);

	NVT_LOG("end\n");

	return 0;
}

#if defined(_MSM_DRM_NOTIFY_H_)
static int nvt_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct msm_drm_notifier *evdata = data;
	int *blank;
	struct nvt_ts_data *ts =
		container_of(self, struct nvt_ts_data, drm_notif);

	if (!evdata || (evdata->id != 0))
		return 0;

	if (evdata->data && ts) {
		blank = evdata->data;
		if (event == MSM_DRM_EARLY_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_POWERDOWN) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				nvt_ts_suspend(&ts->client->dev);
			}
		} else if (event == MSM_DRM_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_UNBLANK) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				nvt_ts_resume(&ts->client->dev);
			}
		}
	}

	return 0;
}
#elif defined(CONFIG_FB)
static int nvt_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct nvt_ts_data *ts =
		container_of(self, struct nvt_ts_data, fb_notif);

	if (evdata && evdata->data && event == FB_EARLY_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_POWERDOWN) {
			NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
			nvt_ts_suspend(&ts->client->dev);
		}
	} else if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
			NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
			nvt_ts_resume(&ts->client->dev);
		}
	}

	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
/*******************************************************
Description:
	Novatek touchscreen driver early suspend function.

return:
	n.a.
*******************************************************/
static void nvt_ts_early_suspend(struct early_suspend *h)
{
	nvt_ts_suspend(ts->client, PMSG_SUSPEND);
}

/*******************************************************
Description:
	Novatek touchscreen driver late resume function.

return:
	n.a.
*******************************************************/
static void nvt_ts_late_resume(struct early_suspend *h)
{
	nvt_ts_resume(ts->client);
}
#endif

static const struct spi_device_id nvt_ts_id[] = {
	{ NVT_SPI_NAME, 0 },
	{ }
};

#ifdef CONFIG_OF
static struct of_device_id nvt_match_table[] = {
	{ .compatible = "novatek,NVT-ts-spi",},
	{ },
};
#endif

#ifdef CONFIG_PM
static int nvt_pm_suspend(struct device *dev)
{
	NVT_LOG("++\n");

	if (device_may_wakeup(dev) && ts->gesture_command) {
		NVT_LOG("enable touch irq wake\n");
		enable_irq_wake(ts->client->irq);
	}
	ts->dev_pm_suspend = true;
	reinit_completion(&ts->dev_pm_suspend_completion);

	NVT_LOG("--\n");

	return 0;
}

static int nvt_pm_resume(struct device *dev)
{
	NVT_LOG("++\n");

	if (device_may_wakeup(dev) && ts->gesture_command) {
		NVT_LOG("disable touch irq wake\n");
		disable_irq_wake(ts->client->irq);
	}
	ts->dev_pm_suspend = false;
	complete(&ts->dev_pm_suspend_completion);

	NVT_LOG("--\n");

	return 0;
}

static const struct dev_pm_ops nvt_dev_pm_ops = {
	.suspend = nvt_pm_suspend,
	.resume = nvt_pm_resume,
};
#endif

static struct spi_driver nvt_spi_driver = {
	.probe		= nvt_ts_probe,
	.remove		= nvt_ts_remove,
	.id_table	= nvt_ts_id,
	.driver = {
		.name	= NVT_SPI_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = nvt_match_table,
#endif
#ifdef CONFIG_PM
		.pm = &nvt_dev_pm_ops,
#endif
	},
};

/*******************************************************
Description:
	Driver Install function.

return:
	Executive Outcomes. 0---succeed. not 0---failed.
********************************************************/
static int32_t __init nvt_driver_init(void)
{
	int32_t ret = 0;

	NVT_LOG("start\n");

	//---add spi driver---
	ret = spi_register_driver(&nvt_spi_driver);
	if (ret) {
		NVT_ERR("failed to add spi driver");
		goto err_driver;
	}

	NVT_LOG("finished\n");

err_driver:
	return ret;
}

/*******************************************************
Description:
	Driver uninstall function.

return:
	n.a.
********************************************************/
static void __exit nvt_driver_exit(void)
{
	spi_unregister_driver(&nvt_spi_driver);
}

#if defined(CONFIG_DRM_PANEL)
late_initcall(nvt_driver_init);
#else
module_init(nvt_driver_init);
#endif
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek Touchscreen Driver");
MODULE_LICENSE("GPL");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
