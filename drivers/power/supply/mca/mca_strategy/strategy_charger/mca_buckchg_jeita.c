// SPDX-License-Identifier: GPL-2.0
/*
 *mca_buckchg_jieta.c
 *
 * mca buck charger jeita control driver
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
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
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include "inc/mca_buckchg_jeita.h"
#include <mca/platform/platform_loadsw_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/common/mca_hwid.h>
#include "hwid.h"
#include <mca/protocol/protocol_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_buckchg_jeita"
#endif

#define MCA_DTPT_MOLECULE_SCALE 8
#define MCA_DTPT_DENOM_SCALE 10
#define MCA_BASE_OVER_CURR_COUNT_MAX 4

#define JEITA_COLD_ZONE_INDEX 1

static int mca_buckchg_jeita_get_curr(struct mca_buckchg_jeita_data *jeita_data,
				      int *vbat_index)
{
	int i;
	int ret;
	int vbat = 0;
	int min_cur = jeita_data->max_current;

	ret = strategy_class_fg_ops_get_voltage(&vbat);
	if (ret) {
		mca_log_err("get vbat failed\n");
		return min_cur;
	}

	if (!jeita_data->volt_para.size)
		return min_cur;

	for (i = 0; i < jeita_data->volt_para.size; i++) {
		min_cur = min(min_cur,
			      jeita_data->volt_para.volt_data[i].max_current);
		if (vbat <= jeita_data->volt_para.volt_data[i].voltage) {
			*vbat_index = i;
			return jeita_data->volt_para.volt_data[i].max_current;
		}
	}

	/* can not find para, use min current */
	return min_cur;
}

#define ABNORMAL_BATT_FV_MAX 4200
#define JEITA_HOT_RECHARGE_VBAT_HYS 150

