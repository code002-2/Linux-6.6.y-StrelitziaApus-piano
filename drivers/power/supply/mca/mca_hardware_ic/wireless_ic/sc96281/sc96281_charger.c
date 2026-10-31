// SPDX-License-Identifier: GPL-2.0
/*
 * sc96281_charger.c
 *
 * wireless ic driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
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

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irqreturn.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
//#include <linux/power_supply.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>
#include <linux/sched/clock.h>
#include <uapi/linux/sched/types.h>

#include "inc/sc96281_reg.h"
#include "inc/sc96281_mtp_program.h"
//#include "inc/sc96281_pgm.h"
#include "inc/sc96281_firmware.h"
#include <linux/mca/common/mca_log.h>
#include <linux/mca/common/mca_event.h>
#include <linux/mca/common/mca_parse_dts.h>
#include <linux/mca/common/mca_adsp_glink.h>
#include <linux/mca/platform/platform_wireless_class.h>
#include <linux/mca/platform/platform_cp_class.h>
#include <linux/mca/common/mca_sysfs.h>
#include <linux/mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "sc96281"
#endif

#define SC96281_DRV_VERSION "1.0.0_G"
#define FOD_SIZE 16
#define I2C_RETRY_CNT 3

//--------------------------IIC API-----------------------------
static const struct regmap_config sc96281_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xFFFF,
};

static int __sc96281_read_block(struct sc96281 *sc, uint16_t reg, uint8_t *data,
				uint8_t length)
{
	int ret;

	ret = regmap_raw_read(sc->regmap, reg, data, length);
	if (ret < 0) {
		mca_log_err("i2c read fail: can't read from reg 0x%04X\n", reg);
	}

	return ret;
}

static int __sc96281_write_block(struct sc96281 *sc, uint16_t reg,
				 uint8_t *data, uint8_t length)
{
	int ret;

	ret = regmap_raw_write(sc->regmap, reg, data, length);
	if (ret < 0) {
		mca_log_err("i2c write fail: can't write 0x%04X: %d\n", reg,
			    ret);
	}

	return ret;
}

static int sc96281_read_block(struct sc96281 *sc, uint16_t reg, uint8_t *data,
			      uint8_t len)
{
	int ret;
	uint16_t alignAddr = reg & 0XFFFFFFFC;
	uint32_t alignOffs = reg % 4;
	uint8_t ext = (reg + len) % 4;
	uint16_t length = len;
	uint8_t *pbuf = NULL;

	if (sc->fw_program) {
		mca_log_err("firmware programming\n");
		return -1;
	}

	mutex_lock(&sc->i2c_rw_lock);
	length += alignOffs;
	if (ext != 0) {
		length += (4 - ext);
	}

	pbuf = kzalloc(length, GFP_KERNEL);

	ret = __sc96281_read_block(sc, alignAddr, pbuf, length);
	if (ret) {
		mca_log_err("read fail\n");
	} else {
		memcpy(data, pbuf + alignOffs, len);
	}

	kfree(pbuf);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int __sc96281_write_read_block(struct sc96281 *sc, uint16_t reg,
				      uint8_t *data, uint8_t len)
{
	int ret;
	uint16_t alignAddr = reg & 0XFFFFFFFC;
	uint32_t alignOffs = reg % 4;
	uint8_t ext = (reg + len) % 4;
	uint16_t length = len;
	uint8_t *pbuf = NULL;

	length += alignOffs;
	if (ext != 0) {
		length += (4 - ext);
	}

	pbuf = kzalloc(length, GFP_KERNEL);

	ret = __sc96281_read_block(sc, alignAddr, pbuf, length);
	if (ret) {
		mca_log_err("read fail\n");
	} else {
		memcpy(data, pbuf + alignOffs, len);
	}

	kfree(pbuf);

	return ret;
}

static int sc96281_write_block(struct sc96281 *sc, uint16_t reg, uint8_t *data,
			       uint8_t len)
{
	int ret;
	uint16_t alignAddr = reg & 0XFFFFFFFC;
	uint32_t alignOffs = reg % 4;
	uint8_t ext = (reg + len) % 4;
	uint16_t length = len;
	uint8_t *pbuf = NULL;

	if (sc->fw_program) {
		mca_log_err("firmware programming\n");
		return -1;
	}

	mutex_lock(&sc->i2c_rw_lock);
	pbuf = kzalloc(len + 8, GFP_KERNEL);
	if (alignOffs != 0) {
		ret = __sc96281_write_read_block(sc, alignAddr, pbuf, len + 8);
		if (ret) {
			mca_log_err(
				"sc96281 block read failed: reg=0x%04X len=%d ret=%d\n",
				alignAddr, alignOffs, ret);
			goto write_fail;
		}
		memcpy(pbuf + alignOffs, data, length);
		length += alignOffs;
	} else {
		memcpy(pbuf, data, length);
	}

	if (ext != 0) {
		ret = __sc96281_write_read_block(sc, alignAddr + length,
						 pbuf + length, 4 - ext);
		if (ret) {
			mca_log_err(
				"sc96281 block read failed: reg=0x%04X len=%d ret=%d\n",
				alignAddr + length, 4 - ext, ret);
			goto write_fail;
		}
		length += (4 - ext);
	}

	ret = __sc96281_write_block(sc, alignAddr, pbuf, length);

write_fail:
	kfree(pbuf);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

//-------------------sc96281 system interface-------------------
#define read_cust(sc, member, p)                                      \
	sc96281_read_block(sc, (uint64_t)(&(member)), (uint8_t *)(p), \
			   (uint8_t)sizeof(member))

#define write_cust(sc, member, p)                                      \
	sc96281_write_block(sc, (uint64_t)(&(member)), (uint8_t *)(p), \
			    (uint8_t)sizeof(member))

static int sc96281_rx_set_cmd(struct sc96281 *sc, uint32_t cmd);

__maybe_unused int sc96281_get_chipid(struct sc96281 *sc, uint16_t *chip_id)
{
	int ret;

	ret = read_cust(sc, cust_rx.chip_id, chip_id);
	if (ret) {
		mca_log_err("sc96281 get chip id fail\n");
	}

	return ret;
}

static int sc96281_get_fwver(struct sc96281 *sc, uint32_t *fw_ver)
{
	int ret;

	ret = read_cust(sc, cust_rx.firmware_ver, fw_ver);
	if (ret) {
		mca_log_err("sc96281 get fw ver fail\n");
	}

	return ret;
}

static int sc96281_get_image_fwver(const unsigned char *firmware,
				   const uint32_t len, uint32_t *image_ver)
{
	if (len < 0x200) {
		mca_log_err("Firmware image length is too short\n");
		return -1;
	}

	*image_ver = (uint32_t)firmware[0X100 + 4] & 0x00FF;
	*image_ver |= ((uint32_t)firmware[0X100 + 5] & 0x00FF) << 8;
	*image_ver |= ((uint32_t)firmware[0X100 + 6] & 0x00FF) << 16;
	*image_ver |= ((uint32_t)firmware[0X100 + 7] & 0x00FF) << 24;

	return 0;
}

__maybe_unused static int sc96281_get_hwver(struct sc96281 *sc,
					    uint32_t *hw_ver)
{
	int ret;

	ret = read_cust(sc, cust_rx.hardware_ver, hw_ver);
	if (ret) {
		mca_log_err("sc96281 get hw ver fail\n");
	}

	return ret;
}

__maybe_unused static int sc96281_get_gitver(struct sc96281 *sc,
					     uint32_t *git_ver)
{
	int ret;

	ret = read_cust(sc, cust_rx.git_ver, git_ver);
	if (ret) {
		mca_log_err("sc96281 get git ver fail\n");
	}

	return ret;
}

__maybe_unused int sc96281_get_mcode(struct sc96281 *sc, uint32_t *mcode)
{
	int ret;

	ret = read_cust(sc, cust_rx.mfr_code, mcode);
	if (ret) {
		mca_log_err("sc96281 get mcode fail\n");
	}

	return ret;
}

int sc96281_get_tdie(struct sc96281 *sc, uint16_t *tdie)
{
	int ret;

	ret = read_cust(sc, cust_rx.t_die, tdie);
	if (ret) {
		mca_log_err("sc96281 get tdie fail\n");
	} else {
		mca_log_info("sc96281 get tdie: %d\n", *tdie);
	}

	return ret;
}

int sc96281_check_i2c(struct sc96281 *sc)
{
	int ret = 0;
	uint8_t data = 0x55;

	ret = write_cust(sc, cust_rx.iic_check, &data);
	if (ret) {
		mca_log_err("sc96281 check i2c write fail\n");
		return ret;
	}

	msleep(20);

	ret = read_cust(sc, cust_rx.iic_check, &data);
	if (ret < 0) {
		mca_log_err("sc96281 check i2c read fail\n");
		return ret;
	}

	if (data == 0x55) {
		mca_log_info("i2c check ok!\n");
	} else {
		mca_log_err("i2c check failed!\n");
		return -1;
	}

	return ret;
}

//-------------------sc96281 RX interface-----------------------
static int sc96281_rx_set_cmd(struct sc96281 *sc, uint32_t cmd)
{
	int ret;

	ret = write_cust(sc, cust_rx.cmd, &cmd);
	if (ret) {
		mca_log_err("sc96281 rx set cmd %d fail\n", cmd);
	}

	return ret;
}

static int sc96281_rx_set_cust_cmd(struct sc96281 *sc, uint32_t cmd)
{
	int ret = 0;

	ret = write_cust(sc, cust_rx.mi_ctx.cmd, &cmd);
	if (ret) {
		mca_log_err("sc96281 rx set cust cmd %d fail\n", cmd);
	}

	return ret;
}

static int sc96281_rx_get_cust_flag(struct sc96281 *sc, uint32_t *func_flag)
{
	int ret = 0;

	ret = read_cust(sc, cust_rx.mi_ctx.fun_flag, func_flag);
	if (ret) {
		mca_log_err("sc96281 rx get cust flag fail\n");
	} else {
		mca_log_info("sc96281 rx get cust flag 0x%x\n", *func_flag);
	}

	return ret;
}

__maybe_unused int sc96281_rx_get_mode(struct sc96281 *sc, bool *is_epp)
{
	int ret;
	uint32_t rx_mode;

	ret = read_cust(sc, cust_rx.mode, &rx_mode);
	if (ret) {
		mca_log_err("sc96281 get rx mode fail\n");
		return -EINVAL;
	}

	if (rx_mode & WORK_MODE_EPP) {
		*is_epp = true;
		mca_log_info("SC96281 Rx power profile is EPP\n");
	} else {
		*is_epp = false;
		mca_log_info("SC96281 Rx power profile is BPP\n");
	}

	return ret;
}

__maybe_unused int sc96281_rx_get_ce_value(struct sc96281 *sc, int8_t *ce)
{
	int ret;

	ret = read_cust(sc, cust_rx.cep_value, ce);
	if (ret) {
		mca_log_err("sc96281 get rx value fail\n");
	}
	return ret;
}

__maybe_unused int sc96281_rx_get_frequecy(struct sc96281 *sc, uint16_t *freq)
{
	int ret;

	ret = read_cust(sc, cust_rx.power_freq, freq);
	if (ret) {
		mca_log_err("sc96281 get mcode fail\n");
	}

	return ret;
}

int sc96281_rx_get_vrect(struct sc96281 *sc, uint16_t *vrect)
{
	int ret;

	ret = read_cust(sc, cust_rx.vrect, vrect);
	if (ret) {
		mca_log_err("sc96281 get vrect fail\n");
	} else {
		mca_log_info("sc96281 get vrect: %d\n", *vrect);
	}

	return ret;
}

int sc96281_rx_get_vout(struct sc96281 *sc, uint16_t *volt)
{
	int ret;

	ret = read_cust(sc, cust_rx.vout, volt);
	if (ret) {
		mca_log_err("sc96281 get vout fail\n");
	} else {
		mca_log_info("sc96281 get vout: %d\n", *volt);
	}

	return ret;
}

int sc96281_rx_get_iout(struct sc96281 *sc, uint16_t *curr)
{
	int ret;

	ret = read_cust(sc, cust_rx.iout, curr);
	if (ret) {
		mca_log_err("sc96281 get iout fail\n");
	} else {
		mca_log_info("sc96281 get iout: %d\n", *curr);
	}

	return ret;
}

int sc96281_rx_set_Vout(struct sc96281 *sc, uint16_t vout)
{
	int ret;

	ret = write_cust(sc, cust_rx.target_vout, &vout);
	if (ret) {
		mca_log_err("sc96281 set vout fail\n");
	}

	ret = sc96281_rx_set_cmd(sc, AP_CMD_RX_VOUT_CHANGE);
	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	}

	return ret;
}

__maybe_unused int sc96281_rx_set_Ilimit(struct sc96281 *sc, uint16_t ilimt)
{
	int ret;

	ret = write_cust(sc, cust_rx.ilimit, &ilimt);
	if (ret) {
		mca_log_err("sc96281 set ilimit fail\n");
	}

	ret = sc96281_rx_set_cmd(sc, AP_CMD_MAX_I_CHANGE);
	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	}

	return ret;
}

__maybe_unused int sc96281_rx_set_vout_ovp(struct sc96281 *sc, uint16_t vovp)
{
	int ret;

	ret = write_cust(sc, cust_rx.ovp, &vovp);
	if (ret) {
		mca_log_err("sc96281 set vout ovp fail\n");
	}

	ret = sc96281_rx_set_cmd(sc, AP_CMD_OVP_CHANGE);
	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	}

	return ret;
}

__maybe_unused int sc96281_rx_set_vout_ocp(struct sc96281 *sc, uint16_t iocp)
{
	int ret;

	ret = write_cust(sc, cust_rx.ocp, &iocp);
	if (ret) {
		mca_log_err("sc96281 set iout ocp fail\n");
	}

	ret = sc96281_rx_set_cmd(sc, AP_CMD_OCP_CHANGE);
	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	}

	return ret;
}

__maybe_unused int sc96281_rx_set_otp(struct sc96281 *sc, uint16_t otp)
{
	int ret;

	ret = write_cust(sc, cust_rx.otp, &otp);
	if (ret) {
		mca_log_err("sc96281 set otp fail\n");
	}

	ret = sc96281_rx_set_cmd(sc, AP_CMD_OTP_CHANGE);
	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	}

	return ret;
}

__maybe_unused int sc96281_rx_send_ept(struct sc96281 *sc, ept_reason_t ept_v)
{
	int ret;

	ret = write_cust(sc, cust_rx.ept_reason, &ept_v);
	if (ret) {
		mca_log_err("sc96281 clear rx int fail\n");
	}

	ret = sc96281_rx_set_cmd(sc, AP_CMD_RX_SEND_EPT);
	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	}

	return ret;
}

int sc96281_rx_recv_fsk_pkt(struct sc96281 *sc, fsk_pkt_t *fsk)
{
	int ret;

	ret = read_cust(sc, cust_rx.fsk_pkt, fsk);
	if (ret) {
		mca_log_err("sc96281 read recv fsk pkt fail\n");
	}

	return ret;
}

int sc96281_rx_send_ask_pkt(struct sc96281 *sc, ask_pkt_t *ask, bool is_resp)
{
	int ret;

	ret = write_cust(sc, cust_rx.ask_pkt, ask);
	if (ret) {
		mca_log_err("sc96281 write send ask pkt 0x%x fail\n",
			    ask->header);
	}

	if (is_resp)
		ret = sc96281_rx_set_cust_cmd(sc, CUST_CMD_RX_TP_SEND);
	else
		ret = sc96281_rx_set_cmd(sc, AP_CMD_SEND_PPP);

	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	} else {
		mca_log_info("sc96281 set cmd ask 0x%X %X %X %X %X %X %X %X",
			     ask->header, ask->msg[0], ask->msg[1], ask->msg[2],
			     ask->msg[3], ask->msg[4], ask->msg[5],
			     ask->msg[6]);
	}

	return ret;
}

__maybe_unused int sc96281_rx_config_bpp_fod(struct sc96281 *sc,
					     fod_type_t *fod)
{
	int ret;

	ret = write_cust(sc, cust_rx.bpp_fod, fod);
	if (ret) {
		mca_log_err("sc96281 config bpp fod fail\n");
	}

	return ret;
}

__maybe_unused int sc96281_rx_config_epp_fod(struct sc96281 *sc,
					     fod_type_t *fod)
{
	int ret;

	ret = write_cust(sc, cust_rx.epp_fod, fod);
	if (ret) {
		mca_log_err("sc96281 config epp fod fail\n");
	}

	return ret;
}

int sc96281_set_fod(struct sc96281 *sc, fod_params_t *fod_params)
{
	int ret = 0;

	ret = write_cust(sc, cust_rx.bpp_fod, fod_params->params);
	ret |= write_cust(sc, cust_rx.epp_fod,
			  (uint8_t *)fod_params->params +
				  sizeof(cust_rx.bpp_fod));
	ret |= sc96281_rx_set_cust_cmd(sc, CUST_CMD_FOD_16_SEGMENT);
	if (ret)
		mca_log_err("sc96281 set 16-segment fod fail\n");
	else
		mca_log_info("sc96281 set 16-segment fod success\n");

	return ret;
}

int sc96281_rx_reneg(struct sc96281 *sc, int reneg_W)
{
	int ret;
	contract_t req;

	ret = read_cust(sc, cust_rx.neg_req_contract, &req);
	if (ret) {
		mca_log_err("read neg req fail\n");
		return ret;
	}

	req.guaranteed_power = reneg_W * 2;

	ret = write_cust(sc, cust_rx.neg_req_contract, &req);
	if (ret) {
		mca_log_err("write neg req fail\n");
		return ret;
	}

	return sc96281_rx_set_cmd(sc, AP_CMD_RX_RENEG);
}

int sc96281_vsys_ctrl(struct sc96281 *sc, bool enable)
{
	int ret;
	uint8_t vdd = enable ? 3 : 0;

	ret = write_cust(sc, cust_rx.vsys_cfg, &vdd);
	if (ret) {
		mca_log_err("write vsys ctrl fail\n");
	} else {
		mca_log_info("write vsys ctrl to %d success\n", enable);
	}

	return ret;
}

int sc96281_rx_clr_int(struct sc96281 *sc, uint32_t rxint)
{
	int ret;

	ret = write_cust(sc, cust_rx.irq_clr, &rxint);
	if (ret) {
		mca_log_err("sc96281 clear rx int fail\n");
	}

	return ret;
}

int sc96281_rx_get_int(struct sc96281 *sc, uint32_t *rxint)
{
	int ret;

	ret = read_cust(sc, cust_rx.irq_flag, rxint);
	if (ret) {
		mca_log_err("sc96281 clear rx int fail\n");
	}

	return ret;
}

//-------------------sc96281 TX interface-------------------
static int sc96281_tx_set_cmd(struct sc96281 *sc, uint32_t cmd)
{
	int ret;

	ret = write_cust(sc, cust_tx.cmd, &cmd);
	if (ret) {
		mca_log_err("sc96281 tx set cmd fail\n");
	}

	return ret;
}

static int sc96281_tx_enable(struct sc96281 *sc, bool enable)
{
	int ret;

	if (enable) {
		ret = sc96281_tx_set_cmd(sc, AP_CMD_TX_ENABLE);
		if (ret) {
			mca_log_err("enter TX mode fail\n");
		} else {
			mca_log_err("enter TX mode success\n");
		}
	} else {
		ret = sc96281_tx_set_cmd(sc, AP_CMD_TX_DISABLE);
		if (ret) {
			mca_log_err("exit TX mode fail\n");
		} else {
			mca_log_err("exit TX mode success\n");
		}
	}
	return ret;
}

__maybe_unused int sc96281_tx_get_ce_value(struct sc96281 *sc, int8_t *ce)
{
	int ret;

	ret = read_cust(sc, cust_tx.cep_value, ce);
	if (ret) {
		mca_log_err("sc96281 get tx value fail\n");
	}
	return ret;
}

static int sc96281_tx_fod_enable(struct sc96281 *sc, bool enable)
{
	int ret;

	if (enable) {
		ret = sc96281_tx_set_cmd(sc, AP_CMD_TX_FOD_ENABLE);
		if (ret) {
			mca_log_err("enable tx fod fail\n");
		} else {
			mca_log_err("enable tx fod success\n");
		}
	} else {
		ret = sc96281_tx_set_cmd(sc, AP_CMD_TX_FOD_DISABLE);
		if (ret) {
			mca_log_err("disable tx fod fail\n");
		} else {
			mca_log_err("disable tx fod success\n");
		}
	}

	return ret;
}

__maybe_unused int sc96281_tx_send_fsk_pkt(struct sc96281 *sc, fsk_pkt_t *fsk)
{
	int ret;

	ret = write_cust(sc, cust_tx.fsk_pkt, fsk);
	if (ret) {
		mca_log_err("sc96281 write send fsk pkt 0x%X fail\n",
			    fsk->header);
	}

	ret = sc96281_tx_set_cmd(sc, AP_CMD_SEND_PPP);
	if (ret) {
		mca_log_err("sc96281 set cmd fail\n");
	} else {
		mca_log_info("SC96281 tx send FSK=0x%X %X %X %X %X %X %X %X",
			     fsk->header, fsk->msg[0], fsk->msg[1], fsk->msg[2],
			     fsk->msg[3], fsk->msg[4], fsk->msg[5],
			     fsk->msg[6]);
	}

	return ret;
}

__maybe_unused static int sc96281_tx_recv_ask_pkt(struct sc96281 *sc,
						  ask_pkt_t *ask)
{
	int ret;

	ret = read_cust(sc, cust_tx.ask_pkt, ask);
	if (ret) {
		mca_log_err("sc96281 read recv ask pkt fail\n");
	} else {
		mca_log_info("SC96281 tx recv ASK=0x%X %X %X %X %X %X %X %X",
			     ask->header, ask->msg[0], ask->msg[1], ask->msg[2],
			     ask->msg[3], ask->msg[4], ask->msg[5],
			     ask->msg[6]);
	}

	return ret;
}

int sc96281_tx_clr_int(struct sc96281 *sc, uint32_t txint)
{
	int ret;

	ret = write_cust(sc, cust_tx.irq_clr, &txint);
	if (ret) {
		mca_log_err("sc96281 clear tx int fail\n");
	}

	return ret;
}

int sc96281_tx_get_int(struct sc96281 *sc, uint32_t *txint)
{
	int ret;

	ret = read_cust(sc, cust_tx.irq_flag, txint);
	if (ret) {
		mca_log_err("sc96281 clear tx int fail\n");
	}

	return ret;
}

#if 0
//-------------------Interrupt interface-------------------
static int ocp_irq_handler(struct sc96281 *sc)
{
	mca_log_info("trigger\n");

    return 0;
}

static int vout_ovp_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int clamp_ovp_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int ngate_ovp_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int vout_lvp_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int otp_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int otp_160c_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int sleep_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int mode_change_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

//rx
static int rx_fsk_recv_irq_handler(struct sc96281 *sc)
{
    int ret;
    fsk_pkt_t fsk_pkt;

    mca_log_info("trigger\n");

    ret = sc96281_rx_recv_fsk_pkt(sc, &fsk_pkt);

    return ret;
}

static int rx_ppp_success_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_afc_det_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_profile_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_poweron_irq_handler(struct sc96281 *sc)
{
    int ret;
    uint32_t regval;

    mca_log_info("trigger\n");

    ret = sc96281_get_fwver(sc, &regval);
    mca_log_info(" fw ver : 0x%08x\n", regval);
    ret = sc96281_get_hwver(sc, &regval);
    mca_log_info(" hw ver : 0x%08x\n", regval);
    ret = sc96281_get_gitver(sc, &regval);
    mca_log_info(" git ver : 0x%08x\n", regval);

    return ret;
}

static int rx_ss_pkt_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_id_pkt_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_config_pkt_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_ready_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_ldo_on_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_ldo_off_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_pldo_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_scp_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_reneg_success_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int rx_reneg_fail_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

//tx
static int tx_ask_recv_irq_handler(struct sc96281 *sc)
{
    int ret;
    ask_pkt_t ask_pkt;

    mca_log_info("trigger\n");

    ret = sc96281_tx_recv_ask_pkt(sc, &ask_pkt);
    return ret;
}

static int tx_ppp_timeout_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_ppp_success_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_profile_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_brg_ocp_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_detect_rx_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_remove_power_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_pt_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}


static int tx_fod_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_detect_tx_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_cep_timeout_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_rpp_timeout_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_ping_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_ss_pkg_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_id_pkg_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_cfg_pkg_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}

static int tx_power_on_irq_handler(struct sc96281 *sc)
{
    mca_log_info("trigger\n");
    return 0;
}


struct interrupt_handler {
    uint32_t bit_mask;
    int (*handler)(struct sc96281 *sc);
};

#define DECL_INTERRUPT_HANDLER(bit_m, xhandler) \
	{                                       \
		.bit_mask = bit_m,              \
		.handler = xhandler,            \
	}

static const struct interrupt_handler rx_irq_handlers[] = {
    DECL_INTERRUPT_HANDLER(WP_IRQ_OCP, ocp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_OVP, vout_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_CLAMP_OVP, clamp_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_NGATE_OVP, ngate_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_LVP, vout_lvp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_OTP, otp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_OTP_160, otp_160c_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_SLEEP, sleep_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_MODE_CHANGE, mode_change_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_PKT_RECV, rx_fsk_recv_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_PPP_SUCCESS, rx_ppp_success_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_AFC, rx_afc_det_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_PROFILE, rx_profile_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_POWER_ON, rx_poweron_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_SS_PKT, rx_ss_pkt_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_ID_PKT, rx_id_pkt_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_CFG_PKT, rx_config_pkt_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_READY, rx_ready_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_LDO_ON, rx_ldo_on_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_LDO_OFF, rx_ldo_off_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_LDO_OPP, rx_pldo_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_SCP, rx_scp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_RENEG_SUCCESS, rx_reneg_success_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_RX_RENEG_FAIL, rx_reneg_fail_irq_handler),
};

static const struct interrupt_handler tx_irq_handlers[] = {
    DECL_INTERRUPT_HANDLER(WP_IRQ_OCP, ocp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_OVP, vout_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_CLAMP_OVP, clamp_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_NGATE_OVP, ngate_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_LVP, vout_lvp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_OTP, otp_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_OTP_160, otp_160c_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_SLEEP, sleep_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_MODE_CHANGE, mode_change_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_PKT_RECV, tx_ask_recv_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_PPP_TIMEOUT, tx_ppp_timeout_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_PPP_SUCCESS, tx_ppp_success_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_PROFILE, tx_profile_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_DET_RX, tx_detect_rx_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_EPT, tx_remove_power_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_PT, tx_pt_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_FOD, tx_fod_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_DET_TX, tx_detect_tx_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_CEP_TIMEOUT, tx_cep_timeout_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_RPP_TIMEOUT, tx_rpp_timeout_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_PING, tx_ping_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_SS_PKT, tx_ss_pkg_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_ID_PKT, tx_id_pkg_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_CFG_PKT, tx_cfg_pkg_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_POWER_ON, tx_power_on_irq_handler),
    DECL_INTERRUPT_HANDLER(WP_IRQ_TX_BRG_OCP, tx_brg_ocp_irq_handler),
};

/*********************************************************************/
static ssize_t sc96281_show_registers(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct sc96281 *sc = dev_get_drvdata(dev);
    int i = 0;
    uint32_t data;
    uint8_t tmpbuf[500];
    int len;
    int idx = 0;
    int ret;

    for (i = 0; i < 0x200 / 4; i++) {
        ret = sc96281_read_block(sc, i * 4, (uint8_t *)&data, 4);
        if (!ret)
        {
            len = snprintf(tmpbuf, PAGE_SIZE - idx,
                    "Reg[%04X] = 0x%08x\n", i * 4, data);
            memcpy(&buf[idx], tmpbuf, len);
            idx += len;
        }
    }

    return idx;
}

