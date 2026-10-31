/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_connector_antiburn.h
 *
 * connector antiburn driver
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

#ifndef __CONNECTOR_ANTIBURN_H__
#define __CONNECTOR_ANTIBURN_H__

enum connector_antiburn_attr_list {
	CONNECTOR_PROP_TEMP_1,
	CONNECTOR_PROP_TEMP_2,
	CONNECTOR_PROP_TEMP_MAX,
	CONNECTOR_PROP_RESET_VSAFE0V,
	CONNECTOR_PROP_NTC_ALARM,
	CONNECTOR_PROP_MOS_CTRL,
};

struct connector_antiburn {
	struct device *dev;
	struct thermal_zone_device *tzd_conn;
	struct thermal_zone_device *tzd_conn2;
	struct notifier_block thermal_board_nb;
	struct notifier_block debug_nb;
	int triggered;
	int is_reset_vsafe0V;
	int ntc_alarm;
	int temperature[CONNECTOR_PROP_TEMP_MAX];
	int temp_increase_rate[CONNECTOR_PROP_TEMP_MAX];
	int fake_connector_temp[CONNECTOR_PROP_TEMP_MAX];
	int thermal_board_temp;
	int max_thermal_board_temp;
	int comb_sensorboard_con_trigger_temp;
	int comb_rate_conn_trigger_temp;
	int trigger_temp;
	int recharge_temp;
	int mos_ctrl_gpio;
	int otg_detect_en;
	int support_soft_antiburn;
	int support_hw_antiburn;
	int use_double_ntc;
	int monitor_interval;
	int max_temp_increase_rate;
	const char *thermal_zone_name;
	const char *thermal_zone_name2;
	bool otg_plugin_status;
	int usb_online;
	bool cid_status;
	int otg_boost_src;
	int en_src;
	int support_base_flip;
	int disable_antiburn;

	struct delayed_work monitor_work;
};

#endif /* __CONNECTOR_ANTIBURN_H__ */