static void mca_buckchg_base_jeita_update(struct mca_buckchg_jeita_dev *info)
{
	int temp = 0;
	int i, ret;
	struct mca_buckchg_jeita_data *jeita_data, *cur_jeita_data;
	int fastcharge_mode = 0;
	int now_curr = 0, chg_curr = 0, effective_curr;
	int hys_affect = 0;
	int data_change = 0;
	static int last_fastcharge_mode;
	static int last_chg_curr;
	static bool runswocp;
	static int base_over_curr_count;
	int vbat_index = -1;
	int vbat = 0;

	if (!info->base_jeita_para.jeita_data || !info->voter_ok) {
		mca_log_err("jeita data not ready\n");
		return;
	}
	effective_curr = mca_get_effective_result(info->fcc_voter);

	ret = platform_fg_ops_get_temp(FG_IC_MASTER, &temp);
	ret |= platform_fg_ops_get_curr(FG_IC_MASTER, &now_curr);
	if (ret) {
		mca_log_err("get battery temp or currfailed\n");
		return;
	}

	temp /= 10;
	fastcharge_mode = strategy_class_fg_get_fastcharge();
	mca_log_err("fastcharge_mode is %d\n", fastcharge_mode);
	platform_fg_ops_get_volt(FG_IC_MASTER, &vbat);
	mca_log_info("vbat is %d\n", vbat);

	if (!fastcharge_mode) {
		for (i = 0; i < info->base_jeita_para.size; i++) {
			cur_jeita_data = info->base_jeita_para.jeita_data + i;
			if (temp < cur_jeita_data->temp_high &&
			    temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->base_jeita_para.size) {
			mca_log_err("can not find flip jeita para\n");
			return;
		}
	} else {
		for (i = 0; i < info->base_jeita_para.fcc_size; i++) {
			cur_jeita_data =
				info->base_jeita_para.jeita_ffc_data + i;
			if (temp < cur_jeita_data->temp_high &&
			    temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->base_jeita_para.fcc_size) {
			mca_log_err("can not find flip jeita fcc para\n");
			return;
		}
	}

	/* select cur jeita_data (temperature hysteresis) */
	if (info->base_proc_data.cur_jeita_index == -1) {
		jeita_data = cur_jeita_data;
		last_chg_curr =
			mca_buckchg_jeita_get_curr(cur_jeita_data, &vbat_index);
	} else {
		if (info->base_proc_data.cur_jeita_index == i ||
		    fastcharge_mode != last_fastcharge_mode)
			jeita_data = cur_jeita_data;
		else {
			if (!fastcharge_mode ||
			    info->base_jeita_para.fcc_size == 0)
				jeita_data = info->base_jeita_para.jeita_data +
					     info->base_proc_data.cur_jeita_index;
			else
				jeita_data =
					info->base_jeita_para.jeita_ffc_data +
					info->base_proc_data.cur_jeita_index;

			mca_log_info("temp_low: %d, temp_high: %d, temp: %d\n",
				     jeita_data->temp_low -
					     jeita_data->low_temp_hys,
				     jeita_data->temp_high +
					     jeita_data->high_temp_hys,
				     temp);
			if (i > info->base_proc_data.cur_jeita_index) {
				if (info->base_proc_data.cur_jeita_index == 0 &&
				    mca_smartchg_is_extreme_cold_enabled())
					jeita_data = cur_jeita_data;
				else if (temp < jeita_data->temp_high +
							 jeita_data->high_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			} else {
				if (temp > jeita_data->temp_low -
						   jeita_data->low_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			}
		}
	}
	last_fastcharge_mode = fastcharge_mode;

	info->base_proc_data.max_chg_curr =
		mca_buckchg_jeita_get_curr(jeita_data, &vbat_index);
	if (info->base_proc_data.cur_jeita_index != i && !hys_affect) {
		data_change = 1;
		mca_log_info(
			"jeita_index: cur_jeita_index:%d, i:%d, hys_affect:%d\n",
			info->base_proc_data.cur_jeita_index, i, hys_affect);
		last_chg_curr = info->base_proc_data.max_chg_curr;
	} else if (info->base_proc_data.max_chg_curr > last_chg_curr) {
		if (vbat_index != -1 &&
		    vbat < jeita_data->volt_para.volt_data[vbat_index].voltage -
				   info->base_vbat_low_hyst) {
			data_change = 1;
			mca_log_info(
				"vbat_thre[l->s]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d, vbat:%d\n",
				info->base_proc_data.max_chg_curr, last_chg_curr,
				vbat_index, vbat);
			last_chg_curr = info->base_proc_data.max_chg_curr;
		}
	} else if (info->base_proc_data.max_chg_curr < last_chg_curr) {
		if (vbat_index != -1 &&
		    vbat > jeita_data->volt_para.volt_data[vbat_index - 1]
				   .voltage) {
			data_change = 1;
			mca_log_info(
				"vbat_thre[s->l]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d\n",
				info->base_proc_data.max_chg_curr, last_chg_curr,
				vbat_index);
			last_chg_curr = info->base_proc_data.max_chg_curr;
		}
	}

	chg_curr = info->base_proc_data.max_chg_curr;
	mca_log_info(
		"cur index %d/%d max_chg_curr %d chg_curr %d hys_affect %d, now_curr %d, effective_curr %d\n",
		i, info->base_proc_data.cur_jeita_index,
		info->base_proc_data.max_chg_curr, chg_curr, hys_affect,
		now_curr, effective_curr);
	if (data_change && !hys_affect)
		info->base_proc_data.cur_jeita_index = i;

	if (effective_curr != 0 && now_curr / 1000 < -chg_curr)
		base_over_curr_count++;
	else
		base_over_curr_count = 0;

	if (base_over_curr_count >= MCA_BASE_OVER_CURR_COUNT_MAX &&
	    effective_curr != 0) {
		effective_curr -= 100;
		base_over_curr_count = 0;
		mca_vote(info->fcc_voter, "swocp", true, effective_curr);
		runswocp = true;
		mca_log_err("reduce fcc %d by swocp\n", effective_curr);
	}
	if (now_curr / 1000 > -chg_curr + 500 && runswocp) {
		runswocp = false;
		mca_vote(info->fcc_voter, "swocp", false, 0);
		mca_log_err("disable swocp\n");
	}
}

static void mca_buckchg_flip_jeita_update(struct mca_buckchg_jeita_dev *info)
{
	int temp = 0;
	int i, ret;
	struct mca_buckchg_jeita_data *jeita_data, *cur_jeita_data;
	int fastcharge_mode = 0;
	int chg_curr = 0;
	int hys_affect = 0;
	int data_change = 0;
	static int last_fastcharge_mode;
	static int last_chg_curr;
	int vbat_index = -1;
	int vbat = 0;

	if (!info->flip_jeita_para.jeita_data || !info->voter_ok) {
		mca_log_err("jeita data not ready\n");
		return;
	}

	ret = platform_fg_ops_get_temp(FG_IC_SLAVE, &temp);
	if (ret) {
		mca_log_err("get battery temp failed\n");
		return;
	}

	temp /= 10;
	fastcharge_mode = strategy_class_fg_get_fastcharge();
	mca_log_err("fastcharge_mode is %d\n", fastcharge_mode);
	platform_fg_ops_get_volt(FG_IC_SLAVE, &vbat);
	mca_log_info("vbat is %d\n", vbat);

	if (!fastcharge_mode) {
		for (i = 0; i < info->flip_jeita_para.size; i++) {
			cur_jeita_data = info->flip_jeita_para.jeita_data + i;
			if (temp < cur_jeita_data->temp_high &&
			    temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->flip_jeita_para.size) {
			mca_log_err("can not find flip jeita para\n");
			return;
		}
	} else {
		for (i = 0; i < info->flip_jeita_para.fcc_size; i++) {
			cur_jeita_data =
				info->flip_jeita_para.jeita_ffc_data + i;
			if (temp < cur_jeita_data->temp_high &&
			    temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->flip_jeita_para.fcc_size) {
			mca_log_err("can not find flip jeita fcc para\n");
			return;
		}
	}

	/* select cur jeita_data (temperature hysteresis) */
	if (info->flip_proc_data.cur_jeita_index == -1) {
		jeita_data = cur_jeita_data;
		last_chg_curr =
			mca_buckchg_jeita_get_curr(cur_jeita_data, &vbat_index);
	} else {
		if (info->flip_proc_data.cur_jeita_index == i ||
		    fastcharge_mode != last_fastcharge_mode)
			jeita_data = cur_jeita_data;
		else {
			if (!fastcharge_mode ||
			    info->flip_jeita_para.fcc_size == 0)
				jeita_data = info->flip_jeita_para.jeita_data +
					     info->flip_proc_data.cur_jeita_index;
			else
				jeita_data =
					info->flip_jeita_para.jeita_ffc_data +
					info->flip_proc_data.cur_jeita_index;

			mca_log_info("temp_low: %d, temp_high: %d, temp: %d\n",
				     jeita_data->temp_low -
					     jeita_data->low_temp_hys,
				     jeita_data->temp_high +
					     jeita_data->high_temp_hys,
				     temp);
			if (i > info->flip_proc_data.cur_jeita_index) {
				if (info->flip_proc_data.cur_jeita_index == 0 &&
				    mca_smartchg_is_extreme_cold_enabled())
					jeita_data = cur_jeita_data;
				else if (temp < jeita_data->temp_high +
							 jeita_data->high_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			} else {
				if (temp > jeita_data->temp_low -
						   jeita_data->low_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			}
		}
	}
	last_fastcharge_mode = fastcharge_mode;

	info->flip_proc_data.max_chg_curr =
		mca_buckchg_jeita_get_curr(jeita_data, &vbat_index);
	if (info->flip_proc_data.cur_jeita_index != i && !hys_affect) {
		data_change = 1;
		mca_log_info(
			"jeita_index: cur_jeita_index:%d, i:%d, hys_affect:%d\n",
			info->flip_proc_data.cur_jeita_index, i, hys_affect);
		last_chg_curr = info->flip_proc_data.max_chg_curr;
	} else if (info->flip_proc_data.max_chg_curr > last_chg_curr) {
		if (vbat_index != -1 &&
		    vbat < jeita_data->volt_para.volt_data[vbat_index].voltage -
				   info->flip_vbat_low_hyst) {
			data_change = 1;
			mca_log_info(
				"vbat_thre[l->s]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d, vbat:%d\n",
				info->flip_proc_data.max_chg_curr, last_chg_curr,
				vbat_index, vbat);
			last_chg_curr = info->flip_proc_data.max_chg_curr;
		}
	} else if (info->flip_proc_data.max_chg_curr < last_chg_curr) {
		if (vbat_index != -1 &&
		    vbat > jeita_data->volt_para.volt_data[vbat_index - 1]
				   .voltage) {
			data_change = 1;
			mca_log_info(
				"vbat_thre[s->l]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d\n",
				info->flip_proc_data.max_chg_curr, last_chg_curr,
				vbat_index);
			last_chg_curr = info->flip_proc_data.max_chg_curr;
		}
	}

	chg_curr = info->flip_proc_data.max_chg_curr;
	mca_log_info("cur index %d/%d max_chg_curr %d chg_curr %d hys_affect %d\n",
		     i, info->flip_proc_data.cur_jeita_index,
		     info->flip_proc_data.max_chg_curr, chg_curr, hys_affect);

	if (data_change && !hys_affect) {
		info->flip_proc_data.cur_jeita_index = i;
		mca_vote(info->flip_fcc_voter, "jeita", true, chg_curr);
	}
}
static void mca_buckchg_jeita_update(struct mca_buckchg_jeita_dev *info)
{
	int temp = 0;
	int i, ret;
	struct mca_buckchg_jeita_data *jeita_data, *cur_jeita_data;
	int fastcharge_mode = 0;
	int vterm = 0, chg_curr = 0;
	int hys_affect = 0;
	int data_change = 0;
	int vbat = 0;
	static int last_fastcharge_mode;
	static int last_chg_curr;
	int vbat_index = -1;

	if (!info->jeita_para.jeita_data || !info->voter_ok) {
		mca_log_err("jeita data not ready\n");
		return;
	}

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
		mca_log_err("get battery temp failed\n");
		return;
	}

	temp /= 10;
	fastcharge_mode = strategy_class_fg_get_fastcharge();

	ret = strategy_class_fg_ops_get_voltage(&vbat);
	if (ret) {
		mca_log_err("get battery volt failed\n");
		return;
	}

	if (!fastcharge_mode) {
		for (i = 0; i < info->jeita_para.size; i++) {
			cur_jeita_data = info->jeita_para.jeita_data + i;
			if (temp < cur_jeita_data->temp_high &&
			    temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->jeita_para.size) {
			mca_log_err("can not find jeita para\n");
			return;
		}
	} else {
		for (i = 0; i < info->jeita_para.fcc_size; i++) {
			cur_jeita_data = info->jeita_para.jeita_ffc_data + i;
			if (temp < cur_jeita_data->temp_high &&
			    temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->jeita_para.fcc_size) {
			mca_log_err("can not find jeita fcc para\n");
			return;
		}
	}

	/* select cur jeita_data */
	if (info->proc_data.cur_jeita_index == -1 || info->baacfg_update) {
		jeita_data = cur_jeita_data;
		last_chg_curr =
			mca_buckchg_jeita_get_curr(cur_jeita_data, &vbat_index);
	} else {
		if (info->proc_data.cur_jeita_index == i ||
		    fastcharge_mode != last_fastcharge_mode)
			jeita_data = cur_jeita_data;
		else {
			if (!fastcharge_mode || info->jeita_para.fcc_size == 0)
				jeita_data = info->jeita_para.jeita_data +
					     info->proc_data.cur_jeita_index;
			else
				jeita_data = info->jeita_para.jeita_ffc_data +
					     info->proc_data.cur_jeita_index;

			mca_log_info("temp_low: %d, temp_high: %d, temp: %d\n",
				     jeita_data->temp_low -
					     jeita_data->low_temp_hys,
				     jeita_data->temp_high +
					     jeita_data->high_temp_hys,
				     temp);
			if (i > info->proc_data.cur_jeita_index) {
				if (info->proc_data.cur_jeita_index == 0 &&
				    mca_smartchg_is_extreme_cold_enabled())
					jeita_data = cur_jeita_data;
				else if (temp <
					 jeita_data->temp_high +
						 jeita_data->high_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			} else {
				if (temp > jeita_data->temp_low -
						   jeita_data->low_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			}
		}
	}

	/* select whether vote */
	info->proc_data.max_chg_curr =
		mca_buckchg_jeita_get_curr(jeita_data, &vbat_index);
	if (info->baacfg_update) {
		data_change = 1;
		info->baacfg_update = false;
		mca_log_info("BAA config data update\n");
	} else if (info->proc_data.cur_jeita_index != i &&
		   !hys_affect) { //jeita_index change
		data_change = 1;
		mca_log_info(
			"jeita_index: cur_jeita_index:%d, i:%d, hys_affect:%d\n",
			info->proc_data.cur_jeita_index, i, hys_affect);
		last_chg_curr = info->proc_data.max_chg_curr;
	} else { //jeita_index not change, check vbat_thre
		if (info->proc_data.max_chg_curr > last_chg_curr) {
			bool in_cold_zone = (i == 1 || i == 2 ||
					     (i == 3 && hys_affect));
			int low_hyst = in_cold_zone ? info->vbat_low_cold_hyst :
						      info->vbat_low_hyst;

			if (vbat_index != -1 &&
			    vbat < jeita_data->volt_para.volt_data[vbat_index]
						    .voltage -
					    low_hyst) {
				data_change = 1;
				mca_log_info(
					"vbat_thre[l->s]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d\n",
					info->proc_data.max_chg_curr,
					last_chg_curr, vbat_index);
				last_chg_curr = info->proc_data.max_chg_curr;
			}
		} else if (info->proc_data.max_chg_curr < last_chg_curr) {
			if (vbat_index != -1 &&
			    vbat > jeita_data->volt_para
					    .volt_data[vbat_index - 1]
					    .voltage) {
				data_change = 1;
				mca_log_info(
					"vbat_thre[s->l]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d\n",
					info->proc_data.max_chg_curr,
					last_chg_curr, vbat_index);
				last_chg_curr = info->proc_data.max_chg_curr;
			}
		}
	}

	vterm = jeita_data->vterm;

	if (vterm > ABNORMAL_BATT_FV_MAX) {
		vterm -= info->smartchg_data.delta_fv;
		mca_vote(info->en_voter, "jeita-hot", true, 1);
		if (vterm != info->vterm) {
			mca_vote(info->vterm_voter, "jeita", true, vterm);
			info->vterm = vterm;
		}
	} else {
		if (vbat >= vterm - info->jeita_hot_termination_hyst)
			mca_vote(info->en_voter, "jeita-hot", true, 0);
		else if (vbat < vterm - JEITA_HOT_RECHARGE_VBAT_HYS)
			mca_vote(info->en_voter, "jeita-hot", true, 1);
	}

	if (info->dtpt_status)
		chg_curr = info->proc_data.max_chg_curr *
			   MCA_DTPT_MOLECULE_SCALE / MCA_DTPT_DENOM_SCALE;
	else
		chg_curr = info->proc_data.max_chg_curr;

	mca_log_info(
		"cur index %d/%d last_chg_curr %d chg_curr %d/%d fastcharge %d jeita_vterm %d jeita_iterm %d vterm %d delta_fv %d hys_affect %d\n",
		i, info->proc_data.cur_jeita_index, last_chg_curr,
		info->proc_data.max_chg_curr, chg_curr, fastcharge_mode,
		jeita_data->vterm, jeita_data->iterm, vterm,
		info->smartchg_data.delta_fv, hys_affect);

	last_fastcharge_mode = fastcharge_mode;
	if (chg_curr)
		mca_vote(info->en_voter, "jeita", true, 1);
	else
		mca_vote(info->en_voter, "jeita", true, 0);

	if (data_change) {
		if (!hys_affect)
			info->proc_data.cur_jeita_index = i;
		mca_vote(info->fcc_voter, "jeita", true, chg_curr);
		mca_vote(info->vterm_voter, "jeita", true, vterm);
		if (!info->support_base_flip && !info->base_flip_same)
			mca_vote(info->iterm_voter, "jeita", true,
				 jeita_data->iterm);
	}

	if (!fastcharge_mode && info->base_flip_same) {
		info->cold_zone =
			(info->proc_data.cur_jeita_index == JEITA_COLD_ZONE_INDEX);
		mca_log_info(
			"cur index %d/%d last_chg_curr %d cold_zone %d\n", i,
			info->proc_data.cur_jeita_index, last_chg_curr,
			info->cold_zone);
	}
}

static int mca_buckchg_jeita_flip_charge_limit(struct mca_votable *votable,
					       void *data, int effective_result,
					       const char *effective_client)
{
	if (!data)
		return -1;

	mca_log_err("set flip limit current %d\n", effective_result);

	return platform_class_loadsw_set_ibat_limit(LOADSW_ROLE_MASTER,
						    effective_result);
}

static void mca_buckchg_jeita_init_voter(struct mca_buckchg_jeita_dev *info)
{
	info->flip_fcc_voter = mca_create_votable(
		"flip_charge_curr", MCA_VOTE_MIN,
		mca_buckchg_jeita_flip_charge_limit,
		MCA_BUCKCHG_JEITA_FLIP_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->flip_fcc_voter))
		return;

	info->en_voter = mca_find_votable("chg_enable");
	if (!info->en_voter)
		return;
	info->fcc_voter = mca_find_votable("buck_charge_curr");
	if (!info->fcc_voter)
		return;
	info->vterm_voter = mca_find_votable("term_volt");
	if (!info->vterm_voter)
		return;
	info->iterm_voter = mca_find_votable("term_curr");
	if (!info->iterm_voter)
		return;

	info->voter_ok = 1;
}

static void mca_buckchg_jeita_monitor_work(struct work_struct *work)
{
	int interval = MCA_BUCKCHG_JEITA_UPDATE_NORMAL_INTERVAL;
	struct mca_buckchg_jeita_dev *info = container_of(
		work, struct mca_buckchg_jeita_dev, monitor_work.work);

	if (!info->voter_ok)
		mca_buckchg_jeita_init_voter(info);

	if (!info->voter_ok) {
		interval = MCA_BUCKCHG_JEITA_INIT_VOTER_INTERVAL;
		mca_log_info("voter not ok\n");
		goto out;
	}

	if (!info->online) {
		mca_log_info("adapter is plugout\n");
		return;
	}
	mca_buckchg_jeita_update(info);
	if (info->support_base_flip) {
		mca_buckchg_base_jeita_update(info);
		mca_buckchg_flip_jeita_update(info);
	}
out:
	schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(interval));
}

static int
mca_buckchg_parse_volt_para(struct device_node *node, const char *name,
			    struct mca_buckchg_jeita_volt_para *volt_para)
{
	struct mca_buckchg_jeita_volt_data *volt_data;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	if (strcmp(name, "null") == 0) {
		mca_log_info("no need parse volt para\n");
		return 0;
	}
	array_len = mca_parse_dts_count_strings(node, name,
						JEITA_VOLT_DATA_MAX_GROUP,
						BUCKCHG_VOLTAGE_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	volt_para->size = array_len / BUCKCHG_VOLTAGE_PARA_MAX;
	volt_para->volt_data =
		kcalloc(volt_para->size, sizeof(*volt_data), GFP_KERNEL);
	if (!volt_para->volt_data) {
		mca_log_err("volt para no mem\n");
		return -ENOMEM;
	}
	volt_data = volt_para->volt_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / BUCKCHG_VOLTAGE_PARA_MAX;
		col = i % BUCKCHG_VOLTAGE_PARA_MAX;

		switch (col) {
		case BUCKCHG_VOLTAGE_TH:
			if (kstrtoint(tmp_string, 10, &volt_data[row].voltage))
				goto error;
			break;
		case BUCKCHG_CURRENT_MAX:
			if (kstrtoint(tmp_string, 10,
				      &volt_data[row].max_current))
				goto error;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < volt_para->size; i++)
		mca_log_debug("volt_para %d %d\n", volt_data[i].voltage,
			      volt_data[i].max_current);

	return 0;
error:
	kfree(volt_data);
	volt_data = NULL;
	return -1;
}

static int
mca_buckchg_jeita_parse_para(struct device_node *node, const char *name,
			     struct mca_buckchg_jeita_para *jeita_info)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	char ffc_name[50];
	struct mca_buckchg_jeita_data *jeita_data, *jeita_ffc_data;

	if (!node) {
		mca_log_err("node in null\n");
		return -1;
	}

	array_len = mca_parse_dts_count_strings(
		node, name, JEITA_DATA_MAX_GROUP, JEITA_TEMP_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse jeita failed\n");
		return -1;
	}

	jeita_info->size = array_len / JEITA_TEMP_PARA_MAX;
	jeita_info->jeita_data = kcalloc(
		jeita_info->size, sizeof(*jeita_info->jeita_data), GFP_KERNEL);
	if (!jeita_info->jeita_data) {
		mca_log_err("temp para no mem\n");
		return -ENOMEM;
	}
	jeita_data = jeita_info->jeita_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / JEITA_TEMP_PARA_MAX;
		col = i % JEITA_TEMP_PARA_MAX;
		switch (col) {
		case JEITA_TEMP_PARA_TEMP_LOW:
			if (kstrtoint(tmp_string, 10,
				      &jeita_data[row].temp_low))
				goto error;
			break;
		case JEITA_TEMP_PARA_TEMP_HIGH:
			if (kstrtoint(tmp_string, 10,
				      &jeita_data[row].temp_high))
				goto error;
			break;
		case JEITA_TEMP_PARA_LOW_TEMP_HYS:
			if (kstrtoint(tmp_string, 10,
				      &jeita_data[row].low_temp_hys))
				goto error;
			break;
		case JEITA_TEMP_PARA_HIGH_TEMP_HYS:
			if (kstrtoint(tmp_string, 10,
				      &jeita_data[row].high_temp_hys))
				goto error;
			break;
		case JEITA_TEMP_PARA_MAX_CURRENT:
			if (kstrtoint(tmp_string, 10,
				      &jeita_data[row].max_current))
				goto error;
			break;
		case JEITA_TEMP_PARA_VTERM:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].vterm))
				goto error;
			break;
		case JEITA_TEMP_PARA_ITERM:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].iterm))
				goto error;
			break;
		case JEITA_TEMP_PARA_VOLT_PARA_NAME:
			if (mca_buckchg_parse_volt_para(
				    node, tmp_string,
				    &jeita_data[row].volt_para))
				goto error;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < jeita_info->size; i++)
		mca_log_err("jeita para %d %d %d %d %d %d %d %d\n",
			    jeita_data[i].temp_low, jeita_data[i].temp_high,
			    jeita_data[i].low_temp_hys,
			    jeita_data[i].high_temp_hys,
			    jeita_data[i].max_current, jeita_data[i].vterm,
			    jeita_data[i].iterm, jeita_data[i].volt_para.size);

	snprintf(ffc_name, sizeof(ffc_name), "%s", name);
	snprintf(ffc_name + strlen(ffc_name),
		 sizeof(ffc_name) - strlen(ffc_name), "_ffc");
	mca_log_err("new name:%s\n", ffc_name);
	array_len = mca_parse_dts_count_strings(
		node, ffc_name, JEITA_DATA_MAX_GROUP, JEITA_TEMP_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse jeita para ffc failed\n");
		return 0;
	}

	jeita_info->fcc_size = array_len / JEITA_TEMP_PARA_MAX;

	jeita_info->jeita_ffc_data =
		kcalloc(jeita_info->fcc_size,
			sizeof(*jeita_info->jeita_ffc_data), GFP_KERNEL);
	if (!jeita_info->jeita_ffc_data) {
		mca_log_err("temp para no mem\n");
		return -ENOMEM;
	}
	jeita_ffc_data = jeita_info->jeita_ffc_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, ffc_name, i, &tmp_string))
			return -1;

		row = i / JEITA_TEMP_PARA_MAX;
		col = i % JEITA_TEMP_PARA_MAX;
		switch (col) {
		case JEITA_TEMP_PARA_TEMP_LOW:
			if (kstrtoint(tmp_string, 10,
				      &jeita_ffc_data[row].temp_low))
				goto error1;
			break;
		case JEITA_TEMP_PARA_TEMP_HIGH:
			if (kstrtoint(tmp_string, 10,
				      &jeita_ffc_data[row].temp_high))
				goto error1;
			break;
		case JEITA_TEMP_PARA_LOW_TEMP_HYS:
			if (kstrtoint(tmp_string, 10,
				      &jeita_ffc_data[row].low_temp_hys))
				goto error1;
			break;
		case JEITA_TEMP_PARA_HIGH_TEMP_HYS:
			if (kstrtoint(tmp_string, 10,
				      &jeita_ffc_data[row].high_temp_hys))
				goto error1;
			break;
		case JEITA_TEMP_PARA_MAX_CURRENT:
			if (kstrtoint(tmp_string, 10,
				      &jeita_ffc_data[row].max_current))
				goto error1;
			break;
		case JEITA_TEMP_PARA_VTERM:
			if (kstrtoint(tmp_string, 10,
				      &jeita_ffc_data[row].vterm))
				goto error1;
			break;
		case JEITA_TEMP_PARA_ITERM:
			if (kstrtoint(tmp_string, 10,
				      &jeita_ffc_data[row].iterm))
				goto error1;
			break;
		case JEITA_TEMP_PARA_VOLT_PARA_NAME:
			if (mca_buckchg_parse_volt_para(
				    node, tmp_string,
				    &jeita_ffc_data[row].volt_para))
				goto error1;
			break;
		default:
			break;
		}
	}
	for (i = 0; i < jeita_info->fcc_size; i++)
		mca_log_err("jeita ffc para %d %d %d %d %d %d %d %d\n",
			    jeita_ffc_data[i].temp_low,
			    jeita_ffc_data[i].temp_high,
			    jeita_ffc_data[i].low_temp_hys,
			    jeita_ffc_data[i].high_temp_hys,
			    jeita_ffc_data[i].max_current,
			    jeita_ffc_data[i].vterm, jeita_ffc_data[i].iterm,
			    jeita_ffc_data[i].volt_para.size);
	return 0;
error1:
	kfree(jeita_ffc_data);
	jeita_ffc_data = NULL;
error:
	kfree(jeita_data);
	jeita_data = NULL;
	return -1;
}