static ssize_t sc96281_store_register(struct device *dev,
            struct device_attribute *attr, const char *buf, size_t count)
{
    struct sc96281 *sc = dev_get_drvdata(dev);
    int ret;
    int reg;
    int len;
    uint32_t val;

    ret = sscanf(buf, "%x %x %x", &reg, &val, &len);
    if (ret == 3 && reg >= 0x0000 && reg <= 0x0200)
    {
        sc96281_write_block(sc, reg, (uint8_t *)&val, len);
    }

    return count;
}

static DEVICE_ATTR(registers, 0660, sc96281_show_registers,
        sc96281_store_register);

static void sc96281_create_device_node(struct device *dev)
{
    device_create_file(dev, &dev_attr_registers);
}
#endif

// -------------------ops functions-------------------
static int sc96281_enable_reverse_chg(bool enable, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	return sc96281_tx_enable(chip, enable);
}

static int sc96281_is_present(int *present, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	if (chip->proc_data.power_good_flag)
		*present = 1;
	else
		*present = 0;

	return 0;
}

static int sc96281_set_vout(int vout, void *data)
{
	int ret = 0;
	int8_t cep = 0;
	struct sc96281 *chip = (struct sc96281 *)data;

	if (!chip->proc_data.power_good_flag)
		return -1;

	mutex_lock(&chip->data_transfer_lock);
	msleep(20);

	if (chip->proc_data.parallel_charge == true) {
		ret = sc96281_rx_get_ce_value(chip, &cep);
		if (ABS(cep) > ABS_CEP_VALUE &&
		    chip->proc_data.vout_setted <= vout) {
			mca_log_info("vol:%d ,cep %d, not set\n", vout, cep);
			goto exit;
		}
	}

	vout = (vout < VOUT_SET_MIN_MV) ? VOUT_SET_DEFAULT_MV : vout;
	vout = (vout > VOUT_SET_MAX_MV) ? VOUT_SET_MAX_MV : vout;

	ret = sc96281_rx_set_Vout(chip, (uint16_t)vout);
	if (ret) {
		mca_log_err("sc96281 set vout fail\n");
	} else {
		chip->proc_data.vout_setted = vout;
		mca_log_info("wls_set_vout: %d, cep: %d\n", vout, cep);
	}
exit:
	mutex_unlock(&chip->data_transfer_lock);
	return ret;
}

static int sc96281_get_vout(int *vout, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint16_t vol = 0;

	if (!chip->proc_data.power_good_flag) {
		*vout = 0;
		return -1;
	}

	ret = sc96281_rx_get_vout(chip, &vol);
	if (!ret)
		*vout = (int)vol;

	return ret;
}

static int sc96281_get_iout(int *iout, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint16_t cur = 0;

	if (!chip->proc_data.power_good_flag) {
		*iout = 0;
		return -1;
	}

	ret = sc96281_rx_get_iout(chip, &cur);
	if (!ret)
		*iout = (int)cur;

	return ret;
}

static int sc96281_get_vrect(int *vrect, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint16_t rect = 0;

	if (!chip->proc_data.power_good_flag) {
		*vrect = 0;
		return -1;
	}

	ret = sc96281_rx_get_vrect(chip, &rect);
	if (!ret)
		*vrect = (int)rect;

	return ret;
}

static int sc96281_get_temp(int *temp, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint16_t tdie = 0;

	if (!chip->proc_data.power_good_flag) {
		*temp = 0;
		return -1;
	}

	ret = sc96281_get_tdie(chip, &tdie);
	if (!ret)
		*temp = (int)tdie / 10;
	mca_log_info("wls_get_rx_temp: %d\n", *temp);
	return ret;
}

static int sc96281_get_fw_upgrade_fail_info(char **info, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	*info = chip->fw_upgrade_fail_info;
	return 0;
}

#define SC96281_REG_RSV_EPPMODE_FAIL 0x1DA
static int sc96281_rx_get_rsv_eppmode_fail(int *fail, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	u16 val = 0;
	int ret;

	ret = sc96281_read_block(chip, SC96281_REG_RSV_EPPMODE_FAIL,
				 (uint8_t *)&val, 2);
	if (ret)
		mca_log_err("read rsv eppmode fail reg err: %d\n", ret);
	else
		mca_log_info("rsv_eppmode_fail: %d\n", val);

	*fail = val;
	return ret;
}

static int sc96281_get_tx_adapter(int *adapter, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	if (chip->proc_data.power_good_flag)
		*adapter = chip->proc_data.adapter_type;
	else
		*adapter = 0;

	mca_log_info("wls_get_adapter: %d\n", chip->proc_data.adapter_type);

	return 0;
}

static int sc96281_get_tx_adapter_by_i2c(int *adapter, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	u8 val = 0;
	int ret;

	if (!chip->proc_data.power_good_flag) {
		*adapter = 0;
		return 0;
	}

	/* 0x01D8: TX adapter type register */
	ret = sc96281_read_block(chip, 0x01D8, &val, 1);
	if (!ret)
		chip->proc_data.adapter_type = val;

	*adapter = chip->proc_data.adapter_type;
	mca_log_info("wls_get_adapter: %d\n", chip->proc_data.adapter_type);

	return 0;
}

static int sc96281_set_enable_mode(bool enable, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	int en = !!enable;
	int gpio_enable_val = 0;

	mca_log_info("set enable mode:%d\n", enable);

	ret = gpio_direction_output(chip->enable_gpio, !en);
	if (ret)
		mca_log_err("set direction for enable gpio [%d] fail\n",
			    chip->enable_gpio);

	gpio_enable_val = gpio_get_value(chip->enable_gpio);
	mca_log_info("enable gpio val is :%d\n", gpio_enable_val);

	return ret;
}

static int sc96281_is_car_adapter(bool *enable, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	if (chip->proc_data.is_car_tx)
		*enable = true;
	else
		*enable = false;

	return 0;
}

static int sc96281_get_rx_rtx_mode(int *mode, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t rx_mode;

	ret = read_cust(chip, cust_rx.mode, &rx_mode);
	if (ret) {
		mca_log_err("wls_get_rtx_mode fail\n");
		*mode = RX_MODE;
		return ret;
	} else {
		if (rx_mode & WORK_MODE_TX)
			*mode = RTX_MODE;
		else
			*mode = RX_MODE;
	}

	mca_log_info("%d\n", *mode);
	return ret;
}

static int sc96281_set_input_current_limit(int value, void *data)
{
	mca_log_info("%d\n", value);
	return mca_adsp_glink_write_prop(ADSP_PROP_ID_DC_INPUT_CURR_LIMIT,
					 &value, sizeof(value));
}

static int sc96281_get_rx_int_flag(int *int_flag, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	*int_flag = chip->proc_data.int_flag;

	return 0;
}

static int sc96281_get_rx_power_mode(u8 *power_mode, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t rx_mode;

	ret = read_cust(chip, cust_rx.mode, &rx_mode);
	if (ret) {
		mca_log_err("wls_get_rx_pwrmode fail\n");
		*power_mode = RX_MODE;
		return ret;
	} else {
		if (rx_mode & WORK_MODE_EPP)
			*power_mode = 1;
		else
			*power_mode = 0;
	}

	mca_log_info("%d\n", *power_mode);
	return ret;
}

static int sc96281_get_auth_value(int *value, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t auth_data = 0, uuid = 0;
	uint8_t adapter_type = ADAPTER_NONE;

	if (!chip->proc_data.power_good_flag) {
		*value = 0;
		return ret;
	}

	ret = read_cust(chip, cust_rx.mi_ctx.fun_flag, &auth_data);
	if (ret) {
		mca_log_err("fail, ret = %d\n", ret);
		return ret;
	} else {
		mca_log_info("auth_data = 0x%x\n", auth_data);
	}

	if (auth_data & PROJECT_FLAG_ADAPTER_TYPE) {
		ret = read_cust(chip, cust_rx.mi_ctx.adapter_type,
				&adapter_type);
		if (!ret)
			chip->proc_data.adapter_type =
				(adapter_type != ADAPTER_NONE) ?
					adapter_type :
					ADAPTER_AUTH_FAILED;
	}

	if (auth_data & PROJECT_FLAG_UUID) {
		ret = read_cust(chip, cust_rx.mi_ctx.tx_uuid, &uuid);
		if (!ret) {
			chip->proc_data.uuid[0] = (uuid >> 24) & 0xFF;
			chip->proc_data.uuid[1] = (uuid >> 16) & 0xFF;
			chip->proc_data.uuid[2] = (uuid >> 8) & 0xFF;
			chip->proc_data.uuid[3] = uuid & 0xFF;
		}
	}

	*value = auth_data;

	mca_log_info("adapter type: %d\n", chip->proc_data.adapter_type);
	mca_log_info("uuid: 0x%x, 0x%x, 0x%x, 0x%x\n", chip->proc_data.uuid[0],
		     chip->proc_data.uuid[1], chip->proc_data.uuid[2],
		     chip->proc_data.uuid[3]);

	return 0;
}

static int sc96281_set_adapter_voltage(int voltage, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;

	if ((voltage < ADAPTER_VOL_MIN_MV) || (voltage > ADAPTER_VOL_MAX_MV))
		voltage = ADAPTER_VOL_DEFAULT_MV;

	ret = write_cust(chip, cust_rx.mi_ctx.fc_volt, &voltage);
	if (ret) {
		mca_log_err("wls_set_adapter_voltage to %dmV fail\n", voltage);
		return ret;
	}
	ret = sc96281_rx_set_cust_cmd(chip, CUST_CMD_RX_FAST_CHARGE);

	mca_log_info("wls_set_adapter_voltage to %dmV success\n", voltage);

	return ret;
}

static int sc96281_get_tx_uuid(u8 *uuid, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	if (chip->proc_data.power_good_flag)
		memcpy(uuid, chip->proc_data.uuid,
		       sizeof(chip->proc_data.uuid));
	else
		return -1;

	return 0;
}

static int sc96281_set_fod_params(int value, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	int uuid = 0, i = 0, j = 0;
	bool found = true;

	uuid |= chip->proc_data.uuid[0] << 24;
	uuid |= chip->proc_data.uuid[1] << 16;
	uuid |= chip->proc_data.uuid[2] << 8;
	uuid |= chip->proc_data.uuid[3];
	mca_log_info("uuid: 0x%x,0x%x,0x%x,0x%x\n", chip->proc_data.uuid[0],
		     chip->proc_data.uuid[1], chip->proc_data.uuid[2],
		     chip->proc_data.uuid[3]);

	for (i = 0; i < chip->fod_params_size; i++) {
		found = true;
		if (uuid != chip->fod_params[i].uuid) {
			found = false;
			continue;
		}
		/* found fod by uuid */
		if (found) {
			mca_log_info("found fod params\n");
			for (j = 0; j < chip->fod_params[i].length; j++) {
				mca_log_info(
					"fod gain:%d offset:%d\n",
					chip->fod_params[i].params[j].gain,
					chip->fod_params[i].params[j].offset);
			}
			sc96281_set_fod(chip, &chip->fod_params[i]);
			return 0;
		}
	}

	if (!found) {
		mca_log_info("can not found fod params, use default fod\n");
		sc96281_set_fod(chip, &chip->fod_params_default);
	}

	return 0;
}