static int mca_buckchg_jeita_parse_dt(struct mca_buckchg_jeita_dev *info)
{
	struct mca_buckchg_jeita_para *jeita_info = &info->jeita_para;
	struct mca_buckchg_jeita_para *base_jeita_info = &info->base_jeita_para;
	struct mca_buckchg_jeita_para *flip_jeita_info = &info->flip_jeita_para;
	struct device_node *node = info->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	const char *dev_name = NULL;
	int ret;

	mca_parse_dts_u32(node, "vbat_high_hyst", &(info->vbat_high_hyst),
			  MCA_BUCKCHG_JEITA_VBAT_HIGH_HYST);
	mca_parse_dts_u32(node, "vbat_low_hyst", &(info->vbat_low_hyst),
			  MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "vbat_low_cold_hyst",
			  &(info->vbat_low_cold_hyst),
			  MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "base_vbat_low_hyst",
			  &(info->base_vbat_low_hyst),
			  MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "flip_vbat_low_hyst",
			  &(info->flip_vbat_low_hyst),
			  MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "jeita_hot_termination_hyst",
			  &(info->jeita_hot_termination_hyst), 0);
	info->has_gbl_batt_para =
		of_property_read_bool(node, "has-global-batt-para");
	if (hwid && info->has_gbl_batt_para &&
	    hwid->country_version != CountryCN)
		node = of_find_node_by_name(NULL, "mca_buckchg_jeita_gbl_para");

	if (!node) {
		mca_log_err("node in null\n");
		return -1;
	}

	info->has_tmp_batt_para =
		of_property_read_bool(node, "has-tmp-batt-para");
	if (info->has_tmp_batt_para) {
		platform_fg_ops_get_device_name(FG_IC_MASTER, &dev_name);
		if (!dev_name) {
			mca_log_err("get device name fail, wait for it\n");
			return -EINVAL;
		}
		mca_log_err("project O9 tmp test: device name: %s\n", dev_name);
		if (!strcmp(dev_name, "2@BP")) {
			node = of_find_node_by_name(
				NULL, "mca_buckchg_jeita_tmp_para");
			if (!node) {
				mca_log_err("has-tmp-batt-para node in null\n");
				return -1;
			}
		}
	}

	ret = mca_buckchg_jeita_parse_para(node, "jeita_para", jeita_info);
	info->support_base_flip =
		of_property_read_bool(node, "support-base-flip");
	info->base_flip_same = of_property_read_bool(node, "base-flip-same");
	if (info->support_base_flip) {
		mca_log_err("support base flip, start config buckchg jeita\n");
		ret = mca_buckchg_jeita_parse_para(node, "base_jeita_para",
						   base_jeita_info);
		ret = mca_buckchg_jeita_parse_para(node, "flip_jeita_para",
						   flip_jeita_info);
	}

	return ret;
}