static int sc96281_set_debug_fod_params(void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	struct fod_params_t fod_params;
	int index = chip->wls_debug_one_fod_index;
	int i = 0, k = 0;
	int uuid = 0;
	bool found = false;

	if (!chip->proc_data.power_good_flag) {
		mca_log_err("power good flag is false\n");
		return -1;
	}

	if (chip->wls_debug_set_fod_type == WLS_DEBUG_SET_FOD_ALL_DIRECTLY) {
		fod_params.type = chip->wls_debug_all_fod_params->type;
		fod_params.length = chip->wls_debug_all_fod_params->length;
		for (i = 0; i < fod_params.length; i++) {
			fod_params.params[i].gain =
				chip->wls_debug_all_fod_params->params[i].gain;
			fod_params.params[i].offset =
				chip->wls_debug_all_fod_params->params[i].offset;
		}
		sc96281_set_fod(chip, &fod_params);
		return 0;
	}

	uuid |= chip->proc_data.uuid[0] << 24;
	uuid |= chip->proc_data.uuid[1] << 16;
	uuid |= chip->proc_data.uuid[2] << 8;
	uuid |= chip->proc_data.uuid[3];
	//check all params
	for (i = 0; i < chip->fod_params_size; i++) {
		found = true;
		// uuid checking
		if (uuid != chip->fod_params[i].uuid) {
			found = false;
			continue;
		}
		// found
		if (found) {
			mca_log_info("uuid: 0x%x,0x%x,0x%x,0x%x\n",
				     chip->proc_data.uuid[0],
				     chip->proc_data.uuid[1],
				     chip->proc_data.uuid[2],
				     chip->proc_data.uuid[3]);
			if (chip->wls_debug_set_fod_type ==
				    WLS_DEBUG_SET_FOD_EPP_ALL &&
			    chip->fod_params[i].length ==
				    chip->wls_debug_all_fod_params->length) {
				for (k = 0; k < chip->fod_params[i].length;
				     k++) {
					chip->fod_params[i].params[k].gain =
						chip->wls_debug_all_fod_params
							->params[k]
							.gain;
					chip->fod_params[i].params[k].offset =
						chip->wls_debug_all_fod_params
							->params[k]
							.offset;
				}
			} else if (chip->wls_debug_set_fod_type ==
					   WLS_DEBUG_SET_FOD_EPP_ONE &&
				   index < chip->fod_params[i].length) {
				chip->fod_params[i].params[index].gain =
					chip->wls_debug_one_fod_param.gain;
				chip->fod_params[i].params[index].offset =
					chip->wls_debug_one_fod_param.offset;
			}
			sc96281_set_fod(chip, &chip->fod_params[i]);
			return 0;
		}
	}

	if (chip->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3) {
		found = true;
		mca_log_info("fod epp+ default\n");
		if (chip->wls_debug_set_fod_type == WLS_DEBUG_SET_FOD_EPP_ALL &&
		    chip->fod_params_default.length ==
			    chip->wls_debug_all_fod_params->length) {
			for (k = 0; k < chip->fod_params_default.length; k++) {
				chip->fod_params_default.params[k].gain =
					chip->wls_debug_all_fod_params
						->params[k]
						.gain;
				chip->fod_params_default.params[k].offset =
					chip->wls_debug_all_fod_params
						->params[k]
						.offset;
			}
		} else if (chip->wls_debug_set_fod_type ==
				   WLS_DEBUG_SET_FOD_EPP_ONE &&
			   index < chip->fod_params_default.length) {
			chip->fod_params_default.params[index].gain =
				chip->wls_debug_one_fod_param.gain;
			chip->fod_params_default.params[index].offset =
				chip->wls_debug_one_fod_param.offset;
		}
		sc96281_set_fod(chip, &chip->fod_params_default);
	}
	/*
    else if (((chip->proc_data.adapter_type == ADAPTER_QC3) || (chip->proc_data.adapter_type == ADAPTER_PD)) &&
            (!chip->proc_data.epp)) {
        found = true;
        mca_log_info("fod bpp plus\n");
        sc96281_set_fod(chip, &chip->fod_params_bpp_plus);
    } 
    */

	if (!found)
		mca_log_info(
			"can not found fod params, uuid: 0x%x,0x%x,0x%x,0x%x\n",
			chip->proc_data.uuid[0], chip->proc_data.uuid[1],
			chip->proc_data.uuid[2], chip->proc_data.uuid[3]);

	return 0;
}

static int sc96281_set_debug_fod(int *args, int count, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	uint8_t index = 0;

	switch (args[0]) {
	case DEBUG_SET_ONE_EPP_FOD:
		mca_log_info("set one epp fod\n");
		chip->wls_debug_set_fod_type = WLS_DEBUG_SET_FOD_EPP_ONE;
		index = args[1];
		chip->wls_debug_one_fod_index = index;
		chip->wls_debug_one_fod_param.gain = args[2];
		chip->wls_debug_one_fod_param.offset = args[3];
		sc96281_set_debug_fod_params(chip);
		break;
	case DEBUG_SET_ALL_EPP_FOD:
		mca_log_info("set all epp fod\n");
		if (chip->wls_debug_all_fod_params == NULL)
			chip->wls_debug_all_fod_params = kmalloc(
				sizeof(struct fod_params_t), GFP_KERNEL);
		chip->wls_debug_all_fod_params->length = count / 2;
		chip->wls_debug_set_fod_type = WLS_DEBUG_SET_FOD_EPP_ALL;
		for (index = 0; index < chip->wls_debug_all_fod_params->length;
		     ++index) {
			chip->wls_debug_all_fod_params->params[index].gain =
				args[index * 2 + 1];
			chip->wls_debug_all_fod_params->params[index].offset =
				args[index * 2 + 2];
		}
		sc96281_set_debug_fod_params(chip);
		break;
	case DEBUG_SET_ALL_FOD:
		mca_log_info("wls debug set all fod\n");
		if (chip->wls_debug_all_fod_params == NULL)
			chip->wls_debug_all_fod_params = kmalloc(
				sizeof(struct fod_params_t), GFP_KERNEL);
		chip->wls_debug_all_fod_params->type = args[1];
		chip->wls_debug_all_fod_params->length = count / 2 - 1;
		chip->wls_debug_set_fod_type = WLS_DEBUG_SET_FOD_ALL_DIRECTLY;
		for (index = 0; index < chip->wls_debug_all_fod_params->length;
		     ++index) {
			chip->wls_debug_all_fod_params->params[index].gain =
				args[index * 2 + 2];
			chip->wls_debug_all_fod_params->params[index].offset =
				args[index * 2 + 3];
		}
		sc96281_set_debug_fod_params(chip);
		break;
	default:
		mca_log_err("not support debug_fod_type, return\n");
		break;
	}

	return 0;
}

static int sc96281_get_debug_fod_type(WLS_DEBUG_SET_FOD_TYPE *type, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	*type = chip->wls_debug_set_fod_type;

	return 0;
}

static int sc96281_get_rx_fastcharge_status(uint8_t *fc_flag, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t fastchg_result = 0;

	ret = sc96281_rx_get_cust_flag(chip, &fastchg_result);
	if (ret)
		return ret;

	*fc_flag = (fastchg_result & PROJECT_FLAG_FAST_CHARGE);

	chip->proc_data.fc_flag = *fc_flag;

	mca_log_info("fastch result: %d\n", *fc_flag);

	return ret;
}

static uint8_t sizeof_msg(uint8_t header)
{
	uint8_t len;
	if (header < 0x20)
		len = 1;
	else if (header < 0x80)
		len = 2 + ((header - 0x20) >> 4);
	else if (header < 0xe0)
		len = 8 + ((header - 0x80) >> 3);
	else
		len = 20 + ((header - 0xe0) >> 2);
	return len;
}

static uint8_t private_sizeof(uint8_t header, uint8_t cmd)
{
	if (header == 0x2F && cmd == 0x62)
		return 4;
	else
		return 0;
}

static int sc96281_receive_transparent_data(uint8_t *rcv_value, int buff_len,
					    int *rcv_len, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	fsk_pkt_t fsk;
	int i = 0;
	int sent_len = 0, data_len = 0;
	int rcv_index = 0;
	bool is_resp = false;

	if (!chip->proc_data.power_good_flag)
		return ret;

	ret = sc96281_rx_recv_fsk_pkt(chip, &fsk);
	if (ret)
		return ret;

	mca_log_info("receive_transparent_data fsk=%X %X %X %X %X %X %X %X\n",
		     fsk.buff[0], fsk.buff[1], fsk.buff[2], fsk.buff[3],
		     fsk.buff[4], fsk.buff[5], fsk.buff[6], fsk.buff[7]);

	if ((chip->sent_pri_packet.header == 0x05 && fsk.header >= 0xF0 &&
	     fsk.header <= 0xF4) ||
	    (fsk.header == 0 || fsk.header == 0x33 || fsk.header == 0x55 ||
	     fsk.header == 0xFF)) {
		is_resp = true;
		data_len = 1;
	} else {
		data_len = private_sizeof(fsk.header, fsk.msg[0]);
	}
	if (data_len == 0)
		data_len = sizeof_msg(fsk.header);
	sent_len = sizeof_msg(chip->sent_pri_packet.header) + 1;

	mca_log_info("receive_transparent_data data_length=%d\n", data_len);
	if (data_len > buff_len) {
		mca_log_info("receive_transparent_data buffer overflow\n");
		data_len = 0;
		return -1;
	}

	*rcv_len = (sent_len << 4) | (data_len);
	memcpy(&rcv_value[rcv_index], chip->sent_pri_packet.buff, sent_len);
	rcv_index += sent_len;

	if (is_resp) {
		rcv_value[rcv_index] = fsk.header;
		mca_log_info("receive_transparent_data fsk.header=0x%x\n",
			     fsk.header);
	} else if (fsk.header == 0x2F && fsk.msg[0] == 0x62) {
		for (i = 0; i < data_len; i++) {
			rcv_value[rcv_index + i] = fsk.buff[i];
			mca_log_info(
				"receive_transparent_data i=%d, data[i]=0x%x\n",
				i, rcv_value[rcv_index + i]);
		}
	} else {
		for (i = 0; i < data_len; i++) {
			rcv_value[rcv_index + i] = fsk.msg[i];
			mca_log_info(
				"receive_transparent_data i=%d, data[i]=0x%x\n",
				i, rcv_value[rcv_index + i]);
		}
	}
	mca_log_info("receive_transparent_data rcv_len=0x%x\n", *rcv_len);
	return ret;
}

static int sc96281_send_transparent_data(uint8_t *send_data, uint8_t length,
					 void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	ask_pkt_t ask;
	bool is_resp = false;

	mca_log_info(
		"data[0] = 0x%x data[1] = 0x%x data[2] = 0x%x, length=%d\n",
		send_data[0], send_data[1], send_data[2], length);

	if (send_data[0] == 0 || send_data[0] == 1)
		is_resp = true;
	memcpy(ask.buff, &send_data[1],
	       length - 1); //compatible to existed code,ignore data[0]

	mutex_lock(&chip->data_transfer_lock);

	ret = sc96281_rx_send_ask_pkt(chip, &ask, is_resp);
	if (ret) {
		mca_log_err("send transparent data %d failed\n", length);
	} else {
		mca_log_info("send transparent data %d success\n", length);
		memcpy(chip->sent_pri_packet.buff, &send_data[1], length - 1);
	}

	mutex_unlock(&chip->data_transfer_lock);

	return ret;
}

static int sc96281_get_ss_voltage(int *ss_voltage, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint8_t ss = 0;

	if (!chip->proc_data.power_good_flag) {
		*ss_voltage = 0;
		return ret;
	}

	ret = read_cust(chip, cust_rx.sig_strength, &ss);
	if (ret) {
		mca_log_err("get ss failed\n");
		return ret;
	}

	*ss_voltage = ss * 8500 / 255;
	chip->proc_data.ss_voltage = *ss_voltage;
	mca_log_info("ss=%d, ss_voltage=%d\n", ss, *ss_voltage);

	return ret;
}

static int sc96281_do_renego(uint8_t max_power, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;

	mca_log_info("max_power = %d\n", max_power);

	if (!chip->proc_data.power_good_flag)
		return -1;

	//ret = sc96281_rx_reneg(chip, max_power);
	max_power *= 2;

	ret = write_cust(chip, cust_rx.mi_ctx.reneg_param, &max_power);
	if (ret) {
		mca_log_err("renegotiate write parameter failed\n");
		return ret;
	}

	ret = sc96281_rx_set_cust_cmd(chip, CUST_CMD_RX_RENEG);
	if (ret) {
		mca_log_err("renegotiate cmd failed\n");
		return ret;
	}

	return ret;
}

static int sc96281_set_parallel_charge(bool parallel, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	chip->proc_data.parallel_charge = parallel;
	mca_log_info("set_parallel_charge: %d\n", parallel);

	return 0;
}

static int sc96281_get_vout_setted(int *vout_setted, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	*vout_setted = chip->proc_data.vout_setted;
	mca_log_info("vout_setted is %d\n", *vout_setted);

	return 0;
}

static int sc96281_get_poweroff_err_code(uint8_t *err_code, void *data)
{
	//struct sc96281 *chip = (struct sc96281 *)data;

	return 0;
}

static int sc96281_get_rx_err_code(uint8_t *err_code, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t code = 0;

	ret = read_cust(chip, cust_rx.err_code, &code);
	if (ret) {
		mca_log_err("sc96281 rx get error code fail\n");
		return ret;
	}

	switch (code) {
	case ERROR_CODE_RX_AP_CMD:
		mca_log_info("sc96281 rx error AP_CMD\n");
		break;
	case ERROR_CODE_RX_AC_LOSS:
		mca_log_info("sc96281 rx error AC_LOSS\n");
		break;
	case ERROR_CODE_RX_SS_OVP:
		mca_log_info("sc96281 rx error SS_OVP\n");
		break;
	case ERROR_CODE_RX_VOUT_OVP:
		mca_log_info("sc96281 rx error VOUT_OVP\n");
		break;
	case ERROR_CODE_RX_OVP_SUSTAIN:
		mca_log_info("sc96281 rx error OVP_SUSTAIN\n");
		break;
	case ERROR_CODE_RX_OCP_ADC:
		mca_log_info("sc96281 rx error OCP_ADC\n");
		break;
	case ERROR_CODE_RX_OCP_HARD:
		mca_log_info("sc96281 rx error OCP_HARD\n");
		break;
	case ERROR_CODE_RX_SCP:
		mca_log_info("sc96281 rx error SCP\n");
		break;
	case ERROR_CODE_RX_OTP_HARD:
		mca_log_info("sc96281 rx error OTP_HARD\n");
		break;
	case ERROR_CODE_RX_OTP_110:
		mca_log_info("sc96281 rx error OTP_110\n");
		break;
	case ERROR_CODE_RX_NGATE_OVP:
		mca_log_info("sc96281 rx error NGATE_OVP\n");
		break;
	case ERROR_CODE_RX_LDO_OPP:
		mca_log_info("sc96281 rx error LDO_OPP\n");
		break;
	case ERROR_CODE_RX_SLEEP:
		mca_log_info("sc96281 rx error SLEEP\n");
		break;
	case ERROR_CODE_RX_HOP1:
		mca_log_info("sc96281 rx error HOP1\n");
		break;
	case ERROR_CODE_RX_HOP2:
		mca_log_info("sc96281 rx error HOP2\n");
		break;
	case ERROR_CODE_RX_HOP3:
		mca_log_info("sc96281 rx error HOP3\n");
		break;
	case ERROR_CODE_RX_VRECT_OVP:
		mca_log_info("sc96281 rx error Vrect OVP\n");
		break;
	default:
		mca_log_info("sc96281 rx error code unknow\n");
		break;
	}

	*err_code = (uint8_t)code;
	return ret;
}