static int mca_buckchg_jeita_process_event(int event, int value, void *data)
{
	struct mca_buckchg_jeita_dev *info = data;

	if (!info)
		return -1;

	mca_log_info("receive event %d, value %d\n", event, value);
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
		info->online = 1;
		info->proc_data.cur_jeita_index = -1;
		info->proc_data.max_chg_curr = 0;
		cancel_delayed_work_sync(&info->monitor_work);
		schedule_delayed_work(&info->monitor_work, 0);
		break;
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		info->online = 0;
		cancel_delayed_work_sync(&info->monitor_work);
		info->flip_proc_data.cur_jeita_index = 0;
		info->flip_proc_data.temp_hys_en = 0;
		info->flip_proc_data.max_chg_curr = -1;
		info->base_proc_data.cur_jeita_index = 0;
		info->base_proc_data.temp_hys_en = 0;
		info->base_proc_data.max_chg_curr = -1;
		break;
	case MCA_EVENT_BATTERY_DTPT:
		info->dtpt_status = value;
		break;
	default:
		break;
	}

	return 0;
}

static int
mca_strategy_buckchg_jeita_get_normal_vterm(struct mca_buckchg_jeita_dev *info)
{
	int cur_normal_vterm = 0, temp = 0;
	int ret, i;
	struct mca_buckchg_jeita_data *cur_jeita_data;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
		mca_log_err("get battery temp failed\n");
		return 0;
	}
	temp /= 10;

	for (i = 0; i < info->jeita_para.size; i++) {
		cur_jeita_data = info->jeita_para.jeita_data + i;
		if (temp < cur_jeita_data->temp_high &&
		    temp >= cur_jeita_data->temp_low)
			break;
	}
	if (i == info->jeita_para.size) {
		mca_log_err("can not find jeita para\n");
		return 0;
	}

	cur_normal_vterm = cur_jeita_data->vterm;

	return cur_normal_vterm;
}