static int sc96281_get_tx_err_code(uint8_t *err_code, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t code = 0;

	ret = read_cust(chip, cust_tx.err_code, &code);
	if (ret) {
		mca_log_err("sc96281 tx get error code fail\n");
		return ret;
	}

	switch (code) {
	case ERROR_CODE_TX_PING_OVP:
		mca_log_info("sc96281 tx error PING_OVP\n");
		break;
	case ERROR_CODE_TX_PING_OCP:
		mca_log_info("sc96281 tx error PING_OCP\n");
		break;
	case ERROR_CODE_TX_OVP:
		mca_log_info("sc96281 tx error OVP\n");
		break;
	case ERROR_CODE_TX_OCP:
		mca_log_info("sc96281 tx error OCP\n");
		break;
	case ERROR_CODE_TX_BRIDGE_OCP:
		mca_log_info("sc96281 tx error BRIDGE_OCP\n");
		break;
	case ERROR_CODE_TX_CLAMP_OVP:
		mca_log_info("sc96281 tx error CLAMP_OVP\n");
		break;
	case ERROR_CODE_TX_LVP:
		mca_log_info("sc96281 tx error LVP\n");
		break;
	case ERROR_CODE_TX_OTP:
		mca_log_info("sc96281 tx error OTP\n");
		break;
	case ERROR_CODE_TX_OTP_HARD:
		mca_log_info("sc96281 tx error OTP_HARD\n");
		break;
	case ERROR_CODE_TX_PRE_FOD:
		mca_log_info("sc96281 tx error PRE_FOD\n");
		break;
	case ERROR_CODE_TX_FOD:
		mca_log_info("sc96281 tx error FOD\n");
		break;
	case ERROR_CODE_TX_CE_TIMEOUT:
		mca_log_info("sc96281 tx error CE_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_RP_TIMEOUT:
		mca_log_info("sc96281 tx error RP_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_NOT_SS:
		mca_log_info("sc96281 tx error NOT_SS\n");
		break;
	case ERROR_CODE_TX_NOT_ID:
		mca_log_info("sc96281 tx error NOT_ID\n");
		break;
	case ERROR_CODE_TX_NOT_XID:
		mca_log_info("sc96281 tx error NOT_XID\n");
		break;
	case ERROR_CODE_TX_NOT_CFG:
		mca_log_info("sc96281 tx error NOT_CFG\n");
		break;
	case ERROR_CODE_TX_SS_TIMEOUT:
		mca_log_info("sc96281 tx error SS_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_ID_TIMEOUT:
		mca_log_info("sc96281 tx error ID_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_XID_TIMEOUT:
		mca_log_info("sc96281 tx error XID_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_CFG_TIMEOUT:
		mca_log_info("sc96281 tx error CFG_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_NEG_TIMEOUT:
		mca_log_info("sc96281 tx error NEG_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_CAL_TIMEOUT:
		mca_log_info("sc96281 tx error CAL_TIMEOUT\n");
		break;
	case ERROR_CODE_TX_CFG_COUNT:
		mca_log_info("sc96281 tx error CFG_COUNT\n");
		break;
	case ERROR_CODE_TX_PCH_VALUE:
		mca_log_info("sc96281 tx error PCH_VALUE\n");
		break;
	case ERROR_CODE_TX_EPT_PKT:
		mca_log_info("sc96281 tx error EPT_PKT\n");
		break;
	case ERROR_CODE_TX_ILLEGAL_PKT:
		mca_log_info("sc96281 tx error ILLEGAL_PKT\n");
		break;
	case ERROR_CODE_TX_AC_DET:
		mca_log_info("sc96281 tx error AC_DET\n");
		break;
	case ERROR_CODE_TX_CHG_FULL:
		mca_log_info("sc96281 tx error CHG_FULL\n");
		break;
	case ERROR_CODE_TX_SS_ID:
		mca_log_info("sc96281 tx error SS_ID\n");
		break;
	case ERROR_CODE_TX_AP_CMD:
		mca_log_info("sc96281 tx error AP_CMD\n");
		break;
	default:
		mca_log_info("sc96281 tx error code unknow\n");
		break;
	}

	*err_code = (uint8_t)code;
	return ret;
}

static int sc96281_get_project_vendor(int *project_vendor, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	*project_vendor = chip->project_vendor;
	mca_log_info("project_vendor is %d\n", *project_vendor);

	return 0;
}

static int sc96281_get_firmware_version(char *buf, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	scnprintf(buf, 10, "%02x.%02x.%02x", chip->wls_fw_data->fw_boot_id,
		  chip->wls_fw_data->fw_rx_id, chip->wls_fw_data->fw_tx_id);

	mca_log_info("rx firmware version: boot = 0x%x, rx = 0x%x, tx = 0x%x\n",
		     chip->wls_fw_data->fw_boot_id, chip->wls_fw_data->fw_rx_id,
		     chip->wls_fw_data->fw_tx_id);

	return 0;
}

static int sc96281_ops_check_i2c(void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;

	ret = sc96281_check_i2c(chip);

	return ret;
}

static int sc96281_enable_reverse_fod(bool enable, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	return sc96281_tx_fod_enable(chip, enable);
}

static int sc96281_send_tx_q_value(u8 value, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	int ret = 0;
	u8 send_value[4] = { 0x01, 0x28, 0x62, 0 };

	if (!chip->proc_data.power_good_flag)
		return -1;

	send_value[3] = value;

	ret = sc96281_send_transparent_data(send_value, ARRAY_SIZE(send_value),
					    chip);
	mca_log_info("{0x%02x, 0x%02x, 0x%02x, 0x%02x}\n", send_value[0],
		     send_value[1], send_value[2], send_value[3]);
	return ret;
}

static int sc96281_get_fw_version_check(uint8_t *check_result, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t fw_check = 0, fw_ver = 0, image_ver = 0;

	ret = read_cust(chip, cust_rx.firmware_check, &fw_check);
	ret |= sc96281_get_fwver(chip, &fw_ver);
	if (ret) {
		mca_log_err("sc96281 get fw check failed\n");
		return ret;
	}

	chip->wls_fw_data->fw_boot_id = fw_ver >> 16;
	chip->wls_fw_data->fw_rx_id = fw_ver >> 8;
	chip->wls_fw_data->fw_tx_id = fw_ver;

	ret = sc96281_get_image_fwver(chip->fw_data_ptr, chip->fw_data_size,
				      &image_ver);

	if (fw_check == fw_ver && fw_ver >= image_ver)
		*check_result = BOOT_CHECK_SUCCESS | RX_CHECK_SUCCESS |
				TX_CHECK_SUCCESS;
	mca_log_info(
		"sc96281 get fw check success, ver:0x%x, check:0x%x, res:0x%x\n",
		fw_ver, fw_check, *check_result);

	return ret;
}

static int sc96281_check_firmware_state(bool *update, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint32_t fw_ver = 0, image_ver = 0;

	if (!chip->proc_data.power_good_flag && sc96281_check_i2c(chip)) {
		ret = -1;
		goto err;
	}

	ret = sc96281_get_fwver(chip, &fw_ver);
	if (ret)
		goto err;

	ret = sc96281_get_image_fwver(chip->fw_data_ptr, chip->fw_data_size,
				      &image_ver);
	if (ret < 0)
		goto err;

	chip->wls_fw_data->fw_boot_id = fw_ver >> 16;
	chip->wls_fw_data->fw_rx_id = fw_ver >> 8;
	chip->wls_fw_data->fw_tx_id = fw_ver;

	mca_log_info("ic fw version: %02x.%02x.%02x, img version: 0x%x\n",
		     chip->wls_fw_data->fw_boot_id, chip->wls_fw_data->fw_rx_id,
		     chip->wls_fw_data->fw_tx_id, image_ver);

	if (fw_ver == 0 || fw_ver == ~0UL || fw_ver < image_ver) {
		mca_log_info("need update\n");
		*update = true;
	} else {
		mca_log_info("no need update\n");
		*update = false;
	}

	return ret;

err:
	mca_log_err("i2c error!\n");
	*update = false;
	return ret;
}

static int sc96281_download_fw(void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	bool need_update = true;

	//ret = sc96281_check_i2c(chip);
	//if (ret)
	//    return ret;

	ret = sc96281_check_firmware_state(&need_update, chip);
	if (ret)
		return ret;

	if (need_update) {
		sc96281_rx_set_cmd(chip, AP_CMD_WAIT_FOR_UPDATE);
		ret = mtp_program(chip, false);
		if (ret)
			mca_log_err("download firmware fail\n");
		else
			mca_log_info("download firmware success\n");
	}

	return ret;
}

static int sc96281_set_fw_bin(const char *buf, int count, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	static uint16_t total_length;
	static uint8_t serial_number;
	static uint8_t fw_area;

	mca_log_info("buf:%s, count:%d\n", buf, count);
	if (strncmp("length:", buf, 7) == 0) {
		if (kstrtou16(buf + 7, 10, &total_length))
			return -EINVAL;
		chip->fw_bin_length = total_length;
		serial_number = 0;
		mca_log_info("total_length:%d, serial_number:%d\n",
			     total_length, serial_number);
	} else if (strncmp("area:", buf, 5) == 0) {
		if (kstrtou8(buf + 5, 10, &fw_area))
			return -EINVAL;
		mca_log_info("area:%d\n", fw_area);
	} else {
		memcpy((chip->fw_bin + serial_number * count), buf, count);
		serial_number++;
		mca_log_info("serial_number:%d, count:%d\n", serial_number,
			     count);
	}
	return count;
}

static int sc96281_download_fw_from_bin(void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;

	ret = sc96281_check_i2c(chip);
	if (ret)
		return ret;

	sc96281_rx_set_cmd(chip, AP_CMD_WAIT_FOR_UPDATE);
	ret = mtp_program(chip, true);
	if (ret)
		mca_log_err("download firmware from bin fail\n");
	else
		mca_log_info("download firmware from bin success\n");

	chip->fw_bin_length = 0;

	return ret;
}

static int sc96281_send_tx_fan_speed(int value, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	ask_pkt_t ask;

	if (!chip->proc_data.power_good_flag)
		return -1;

	if (value < 0 || value > 10)
		return -1;

	if (chip->proc_data.tx_speed == value)
		return ret;

	chip->proc_data.tx_speed = value;
	ask.header = 0x38;
	ask.msg[0] = PRIVATE_CMD_CTRL_PRI_PKT;
	ask.msg[1] = value;
	ask.msg[2] = 0;

	mutex_lock(&chip->data_transfer_lock);

	ret = sc96281_rx_send_ask_pkt(chip, &ask, false);
	if (ret) {
		mca_log_err("sc96281 set fan speed %d failed\n", value);
	} else {
		mca_log_info("sc96281 set fan speed %d success\n", value);
	}

	mutex_unlock(&chip->data_transfer_lock);

	return ret;
}

static int sc96281_get_tx_fan_speed(int *value, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	*value = (int)chip->proc_data.tx_speed;
	mca_log_info("tx_fan_speed is %d\n", *value);

	return 0;
}

static int sc96281_set_rx_offset(int rx_offset, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	chip->fake_rx_offset = rx_offset;

	return 0;
}

static int sc96281_get_rx_offset(int *rx_offset, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	*rx_offset = RX_OFFSET_GOOD;
	if (!chip->proc_data.power_good_flag)
		return -1;

	if (chip->fake_rx_offset != RX_OFFSET_GOOD)
		*rx_offset = RX_OFFSET_BAD;

	if (chip->proc_data.ss_voltage > 0 &&
	    chip->proc_data.ss_voltage < RX_OFFSET_THRESHOLD)
		*rx_offset = RX_OFFSET_BAD;

	mca_log_info("rx_offset = %d, ss_voltage = %d", *rx_offset,
		     chip->proc_data.ss_voltage);

	return 0;
}

static int sc96281_set_rx_sleep_mode(int sleep_for_dam, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	int ret = 0;

	if (sleep_for_dam) {
		ret = sc96281_rx_set_cust_cmd(chip, CUST_CMD_ULPM);
		if (ret < 0)
			return -1;
	}
	return ret;
}

static int sc96281_rcv_product_test_cmd(uint8_t *rev_data, int *length,
					void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	int ret = 0;
	uint8_t i;
	fsk_pkt_t fsk;

	if (!chip->proc_data.power_good_flag)
		return ret;

	ret = sc96281_rx_recv_fsk_pkt(chip, &fsk);
	if (ret)
		return ret;

	mca_log_info("receive_transparent_data fsk=%X %X %X %X %X %X %X %X\n",
		     fsk.buff[0], fsk.buff[1], fsk.buff[2], fsk.buff[3],
		     fsk.buff[4], fsk.buff[5], fsk.buff[6], fsk.buff[7]);

	if (fsk.header == 0 || fsk.header == 0x33 || fsk.header == 0x55 ||
	    fsk.header == 0xFF)
		*length = 1;
	else if (fsk.header < 0x20)
		*length = 3;
	else if (fsk.header < 0x80)
		*length = 4 + ((fsk.header - 0x20) >> 4);
	else if (fsk.header < 0xe0)
		*length = 10 + ((fsk.header - 0x80) >> 3);
	else
		*length = 22 + ((fsk.header - 0xe0) >> 2);

	mca_log_info("data_length=%d\n", *length);

	for (i = 0; i < *length; i++) {
		rev_data[i] = fsk.buff[i];
		mca_log_info("receive_product_test_cmd i=%d, data[i]=0x%x\n", i,
			     rev_data[i]);
	}

	return ret;
}

static int sc96281_process_factory_cmd(uint8_t cmd, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;
	int ret = 0;
	uint8_t send_data[8];
	uint8_t data_h, data_l;
	int rx_iout, rx_vout;
	uint8_t index = 0;

	switch (cmd) {
	case FACTORY_TEST_CMD_RX_IOUT:
		ret = sc96281_get_iout(&rx_iout, chip);
		data_h = ((uint32_t)rx_iout & 0x00ff);
		data_l = ((uint32_t)rx_iout & 0xff00) >> 8;
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_3BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_IOUT;
		send_data[index++] = data_h;
		send_data[index++] = data_l;
		mca_log_err(
			"sc96281_factory_test --rx_iout--0x%x,0x%x iout=%d\n",
			data_h, data_l, rx_iout);
		break;
	case FACTORY_TEST_CMD_RX_VOUT:
		ret = sc96281_get_vout(&rx_vout, chip);
		data_h = ((uint32_t)rx_vout & 0x00ff);
		data_l = ((uint32_t)rx_vout & 0xff00) >> 8;
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_3BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_VOUT;
		send_data[index++] = data_h;
		send_data[index++] = data_l;
		mca_log_err(
			"sc96281_factory_test --rx_vout--0x%x,0x%x vout=%d\n",
			data_h, data_l, rx_vout);
		break;
	case FACTORY_TEST_CMD_RX_FW_ID:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_5BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_FW_ID;
		send_data[index++] = 0x0;
		send_data[index++] = 0x0;
		send_data[index++] = chip->wls_fw_data->fw_rx_id;
		send_data[index++] = chip->wls_fw_data->fw_tx_id;
		mca_log_err("sc96281_factory_test --fw_version--0x%x 0x%x\n",
			    chip->wls_fw_data->fw_rx_id,
			    chip->wls_fw_data->fw_tx_id);
		break;
	case FACTORY_TEST_CMD_RX_CHIP_ID:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_3BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_CHIP_ID;
		send_data[index++] = 16;
		send_data[index++] = 51;
		mca_log_err("sc96281_factory_test --rx_chip_id--0x%x0x%x\n",
			    chip->wls_fw_data->hw_id_h,
			    chip->wls_fw_data->hw_id_l);
		break;
	case FACTORY_TEST_CMD_ADAPTER_TYPE:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_2BYTE;
		send_data[index++] = FACTORY_TEST_CMD_ADAPTER_TYPE;
		send_data[index++] = chip->proc_data.adapter_type;
		mca_log_err("sc96281_factory_test --usb_type--%d\n",
			    chip->proc_data.adapter_type);
		break;
	case FACTORY_TEST_CMD_REVERSE_REQ:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_1BYTE;
		send_data[index++] = FACTORY_TEST_CMD_REVERSE_REQ;
		mca_log_err("[ factory reverse test] receive request\n");
		break;
	default:
		return -1;
	}
	ret = sc96281_send_transparent_data(send_data, index, chip);
	return ret;
}

static int sc96281_get_hall_gpio_status(bool *status, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	if (chip->hall_gpio_status)
		*status = true;
	else
		*status = false;

	return 0;
}

static int sc96281_get_phone_case_category(int *category, void *data)
{
    struct sc96281 *chip = (struct sc96281 *)data;

    *category = chip->phone_case_category;
    mca_log_info("get phone_case_category: %d\n", *category);
    return 0;
}

static int sc96281_set_phone_case_category(int category, void *data)
{
    struct sc96281 *chip = (struct sc96281 *)data;

    chip->phone_case_category = category;
    mca_log_info("set phone_case_category: %d\n", category);
    return 0;
}

static int sc96281_enable_vsys_ctrl(bool enable, void *data)
{
	struct sc96281 *chip = (struct sc96281 *)data;

	return sc96281_vsys_ctrl(chip, enable);
}

static int sc96281_get_trx_isense(int *iout, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint16_t cur = 0;

	ret = sc96281_rx_get_iout(chip, &cur);
	if (!ret)
		*iout = (int)cur;
	mca_log_info("trx isense: %d\n", *iout);
	return ret;
}

static int sc96281_get_trx_vrect(int *vrect, void *data)
{
	int ret = 0;
	struct sc96281 *chip = (struct sc96281 *)data;
	uint16_t rect = 0;

	ret = sc96281_rx_get_vrect(chip, &rect);
	if (!ret)
		*vrect = (int)rect;
	mca_log_info("trx vrect: %d\n", *vrect);
	return ret;
}

static int sc96281_switch_bridge(bool full, void *data)
{
	struct sc96281 *sc = data;
	int ret;

	ret = sc96281_rx_set_cust_cmd(sc, full ? 0x200000 : 0x100000);
	mca_log_info("bridge_sw switch to %s, ret %d\n",
		     full ? "FULL_BRIDGE" : "HALF_BRIDGE", ret);
	return ret;
}

static int sc96281_get_brg_rect_mode(u8 *mode, void *data)
{
	struct sc96281 *sc = data;
	u8 val = 0;
	int ret;

	ret = sc96281_read_block(sc, 0x18d, &val, 1);
	if (ret) {
		mca_log_err("bridge_sw get brg_rect_mode fail\n");
		return ret;
	}
	mca_log_err("bridge_sw get brg_rect_mode %d\n", val);
	*mode = val;
	return ret;
}

static int sc96281_get_magnetic_case_flag(bool *flag, void *data)
{
	struct sc96281 *sc = data;

	*flag = sc->magnetic_case_flag;
	return 0;
}

static struct platform_class_wireless_ops sc96281_wls_ops = {
	.wls_enable_reverse_chg = sc96281_enable_reverse_chg,
	.wls_is_present = sc96281_is_present,
	.wls_set_vout = sc96281_set_vout,
	.wls_get_vout = sc96281_get_vout,
	.wls_get_iout = sc96281_get_iout,
	.wls_get_vrect = sc96281_get_vrect,
	.wls_get_temp = sc96281_get_temp,
	.wls_get_tx_adapter = sc96281_get_tx_adapter,
	.wls_get_tx_adapter_by_i2c = sc96281_get_tx_adapter_by_i2c,
	.wls_switch_bridge = sc96281_switch_bridge,
	.wls_get_brg_rect_mode = sc96281_get_brg_rect_mode,
	.wls_get_magnetic_case_flag = sc96281_get_magnetic_case_flag,
	.wls_get_fw_upgrade_fail_info = sc96281_get_fw_upgrade_fail_info,
	.wls_get_rsv_eppmode_fail = sc96281_rx_get_rsv_eppmode_fail,
	.wls_set_enable_mode = sc96281_set_enable_mode,
	.wls_is_car_adapter = sc96281_is_car_adapter,
	.wls_get_rx_rtx_mode = sc96281_get_rx_rtx_mode,
	.wls_set_input_current_limit = sc96281_set_input_current_limit,
	.wls_get_rx_int_flag = sc96281_get_rx_int_flag,
	.wls_get_rx_power_mode = sc96281_get_rx_power_mode,
	.wls_get_auth_value = sc96281_get_auth_value,
	.wls_set_adapter_voltage = sc96281_set_adapter_voltage,
	.wls_get_tx_uuid = sc96281_get_tx_uuid,
	.wls_get_rx_fastcharge_status = sc96281_get_rx_fastcharge_status,
	.wls_receive_transparent_data = sc96281_receive_transparent_data,
	.wls_send_transparent_data = sc96281_send_transparent_data,
	.wls_get_ss_voltage = sc96281_get_ss_voltage,
	.wls_do_renego = sc96281_do_renego,
	.wls_set_parallel_charge = sc96281_set_parallel_charge,
	.wls_get_vout_setted = sc96281_get_vout_setted,
	.wls_get_poweroff_err_code = sc96281_get_poweroff_err_code,
	.wls_get_rx_err_code = sc96281_get_rx_err_code,
	.wls_get_tx_err_code = sc96281_get_tx_err_code,
	.wls_get_project_vendor = sc96281_get_project_vendor,
	.wls_check_i2c_is_ok = sc96281_ops_check_i2c,
	.wls_enable_rev_fod = sc96281_enable_reverse_fod,
	.wls_send_tx_q_value = sc96281_send_tx_q_value,
	.wls_get_fw_version = sc96281_get_firmware_version,
	.wls_get_fw_version_check = sc96281_get_fw_version_check,
	.wls_check_firmware_state = sc96281_check_firmware_state,
	.wls_download_fw = sc96281_download_fw,
	.wls_set_fw_bin = sc96281_set_fw_bin,
	.wls_erase_fw = NULL, //nuvolta_1652_erase_fw_data,
	.wls_download_fw_from_bin = sc96281_download_fw_from_bin,
	.wls_set_confirm_data = NULL, //nuvolta_1652_set_confirm_data,
	.wls_set_fod_params = sc96281_set_fod_params,
	.wls_set_debug_fod_params = sc96281_set_debug_fod_params,
	.wls_set_debug_fod = sc96281_set_debug_fod,
	.wls_get_debug_fod_type = sc96281_get_debug_fod_type,
	.wls_set_tx_fan_speed = sc96281_send_tx_fan_speed,
	.wls_get_tx_fan_speed = sc96281_get_tx_fan_speed,
	.wls_get_rx_offset = sc96281_get_rx_offset,
	.wls_set_rx_offset = sc96281_set_rx_offset,
	.wls_set_rx_sleep_mode = sc96281_set_rx_sleep_mode,
	.wls_receive_test_cmd = sc96281_rcv_product_test_cmd,
	.wls_process_factory_cmd = sc96281_process_factory_cmd,
	.wls_get_hall_gpio_status = sc96281_get_hall_gpio_status,
	.wls_enable_vsys_ctrl = sc96281_enable_vsys_ctrl,
	.wls_get_trx_isense = sc96281_get_trx_isense,
	.wls_get_trx_vrect = sc96281_get_trx_vrect,
    .wls_get_phone_case_category = sc96281_get_phone_case_category,
    .wls_set_phone_case_category = sc96281_set_phone_case_category,
};

//-------------------------irq & work---------------------------
static int_map_t rx_irq_map[] = {
	DECL_INTERRUPT_MAP(WP_IRQ_RX_LDO_ON, RX_INT_LDO_ON),
	DECL_INTERRUPT_MAP(WP_IRQ_RX_FAST_CHARGE_SUCCESS, RX_INT_FAST_CHARGE),
	DECL_INTERRUPT_MAP(WP_IRQ_RX_AUTH, RX_INT_AUTHEN_FINISH),
	DECL_INTERRUPT_MAP(WP_IRQ_RX_RENEG_SUCCESS, RX_INT_RENEGO_DONE),
	DECL_INTERRUPT_MAP(WP_IRQ_RX_RENEG_FAIL, RX_INT_RENEGO_FAIL),
	//DECL_INTERRUPT_MAP(xx, RX_INT_ALARM_SUCCESS),
	//DECL_INTERRUPT_MAP(xx, RX_INT_ALARM_FAIL),
	//DECL_INTERRUPT_MAP(xx, RX_INT_OOB_GOOD),
	//DECL_INTERRUPT_MAP(xx, RX_INT_RPP),
	DECL_INTERRUPT_MAP(WP_IRQ_PKT_RECV, RX_INT_TRANSPARENT_SUCCESS),
	DECL_INTERRUPT_MAP(WP_IRQ_PPP_TIMEOUT, RX_INT_TRANSPARENT_FAIL),
	DECL_INTERRUPT_MAP(WP_IRQ_RX_FACTORY_TEST, RX_INT_FACTORY_TEST),
	DECL_INTERRUPT_MAP(WP_IRQ_OTP, RX_INT_OCP_OTP_ALARM),
	DECL_INTERRUPT_MAP(WP_IRQ_OTP_110, RX_INT_OCP_OTP_ALARM),
	//DECL_INTERRUPT_MAP(xx, RX_INT_FIRST_AUTHEN),
	//DECL_INTERRUPT_MAP(xx, RX_INT_POWER_OFF),
	DECL_INTERRUPT_MAP(WP_IRQ_RX_POWER_ON, RX_INT_POWER_ON),
	DECL_INTERRUPT_MAP(WP_IRQ_ERROR_CODE, RX_INT_ERR_CODE),
};

static int_map_t tx_irq_map[] = {
	DECL_INTERRUPT_MAP(WP_IRQ_TX_PING, RTX_INT_PING),
	DECL_INTERRUPT_MAP(WP_IRQ_TX_DET_RX, RTX_INT_GET_RX),
	DECL_INTERRUPT_MAP(WP_IRQ_TX_CEP_TIMEOUT, RTX_INT_CEP_TIMEOUT),
	DECL_INTERRUPT_MAP(WP_IRQ_TX_EPT, RTX_INT_EPT),
	//DECL_INTERRUPT_MAP(xx, RTX_INT_PROTECTION),
	DECL_INTERRUPT_MAP(WP_IRQ_TX_DET_TX, RTX_INT_GET_TX),
	//DECL_INTERRUPT_MAP(xx, RTX_INT_REVERSE_TEST_READY),
	//DECL_INTERRUPT_MAP(xx, RTX_INT_REVERSE_TEST_DONE),
	DECL_INTERRUPT_MAP(WP_IRQ_TX_FOD, RTX_INT_FOD),
	DECL_INTERRUPT_MAP(WP_IRQ_PKT_RECV, RTX_INT_EPT_PKT),
	DECL_INTERRUPT_MAP(WP_IRQ_ERROR_CODE, RTX_INT_ERR_CODE),
};

static int sc96281_rx_get_int_flag(uint32_t rxint)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(rx_irq_map); i++) {
		if (rx_irq_map[i].irq_regval & rxint) {
			return rx_irq_map[i].irq_flag;
		}
	}

	return RX_INT_UNKNOWN;
}

static int sc96281_tx_get_int_flag(uint32_t txint)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(tx_irq_map); i++) {
		if (tx_irq_map[i].irq_regval & txint) {
			return tx_irq_map[i].irq_flag;
		}
	}

	return RTX_INT_UNKNOWN;
}

static void sc96281_init_detect_work(struct work_struct *work)
{
	struct sc96281 *chip =
		container_of(work, struct sc96281, init_detect_work.work);
	int ret = 0;

	if (gpio_is_valid(chip->power_good_gpio)) {
		ret = gpio_get_value(chip->power_good_gpio);
		mca_log_info("init power good: %d\n", ret);
		if (ret) {
			sc96281_set_enable_mode(false, chip);
			usleep_range(125000, 150000);
			sc96281_set_enable_mode(true, chip);
		}
	}
}

static void sc96281_pg_det_work(struct work_struct *work)
{
	struct sc96281 *chip =
		container_of(work, struct sc96281, pg_detect_work.work);
	int ret = 0;

	if (gpio_is_valid(chip->power_good_gpio)) {
		ret = gpio_get_value(chip->power_good_gpio);
		if (ret) {
			if (chip->proc_data.power_good_flag) {
				mca_log_info("wireless is already attached\n");
				return;
			}
			mca_log_info("power_good high, wireless attached\n");
			chip->proc_data.power_good_flag = true;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
					       MCA_EVENT_WIRELESS_CONNECT,
					       NULL);
		} else {
			mca_log_info("power_good low, wireless detached\n");
			chip->proc_data.power_good_flag = false;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
					       MCA_EVENT_WIRELESS_DISCONNECT,
					       NULL);
			memset(&chip->proc_data, 0, sizeof(chip->proc_data));
		}
	}
}

static irqreturn_t sc96281_power_good_handler(int irq, void *dev_id)
{
	struct sc96281 *chip = dev_id;

	mca_log_info("power_good detected\n");

	schedule_delayed_work(&chip->pg_detect_work, msecs_to_jiffies(0));

	return IRQ_HANDLED;
}

static void sc96281_interrupt_work(struct work_struct *work)
{
	struct sc96281 *chip =
		container_of(work, struct sc96281, interrupt_work.work);
	int ret = 0;
	uint32_t regval;

	mutex_lock(&chip->wireless_chg_int_lock);

	ret = sc96281_get_rx_rtx_mode(&chip->proc_data.int_trx_mode, chip);
	if (ret < 0) {
		mca_log_err("get rtx mode fail\n");
		goto exit;
	}
	mca_log_info("trx_mode = %d\n", chip->proc_data.int_trx_mode);

	if (chip->proc_data.int_trx_mode == RTX_MODE) {
		ret = sc96281_tx_get_int(chip, &regval);
		if (ret < 0) {
			mca_log_err("get tx int flag fail\n");
			goto exit;
		}
		sc96281_tx_clr_int(chip, regval);
		chip->proc_data.int_flag = sc96281_tx_get_int_flag(regval);
		mca_log_info("get tx int 0x%x, flag %d \n", regval,
			     chip->proc_data.int_flag);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS,
					  MCA_EVENT_WIRELESS_INT_CHANGE,
					  chip->proc_data.int_flag);
	} else {
		ret = sc96281_rx_get_int(chip, &regval);
		if (ret < 0) {
			mca_log_err("get rx int flag fail\n");
			goto exit;
		}
		sc96281_rx_clr_int(chip, regval);
		chip->proc_data.int_flag = sc96281_rx_get_int_flag(regval);
		mca_log_info("get rx int 0x%x, flag %d \n", regval,
			     chip->proc_data.int_flag);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
					  MCA_EVENT_WIRELESS_INT_CHANGE,
					  chip->proc_data.int_flag);
	}