static int
mca_strategy_buckchg_jeita_get_ffc_iterm(struct mca_buckchg_jeita_dev *info)
{
	int cur_ffc_itrem = 0, temp = 0;
	int ret, i;
	struct mca_buckchg_jeita_data *cur_jeita_data;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
		mca_log_err("get battery temp failed\n");
		return 0;
	}
	temp /= 10;

	for (i = 0; i < info->jeita_para.fcc_size; i++) {
		cur_jeita_data = info->jeita_para.jeita_ffc_data + i;
		if (temp < cur_jeita_data->temp_high &&
		    temp >= cur_jeita_data->temp_low)
			break;
	}
	if (i == info->jeita_para.fcc_size) {
		mca_log_err("can not find jeita fcc para\n");
		return 0;
	}

	cur_ffc_itrem = cur_jeita_data->iterm;

	return cur_ffc_itrem;
}

static int
mca_strategy_buckchg_jeita_get_ffc_vterm(struct mca_buckchg_jeita_dev *info)
{
	int cur_ffc_vterm = 0, temp = 0;
	int ret, i;
	struct mca_buckchg_jeita_data *cur_jeita_data;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
		mca_log_err("get battery temp failed\n");
		return 0;
	}
	temp /= 10;

	for (i = 0; i < info->jeita_para.fcc_size; i++) {
		cur_jeita_data = info->jeita_para.jeita_ffc_data + i;
		if (temp < cur_jeita_data->temp_high &&
		    temp >= cur_jeita_data->temp_low)
			break;
	}
	if (i == info->jeita_para.fcc_size) {
		mca_log_err("can not find jeita fcc para\n");
		return 0;
	}

	cur_ffc_vterm = cur_jeita_data->vterm;

	return cur_ffc_vterm;
}