exit:
	chip->proc_data.int_flag = 0;
	mutex_unlock(&chip->wireless_chg_int_lock);
	mca_log_info("already clear int and unlock mutex\n");
}

static irqreturn_t sc96281_interrupt_handler(int irq, void *dev_id)
{
	struct sc96281 *chip = dev_id;
	schedule_delayed_work(&chip->interrupt_work, msecs_to_jiffies(0));
	return IRQ_HANDLED;
}

static void sc96281_hall_interrupt_work(struct work_struct *work)
{
	struct sc96281 *chip =
		container_of(work, struct sc96281, hall_interrupt_work.work);
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;
	int ret = 0;
	int len;

	if (gpio_is_valid(chip->hall_int_gpio)) {
		ret = gpio_get_value(chip->hall_int_gpio);
		if (!ret) {
			if (chip->magnetic_case_flag) {
				mca_log_info(
					"magnetic_case_flag is already attached\n");
				return;
			}
			mca_log_info(
				"hall_interrupt low, magnetic_case attached\n");
			chip->hall_gpio_status = false;
			chip->magnetic_case_flag = true;
		} else {
			mca_log_info(
				"hall_interrupt high, magnetic_case detached\n");
			chip->magnetic_case_flag = false;
			chip->hall_gpio_status = true;
		}

		mca_log_info("report hall!!\n");
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
					  MCA_EVENT_WIRELESS_MAGNETIC_CASE_INT,
					  chip->magnetic_case_flag);

		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			       "POWER_SUPPLY_WLS_HALL=%d",
			       chip->magnetic_case_flag);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}
}

static irqreturn_t sc96281_hall_interrupt_handler(int irq, void *dev_id)
{
	struct sc96281 *chip = dev_id;

	mca_log_info("hall_int detected\n");

	schedule_delayed_work(&chip->hall_interrupt_work, msecs_to_jiffies(0));

	return IRQ_HANDLED;
}

static int sc96281_parse_params(struct device_node *node, const char *name,
				struct fod_type_t *params)
{
	int i, j;
	int len;
	u8 *idata = NULL;

	if (strcmp(name, "null") == 0) {
		mca_log_info("no need parse params\n");
		return -1;
	}

	len = mca_parse_dts_u8_count(node, name, DEFAULT_FOD_PARAM_LEN,
				     PARAMS_T_MAX);
	if (len < 0) {
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	idata = kcalloc(len, sizeof(u8), GFP_KERNEL);

	mca_parse_dts_u8_array(node, name, idata, len);
	for (i = 0; i < len / 2; i++) {
		j = 2 * i;
		params[i].gain = idata[j];
		params[i].offset = idata[j + 1];
		mca_log_debug("[%d]params: gain:%d, offset:%d\n", i,
			      params[i].gain, params[i].offset);
	}

	kfree(idata);
	idata = NULL;
	return 0;
}

static int sc96281_parse_fod_params(struct device_node *node,
				    struct sc96281 *info)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(
		node, "fod_params", FOD_PARA_MAX_GROUP, FOD_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse fod_params failed\n");
		return -1;
	}