static int mca_strategy_buckchg_jeita_get_status(int status, void *value,
						 void *data)
{
	struct mca_buckchg_jeita_dev *info =
		(struct mca_buckchg_jeita_dev *)data;
	int *cur_val = (int *)value;

	if (!info || !value)
		return -1;

	switch (status) {
	case STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM:
		*cur_val = mca_strategy_buckchg_jeita_get_ffc_iterm(info);
		break;
	case STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM:
		*cur_val = mca_strategy_buckchg_jeita_get_normal_vterm(info);
		break;
	case STRATEGY_STATUS_TYPE_JEITA_FFC_VTERM:
		*cur_val = mca_strategy_buckchg_jeita_get_ffc_vterm(info);
		break;
	case STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE:
		*cur_val = info->cold_zone;
		break;
	default:
		return -1;
	}

	return 0;
}

static int mca_jeita_smartchg_delta_fv_callback(void *data,
						int effective_result)
{
	struct mca_buckchg_jeita_dev *info =
		(struct mca_buckchg_jeita_dev *)data;

	if (!data)
		return -1;

	info->smartchg_data.delta_fv = effective_result;
	mca_log_err("effective_result: %d\n", effective_result);

	return 0;
}

static int mca_buckchg_jeita_smartchg_baa_update_jeita_data(
	struct mca_buckchg_jeita_data *jeita_data,
	struct smart_batt_jeita_term_para *para)
{
	int max_volt_idx = jeita_data->volt_para.size - 1;

	mca_log_info(
		"jeita_term_para idx: %d, temp: %d:%d => %d:%d, vterm: %d => %d, iterm: %d => %d\n",
		para->t_range.idx, para->t_range.min, para->t_range.max,
		jeita_data->temp_low, jeita_data->temp_high, jeita_data->vterm,
		para->vterm, jeita_data->iterm, para->iterm);

	jeita_data->vterm = para->vterm;
	jeita_data->iterm = para->iterm;
	if (jeita_data->volt_para.size > 0) {
		mca_log_info(
			"volt_data[%d].voltage: %d => %d\n", max_volt_idx,
			jeita_data->volt_para.volt_data[max_volt_idx].voltage,
			jeita_data->vterm);
		jeita_data->volt_para.volt_data[max_volt_idx].voltage =
			jeita_data->vterm;
	}

	return 0;
}

static int mca_buckchg_jeita_smartchg_update_baa_para(void *data,
						      char *baa_para,
						      int ffc_size,
						      int normal_size)
{
	struct mca_buckchg_jeita_dev *info =
		(struct mca_buckchg_jeita_dev *)data;
	struct smart_batt_jeita_term_para *jeita_term_para = NULL;
	int offset = 0;
	int index = 0;

	if (!data || !baa_para) {
		mca_log_err("data or baa_para is NULL\n");
		return -1;
	}

	// update ffc jeita para
	jeita_term_para = (struct smart_batt_jeita_term_para *)baa_para;
	for (int i = 0; i < ffc_size; i++) {
		index = jeita_term_para[i].t_range.idx;
		if (index < 0 || index >= info->jeita_para.fcc_size) {
			mca_log_err(
				"jeita_para.fcc_size: %d, invalid idx: %d\n",
				info->jeita_para.fcc_size, index);
		} else {
			mca_buckchg_jeita_smartchg_baa_update_jeita_data(
				&(info->jeita_para.jeita_ffc_data[index]),
				&(jeita_term_para[i]));
		}
	}

	// update normal jeita para
	offset = ffc_size * sizeof(struct smart_batt_jeita_term_para);
	jeita_term_para =
		(struct smart_batt_jeita_term_para *)(baa_para + offset);
	for (int i = 0; i < normal_size; i++) {
		index = jeita_term_para[i].t_range.idx;
		if (index < 0 || index >= info->jeita_para.size) {
			mca_log_err("jeita_para.size: %d, invalid idx: %d\n",
				    info->jeita_para.size, index);
		} else {
			mca_buckchg_jeita_smartchg_baa_update_jeita_data(
				&(info->jeita_para.jeita_data[index]),
				&(jeita_term_para[i]));
		}
	}

	info->baacfg_update = true;
	return 0;
}