	info->fod_params_size = array_len / FOD_PARA_MAX;

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "fod_params", i,
					       &tmp_string))
			return -1;

		row = i / FOD_PARA_MAX;
		col = i % FOD_PARA_MAX;
		switch (col) {
		case FOD_PARA_TYPE:
			if (kstrtou8(tmp_string, 10,
				     &info->fod_params[row].type))
				return -1;
			break;
		case FOD_PARA_LENGTH:
			if (kstrtou8(tmp_string, 10,
				     &info->fod_params[row].length))
				return -1;
			break;
		case FOD_PARA_UUID:
			if (kstrtoint(tmp_string, 16,
				      &info->fod_params[row].uuid))
				return -1;
			break;
		case FOD_PARA_PARAMS:
			if (sc96281_parse_params(node, tmp_string,
						 info->fod_params[row].params))
				return -1;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int sc96281_parse_fod_params_default(struct device_node *node,
					    struct sc96281 *info)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(
		node, "fod_params_default", FOD_PARA_MAX_GROUP, FOD_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse fod_params_default failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "fod_params_default", i,
					       &tmp_string))
			return -1;

		row = i / FOD_PARA_MAX;
		col = i % FOD_PARA_MAX;
		mca_log_debug("[%d]fod params default %s\n", i, tmp_string);
		switch (col) {
		case FOD_PARA_TYPE:
			if (kstrtou8(tmp_string, 10,
				     &info->fod_params_default.type))
				return -1;
			break;
		case FOD_PARA_LENGTH:
			if (kstrtou8(tmp_string, 10,
				     &info->fod_params_default.length))
				return -1;
			break;
		case FOD_PARA_UUID:
			if (kstrtoint(tmp_string, 16,
				      &info->fod_params_default.uuid))
				return -1;
			break;
		case FOD_PARA_PARAMS:
			if (sc96281_parse_params(
				    node, tmp_string,
				    info->fod_params_default.params))
				return -1;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int sc96281_parse_fw_fod_data(struct sc96281 *chip)
{
	int ret = 0;
	struct device_node *node =
		of_find_node_by_name(NULL, "mca_sc96281_fod_data");

	switch (chip->project_vendor) {
	case WLS_CHIP_VENDOR_SC96281:
		chip->fw_data_ptr = SC96281_FIRMWARE[chip->fw_version_index];
		chip->fw_data_size =
			sizeof(SC96281_FIRMWARE[chip->fw_version_index]);
		break;
	default:
		chip->fw_data_ptr = SC96281_FIRMWARE[chip->fw_version_index];
		chip->fw_data_size =
			sizeof(SC96281_FIRMWARE[chip->fw_version_index]);
		break;
	}

	ret = sc96281_parse_fod_params(node, chip);
	ret = sc96281_parse_fod_params_default(node, chip);

	mca_log_info("cur fw_data is:[0]:%02x; [1]:%02x; [2]:%02x\n",
		     chip->fw_data_ptr[0], chip->fw_data_ptr[1],
		     chip->fw_data_ptr[2]);
	return ret;
}

static int sc96281_parse_dt(struct sc96281 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int ret = 0;

	if (!node) {
		mca_log_err("No DT data Failing Probe\n");
		return -EINVAL;
	}

	(void)mca_parse_dts_u32(node, "project_vendor", &chip->project_vendor,
				0);
	(void)mca_parse_dts_u32(node, "support-hall", &chip->support_hall, 0);
	(void)mca_parse_dts_u32(node, "rx_role", &chip->rx_role, 0);
	(void)mca_parse_dts_u32(node, "fw_version_index",
				&chip->fw_version_index, 0);

	mca_log_err(
		"project_vendor=%d, support_hall=%d, rx_role=%d, fw_version_index=%d",
		chip->project_vendor, chip->support_hall, chip->rx_role,
		chip->fw_version_index);

	sc96281_parse_fw_fod_data(chip);

	chip->sc96281_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->sc96281_pinctrl)) {
		mca_log_err("failed to get sc96281_pinctrl\n");
		return -1;
	}

	chip->pinctrl_stat =
		pinctrl_lookup_state(chip->sc96281_pinctrl, "rx_int_pull_up");
	if (IS_ERR_OR_NULL(chip->pinctrl_stat)) {
		mca_log_err("failed to parse pinctrl_stat\n");
		return -1;
	}

	ret = pinctrl_select_state(chip->sc96281_pinctrl, chip->pinctrl_stat);
	if (ret) {
		mca_log_err("failed to select pinctrl_stat\n");
		return -1;
	}

	if (chip->support_hall) {
		chip->pinctrl_stat_hall = pinctrl_lookup_state(
			chip->sc96281_pinctrl, "hall_int_pull_up");
		if (IS_ERR_OR_NULL(chip->pinctrl_stat_hall)) {
			mca_log_err("failed to parse pinctrl_stat_hall\n");
			return -1;
		}

		ret = pinctrl_select_state(chip->sc96281_pinctrl,
					   chip->pinctrl_stat_hall);
		if (ret) {
			mca_log_err("failed to select pinctrl_stat_hall\n");
			return -1;
		}
	}

	chip->enable_gpio = of_get_named_gpio(node, "sleep-rx-gpio", 0);
	if ((!gpio_is_valid(chip->enable_gpio))) {
		mca_log_err("fail_enable_gpio %d\n", chip->enable_gpio);
		return -EINVAL;
	}
	ret = gpio_request(chip->enable_gpio, "rx-enable-gpio");
	if (ret)
		mca_log_err("request enable gpio [%d] fail\n",
			    chip->enable_gpio);
	else {
		ret = gpio_direction_output(chip->enable_gpio, 0);
		if (ret)
			mca_log_err("set direction for enable gpio [%d] fail\n",
				    chip->enable_gpio);
	}

	chip->irq_gpio = of_get_named_gpio(node, "rx-int", 0);
	if (!gpio_is_valid(chip->irq_gpio)) {
		mca_log_err("fail_irq_gpio %d\n", chip->irq_gpio);
		return -EINVAL;
	}

	if (chip->support_hall) {
		chip->hall_int_gpio = of_get_named_gpio(node, "hall-int2", 0);
		if (!gpio_is_valid(chip->hall_int_gpio)) {
			mca_log_err("fail_hall_int_gpio %d\n",
				    chip->hall_int_gpio);
			return -EINVAL;
		}
	}

	chip->power_good_gpio = of_get_named_gpio(node, "pwr-det-int", 0);
	if (!gpio_is_valid(chip->power_good_gpio)) {
		mca_log_err("fail_power_good_gpio %d\n", chip->power_good_gpio);
		return -EINVAL;
	}

	return 0;
}

static int sc96281_irq_init(struct sc96281 *chip)
{
	int ret = 0;

	// nirq init
	if (gpio_is_valid(chip->irq_gpio)) {
		/*
        ret = gpio_request_one(chip->irq_gpio, GPIOF_DIR_IN,"sc96281_global_irq");
        if (ret) {
            mca_log_err("failed to request sc96281_global_irq\n");
            return -EINVAL;
        }
        */
		chip->irq = gpio_to_irq(chip->irq_gpio);
		if (chip->irq < 0) {
			mca_log_err("gpio_to_irq Fail!\n");
			goto fail_irq_gpio;
		}
	} else {
		mca_log_err("irq gpio not provided\n");
		goto fail_irq_gpio;
	}
	if (chip->irq) {
		ret = devm_request_threaded_irq(&chip->client->dev, chip->irq,
						NULL, sc96281_interrupt_handler,
						(IRQF_TRIGGER_FALLING |
						 IRQF_ONESHOT),
						"sc96281_chg_stat_irq", chip);
		if (ret)
			mca_log_err("Failed irq = %d ret = %d\n", chip->irq,
				    ret);
	}
	enable_irq_wake(chip->irq);

	// pg_irq init
	if (gpio_is_valid(chip->power_good_gpio)) {
		/*
        ret = gpio_request_one(chip->power_good_gpio, GPIOF_DIR_IN,"sc96281_power_good_irq");
        if (ret) {
            mca_log_err("failed to request sc96281_power_good_irq\n");
            return -EINVAL;
        }
        */
		chip->power_good_irq = gpio_to_irq(chip->power_good_gpio);
		if (chip->power_good_irq < 0) {
			mca_log_err("gpio_to_power_good Fail!\n");
			goto fail_power_good_gpio;
		}
	} else {
		mca_log_err("power good gpio not provided\n");
		goto fail_power_good_gpio;
	}
	if (chip->power_good_irq) {
		ret = devm_request_threaded_irq(
			&chip->client->dev, chip->power_good_irq, NULL,
			sc96281_power_good_handler,
			(IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
			 IRQF_ONESHOT),
			"sc96281_power_good_irq", chip);
		if (ret)
			mca_log_err("Failed irq = %d ret = %d\n",
				    chip->power_good_irq, ret);
	}
	enable_irq_wake(chip->power_good_irq);

	//hall_irq init
	if (chip->support_hall) {
		if (gpio_is_valid(chip->hall_int_gpio)) {
			chip->hall_int_irq = gpio_to_irq(chip->hall_int_gpio);
			if (chip->hall_int_gpio < 0) {
				mca_log_err("gpio_to_hall_int Fail!\n");
				goto fail_hall_int_gpio;
			}
		} else {
			mca_log_err("hall int gpio not provided\n");
			goto fail_hall_int_gpio;
		}
		if (chip->hall_int_irq) {
			ret = devm_request_threaded_irq(
				&chip->client->dev, chip->hall_int_irq, NULL,
				sc96281_hall_interrupt_handler,
				(IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				 IRQF_ONESHOT),
				"sc96281_hall_int_irq", chip);
			if (ret)
				mca_log_err("Failed hall irq = %d ret = %d\n",
					    chip->hall_int_irq, ret);
		}
		enable_irq_wake(chip->hall_int_irq);
	}

	return ret;

fail_hall_int_gpio:
	gpio_free(chip->hall_int_gpio);
fail_power_good_gpio:
	gpio_free(chip->power_good_gpio);
fail_irq_gpio:
	gpio_free(chip->irq_gpio);

	return ret;
}

static int sc96281_shutdown_cb(struct notifier_block *nb, unsigned long code,
			       void *unused)
{
	struct sc96281 *bcdev =
		container_of(nb, struct sc96281, shutdown_notifier);

	if (code == SYS_POWER_OFF || code == SYS_RESTART) {
		mca_log_info("start adsp shutdown\n");
		if (bcdev->proc_data.power_good_flag) {
			sc96281_set_enable_mode(false, bcdev);
			msleep(150);
			//TODO:set wls_iin = 0
			sc96281_set_enable_mode(true, bcdev);
		}
	}

	return NOTIFY_DONE;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 9))
static int sc96281_charger_probe(struct i2c_client *client)
#else
static int sc96281_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
#endif
{
	int ret = 0;
	struct sc96281 *chip;

	mca_log_info("sc96281 %s probe start!\n", SC96281_DRV_VERSION);

	chip = devm_kzalloc(&client->dev, sizeof(struct sc96281), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->wls_fw_data = devm_kzalloc(
		&client->dev, sizeof(*chip->wls_fw_data), GFP_KERNEL);
	if (!chip->wls_fw_data)
		return -ENOMEM;

	chip->regmap = devm_regmap_init_i2c(client, &sc96281_regmap_config);
	if (IS_ERR(chip->regmap)) {
		mca_log_err("failed to allocate register map\n");
		return PTR_ERR(chip->regmap);
	}

	chip->client = client;
	chip->dev = &client->dev;

	device_init_wakeup(&client->dev, TRUE);
	i2c_set_clientdata(client, chip);

	mutex_init(&chip->i2c_rw_lock);
	mutex_init(&chip->data_transfer_lock);
	mutex_init(&chip->wireless_chg_int_lock);

	//sc96281_create_device_node(&(client->dev));

	sc96281_parse_dt(chip);
	sc96281_irq_init(chip);

	INIT_DELAYED_WORK(&chip->interrupt_work, sc96281_interrupt_work);
	INIT_DELAYED_WORK(&chip->pg_detect_work, sc96281_pg_det_work);
	INIT_DELAYED_WORK(&chip->init_detect_work, sc96281_init_detect_work);
	INIT_DELAYED_WORK(&chip->hall_interrupt_work,
			  sc96281_hall_interrupt_work);

	chip->shutdown_notifier.notifier_call = sc96281_shutdown_cb;
	chip->shutdown_notifier.priority = 255;
	register_reboot_notifier(&chip->shutdown_notifier);

	ret = platform_class_wireless_register_ops(chip->rx_role, chip,
						   &sc96281_wls_ops);
	if (ret) {
		mca_log_err("register ops fail\n");
		goto err_dev;
	}

	schedule_delayed_work(&chip->init_detect_work, msecs_to_jiffies(1000));
	if (chip->support_hall)
		schedule_delayed_work(&chip->hall_interrupt_work,
				      msecs_to_jiffies(1200));

	mca_log_err("sc96281 probe success!\n");
	return ret;

err_dev:
	mca_log_err("sc96281 probe failed!\n");
	return 0;
}

static int sc96281_suspend(struct device *dev)
{
	struct sc96281 *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Suspend successfully!");
	if (device_may_wakeup(dev))
		enable_irq_wake(sc->irq);

	return 0;
}

static int sc96281_resume(struct device *dev)
{
	struct sc96281 *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Resume successfully!");
	if (device_may_wakeup(dev))
		disable_irq_wake(sc->irq);

	return 0;
}

static const struct dev_pm_ops sc96281_pm_ops = {
	.resume = sc96281_resume,
	.suspend = sc96281_suspend,
};

static void sc96281_charger_remove(struct i2c_client *client)
{
	struct sc96281 *chip = i2c_get_clientdata(client);

	mutex_destroy(&chip->i2c_rw_lock);
	mutex_destroy(&chip->data_transfer_lock);
	mutex_destroy(&chip->wireless_chg_int_lock);

	if (chip->irq_gpio > 0)
		gpio_free(chip->irq_gpio);
	if (chip->power_good_gpio > 0)
		gpio_free(chip->power_good_gpio);
	if (chip->support_hall) {
		if (chip->hall_int_gpio > 0)
			gpio_free(chip->hall_int_gpio);
	}

	//devm_kfree(&client->dev, chip);
}

static void sc96281_charger_shutdown(struct i2c_client *client)
{
	//struct sc96281 *sc = i2c_get_clientdata(client);
	return;
}

static struct of_device_id sc96281_charger_match_table[] = {
	{
		.compatible = "sc,sc96281-wireless-charger",
		.data = NULL,
	},
	{},
};
MODULE_DEVICE_TABLE(of, sc96281_charger_match_table);

static const struct i2c_device_id sc96281_id[] = {
	{ "sc96281", 0 },
	{},
};

static struct i2c_driver sc96281_wireless_charger_driver = {
    .driver     = {
        .name   = "sc-wireless-charger",
        .owner  = THIS_MODULE,
        .of_match_table = of_match_ptr(sc96281_charger_match_table),
        .pm     = &sc96281_pm_ops,
    },
    .probe      = sc96281_charger_probe,
    .remove     = sc96281_charger_remove,
    .shutdown   = sc96281_charger_shutdown,
    .id_table	= sc96281_id,
};

module_i2c_driver(sc96281_wireless_charger_driver);

MODULE_DESCRIPTION("SC SC96281 Wireless Charge Driver");
MODULE_AUTHOR("tianye9@xiaomi.com");
MODULE_LICENSE("GPL v2");