static struct mca_smartchg_if_ops g_jeita_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_JEITA,
	.data = NULL,
	.set_delta_fv = mca_jeita_smartchg_delta_fv_callback,
	.set_delta_ichg = NULL,
	.update_baa_para = mca_buckchg_jeita_smartchg_update_baa_para,
};

static int mca_buckchg_jeita_probe(struct platform_device *pdev)
{
	struct mca_buckchg_jeita_dev *info;
	int ret;
	int online = 0, wireless_online;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	ret = mca_buckchg_jeita_parse_dt(info);
	if (ret) {
		mca_log_err("parse dts failed, ret: %d\n", ret);
		if (ret != -1) {
			static int parse_dt_cnt;

			dev_err(&pdev->dev,
				"%s buckchg_jeita parse_dt_cnt = %d\n",
				__func__, ++parse_dt_cnt);
			msleep(100);
			return parse_dt_cnt <= 49 ? -EPROBE_DEFER : -1;
		}
	}
	INIT_DELAYED_WORK(&info->monitor_work, mca_buckchg_jeita_monitor_work);
	mca_buckchg_jeita_init_voter(info);
	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_JEITA,
					mca_buckchg_jeita_process_event,
					mca_strategy_buckchg_jeita_get_status,
					NULL, info);
	g_jeita_smartchg_if_ops.data = info;
	(void)mca_smartchg_if_ops_register(&g_jeita_smartchg_if_ops);

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &online);
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &wireless_online);

	if (online || wireless_online) {
		info->online = 1;
		schedule_delayed_work(&info->monitor_work, 0);
		return 0;
	}

	if (!info->voter_ok)
		schedule_delayed_work(
			&info->monitor_work,
			msecs_to_jiffies(
				MCA_BUCKCHG_JEITA_INIT_VOTER_INTERVAL));
	mca_log_err("probe ok\n");
	return 0;
}

static int mca_buckchg_jeita_remove(struct platform_device *pdev)
{
	return 0;
}

static void mca_buckchg_jeita_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,buckchg_jeita" },
	{},
};

static struct platform_driver mca_buckchg_jeita_driver = {
	.driver	= {
		.name = "mca_buckchg_jeita",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
	.probe = mca_buckchg_jeita_probe,
	.remove = mca_buckchg_jeita_remove,
	.shutdown = mca_buckchg_jeita_shutdown,
};

static int __init mca_buckchg_jeita_init(void)
{
	return platform_driver_register(&mca_buckchg_jeita_driver);
}
module_init(mca_buckchg_jeita_init);

static void __exit mca_buckchg_jeita_exit(void)
{
	platform_driver_unregister(&mca_buckchg_jeita_driver);
}
module_exit(mca_buckchg_jeita_exit);

MODULE_DESCRIPTION("buckchg jeita");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
