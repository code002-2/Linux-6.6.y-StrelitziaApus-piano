// SPDX-License-Identifier: GPL-2.0
/*
 * charger_partition.c
 *
 * charger partition driver
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

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/efi.h>
#include <linux/rcupdate.h>
#include <linux/of.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/completion.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_class.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include "inc/charger_partition.h"
#include <mca/shared_memory/charger_partition_class.h>
#include "hwid.h"
#include "sd.h"

#define CHARGER_PART_PLATFORM_V9 9
#define CHARGER_PART_NUMBER_V9 25
#define CHARGER_PART_NUMBER_DEFAULT 22

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "charger_partition"
#endif

struct ChargerPartition {
	struct device *dev;
	struct scsi_device *sdev;
	struct delayed_work charger_partition_work;

	int part_info_part_number;
	char *part_info_part_name;

	bool is_charger_partition_rdy;
	bool is_eu_model;
	bool not_notify_module;

	charger_partition_info_2 info_2;
};
static struct ChargerPartition *charger_partition;
static void *rw_buf;

struct partinfo {
	sector_t part_start;
	sector_t part_size;
};
static struct partinfo part_info = { 0 };

static int charger_scsi_read_partition(struct scsi_device *sdev, void *buf,
				       uint32_t lba, uint32_t blocks)
{
	uint8_t cdb[16];
	int ret = 0;
	struct scsi_sense_hdr sshdr = {};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
	};
	unsigned long flags = 0;

	spin_lock_irqsave(sdev->host->host_lock, flags);
	ret = scsi_device_get(sdev);
	if (!ret && !scsi_device_online(sdev)) {
		ret = -ENODEV;
		scsi_device_put(sdev);
		mca_log_err("get device fail\n");
	}
	spin_unlock_irqrestore(sdev->host->host_lock, flags);
	if (ret) {
		mca_log_err("failed to get scsi device\n");
		return ret;
	}
	sdev->host->eh_noresume = 1;

	// Fill in the CDB with SCSI command structure
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = READ_10; // Command
	cdb[1] = 0;
	cdb[2] = (lba >> 24) & 0xff; // LBA
	cdb[3] = (lba >> 16) & 0xff;
	cdb[4] = (lba >> 8) & 0xff;
	cdb[5] = (lba) & 0xff;
	cdb[6] = 0; // Group Number
	cdb[7] = (blocks >> 8) & 0xff; // Transfer Len
	cdb[8] = (blocks) & 0xff;
	cdb[9] = 0; // Control
	ret = scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_IN, buf,
			       (blocks * PART_BLOCK_SIZE),
			       msecs_to_jiffies(15000), 3, &exec_args);
	if (ret)
		mca_log_err("read error %d\n", ret);

	scsi_device_put(sdev);
	sdev->host->eh_noresume = 0;
	return ret;
}

static int charger_scsi_write_partition(struct scsi_device *sdev, void *buf,
					uint32_t lba, uint32_t blocks)
{
	uint8_t cdb[16];
	int ret = 0;
	struct scsi_sense_hdr sshdr = {};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
	};
	unsigned long flags = 0;

	spin_lock_irqsave(sdev->host->host_lock, flags);
	ret = scsi_device_get(sdev);
	if (!ret && !scsi_device_online(sdev)) {
		ret = -ENODEV;
		scsi_device_put(sdev);
		mca_log_err("get device fail\n");
	}
	spin_unlock_irqrestore(sdev->host->host_lock, flags);

	if (ret) {
		mca_log_err("get scsi fail\n");
		return ret;
	}
	sdev->host->eh_noresume = 1;

	// Fill in the CDB with SCSI command structure
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = WRITE_10; // Command
	cdb[1] = 0;
	cdb[2] = (lba >> 24) & 0xff; // LBA
	cdb[3] = (lba >> 16) & 0xff;
	cdb[4] = (lba >> 8) & 0xff;
	cdb[5] = (lba) & 0xff;
	cdb[6] = 0; // Group Number
	cdb[7] = (blocks >> 8) & 0xff; // Transfer Len
	cdb[8] = (blocks) & 0xff;
	cdb[9] = 0; // Control
	ret = scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_OUT, buf,
			       (blocks * PART_BLOCK_SIZE),
			       msecs_to_jiffies(15000), 3, &exec_args);
	if (ret)
		mca_log_err("write error %d\n", ret);

	scsi_device_put(sdev);
	sdev->host->eh_noresume = 0;
	return ret;
}

int charger_partition_alloc(u8 charger_partition_host_type,
			    u8 charger_partition_info_type, uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
		mca_log_err("charger partition not rdy, can't do rw!");
		return -1;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
		mca_log_err("charger_partition_host_type not support!");
		return -1;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
		mca_log_err("charger_partition_info_type not support!");
		return -1;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
		mca_log_err("read %u size not support!", size);
		return -1;
	}

	rw_buf = kzalloc(CHARGER_PARTITION_RWSIZE, GFP_KERNEL);
	if (!rw_buf) {
		mca_log_err("malloc buf error!\n");
		return -1;
	}

	/* check if avaliable */
	ret = charger_scsi_read_partition(
		charger_partition->sdev, rw_buf,
		(part_info.part_start + CHARGER_PARTITION_HEADER),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger read error %d\n", ret);
		kfree(rw_buf);
		return -1;
	}
	if (0 == ((charger_partition_header *)rw_buf)->avaliable) {
		mca_log_err("not avaliable, can't do rw now!\n");
		kfree(rw_buf);
		return -1;
	}
	((charger_partition_header *)rw_buf)->avaliable = 0;
	ret = charger_scsi_write_partition(
		charger_partition->sdev, (void *)rw_buf,
		(part_info.part_start + CHARGER_PARTITION_HEADER),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger write error %d\n", ret);
		kfree(rw_buf);
		return -1;
	}

	return ret;
}
EXPORT_SYMBOL(charger_partition_alloc);

int charger_partition_dealloc(u8 charger_partition_host_type,
			      u8 charger_partition_info_type, uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
		mca_log_err("charger partition not rdy, can't do rw!");
		return -1;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
		mca_log_err("charger_partition_host_type not support!");
		return -1;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
		mca_log_err("charger_partition_info_type not support!");
		return -1;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
		mca_log_err("read %u size not support!", size);
		return -1;
	}

	memset(rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_scsi_read_partition(
		charger_partition->sdev, rw_buf,
		(part_info.part_start + CHARGER_PARTITION_HEADER),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger read error %d\n", ret);
		kfree(rw_buf);
		return -1;
	}
	((charger_partition_header *)rw_buf)->avaliable = 1;
	ret = charger_scsi_write_partition(
		charger_partition->sdev, (void *)rw_buf,
		(part_info.part_start + CHARGER_PARTITION_HEADER),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger write error %d\n", ret);
		kfree(rw_buf);
		return -1;
	}

	kfree(rw_buf);
	return ret;
}
EXPORT_SYMBOL(charger_partition_dealloc);

void *charger_partition_read(u8 charger_partition_host_type,
			     u8 charger_partition_info_type, uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
		mca_log_err("charger partition not rdy, can't do read!");
		return NULL;
	}
	if (!rw_buf) {
		mca_log_err("rw_buf null, please alloc first!");
		return NULL;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
		mca_log_err("charger_partition_host_type not support!");
		return NULL;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
		mca_log_err("charger_partition_info_type not support!");
		return NULL;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
		mca_log_err("read %u size not support!", size);
		return NULL;
	}

	memset(rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_scsi_read_partition(
		charger_partition->sdev, rw_buf,
		(part_info.part_start + charger_partition_info_type),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger read error %d\n", ret);
		return NULL;
	}
	return rw_buf;
}
EXPORT_SYMBOL(charger_partition_read);

int charger_partition_write(u8 charger_partition_host_type,
			    u8 charger_partition_info_type, void *buf,
			    uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
		mca_log_err("charger partition not rdy, can't do read!");
		return -1;
	}
	if (!rw_buf) {
		mca_log_err("rw_buf null, please alloc first!");
		return -1;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
		mca_log_err("charger_partition_host_type not support!");
		return -1;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
		mca_log_err("charger_partition_info_type not support!");
		return -1;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
		mca_log_err("read %u size not support!", size);
		return -1;
	}

	memset(rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_scsi_read_partition(
		charger_partition->sdev, rw_buf,
		(part_info.part_start + charger_partition_info_type),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger read error %d\n", ret);
		return -1;
	}
	memcpy(rw_buf, buf, size);

	ret = charger_scsi_write_partition(
		charger_partition->sdev, rw_buf,
		(part_info.part_start + charger_partition_info_type),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger write error %d\n", ret);
		return -1;
	}

	return ret;
}
EXPORT_SYMBOL(charger_partition_write);

int charger_partition_get_eu_model(bool *is_eu_model)
{
	if (!charger_partition) {
		mca_log_err("charger_partition init error\n");
		return -1;
	}
	*is_eu_model = charger_partition->is_eu_model;
	return 0;
}
EXPORT_SYMBOL(charger_partition_get_eu_model);

static int charger_partition_read_info_1(charger_partition_info_1 *out)
{
	charger_partition_info_1 *info_1;
	int ret;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1,
				      sizeof(charger_partition_info_1));
	if (ret < 0) {
		mca_log_err("failed to alloc\n");
		return -1;
	}

	info_1 = (charger_partition_info_1 *)charger_partition_read(
		CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
		sizeof(charger_partition_info_1));
	if (info_1)
		*out = *info_1;
	else
		ret = -1;

	(void)charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
					CHARGER_PARTITION_INFO_1,
					sizeof(charger_partition_info_1));
	return ret;
}

static int charger_partition_write_info_1(const charger_partition_info_1 *in)
{
	int ret;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1,
				      sizeof(charger_partition_info_1));
	if (ret < 0) {
		mca_log_err("failed to alloc\n");
		return -1;
	}

	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1, (void *)in,
				      sizeof(charger_partition_info_1));
	if (ret < 0)
		mca_log_err("failed to write\n");

	(void)charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
					CHARGER_PARTITION_INFO_1,
					sizeof(charger_partition_info_1));
	return ret;
}

int charger_partition_read_double85(int *val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	*val = info_1.double85;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_double85);

int charger_partition_write_double85(int val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	info_1.double85 = val;
	return charger_partition_write_info_1(&info_1);
}
EXPORT_SYMBOL(charger_partition_write_double85);

int charger_partition_write_remove_temp_limit(int val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	info_1.remove_temp_limit = val;
	return charger_partition_write_info_1(&info_1);
}
EXPORT_SYMBOL(charger_partition_write_remove_temp_limit);

int charger_partition_read_remove_temp_limit(int *val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	*val = info_1.remove_temp_limit;
	return 0;
}

int charger_partition_read_memory_test(int *val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	*val = info_1.memory_test;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_memory_test);

int charger_partition_write_memory_test(int val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	info_1.memory_test = val;
	return charger_partition_write_info_1(&info_1);
}
EXPORT_SYMBOL(charger_partition_write_memory_test);

int charger_partition_read_soc_limit(int *val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	*val = info_1.soc_limit;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_soc_limit);

int charger_partition_write_soc_limit(int val)
{
	charger_partition_info_1 info_1 = { 0 };

	(void)charger_partition_read_info_1(&info_1);
	info_1.soc_limit = val;
	return charger_partition_write_info_1(&info_1);
}
EXPORT_SYMBOL(charger_partition_write_soc_limit);

int charger_partition_get_mishow(bool *mishow)
{
	charger_partition_info_1 info_1 = { 0 };

	if (!charger_partition) {
		mca_log_err("charger_partition init error\n");
		return -1;
	}

	(void)charger_partition_read_info_1(&info_1);
	*mishow = !!info_1.mishow;
	return 0;
}
EXPORT_SYMBOL(charger_partition_get_mishow);

static ssize_t charger_partition_sysfs_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf);
static ssize_t charger_partition_sysfs_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count);
static int charger_partition_get_info_2(void);
static int charger_partition_set_info_2(void);

struct mca_sysfs_attr_info charger_partition_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(charger_partition_sysfs, 0664,
			  MCA_PROP_CHARGER_PARTITION_MISHOW,
			  charger_partition_mishow),
	mca_sysfs_attr_rw(charger_partition_sysfs, 0664,
			  MCA_PROP_CHARGER_PARTITION_POWEROFFMODE,
			  charger_partition_poweroffmode),
	mca_sysfs_attr_rw(charger_partition_sysfs, 0664,
			  MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE,
			  charger_partition_prop_eu_mode),
};

#define CHARGER_PARTITION_SYSFS_ATTRS_SIZE \
	ARRAY_SIZE(charger_partition_sysfs_field_tbl)

static struct attribute
	*charger_partition_sysfs_attrs[CHARGER_PARTITION_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group charger_partition_sysfs_attr_group = {
	.attrs = charger_partition_sysfs_attrs,
};

static ssize_t charger_partition_sysfs_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	int val = 0, ret = 0;
	charger_partition_info_1 *info_1 = NULL;
	charger_partition_info_2 *info_2 = NULL;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  charger_partition_sysfs_field_tbl,
					  CHARGER_PARTITION_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_CHARGER_PARTITION_MISHOW:
		ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
					      CHARGER_PARTITION_INFO_1,
					      sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to alloc\n");
			return -1;
		}

		info_1 = (charger_partition_info_1 *)charger_partition_read(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (!info_1) {
			mca_log_err("failed to read\n");
			ret = charger_partition_dealloc(
				CHARGER_PARTITION_HOST_KERNEL,
				CHARGER_PARTITION_INFO_1,
				sizeof(charger_partition_info_1));
			if (ret < 0) {
				mca_log_err("failed to dealloc\n");
				return -1;
			}
			return -1;
		}
		val = info_1->mishow;

		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_CHARGER_PARTITION_POWEROFFMODE:
		ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
					      CHARGER_PARTITION_INFO_1,
					      sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to alloc\n");
			return -1;
		}

		info_1 = (charger_partition_info_1 *)charger_partition_read(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (!info_1) {
			mca_log_err("failed to read\n");
			ret = charger_partition_dealloc(
				CHARGER_PARTITION_HOST_KERNEL,
				CHARGER_PARTITION_INFO_1,
				sizeof(charger_partition_info_1));
			if (ret < 0) {
				mca_log_err("failed to dealloc\n");
				return -1;
			}
			return -1;
		}
		val = info_1->power_off_mode;

		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE:
		ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
					      CHARGER_PARTITION_INFO_2,
					      sizeof(charger_partition_info_2));
		if (ret < 0) {
			mca_log_err("failed to alloc\n");
			return -1;
		}

		info_2 = (charger_partition_info_2 *)charger_partition_read(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2,
			sizeof(charger_partition_info_2));
		if (!info_2) {
			mca_log_err("failed to read\n");
			ret = charger_partition_dealloc(
				CHARGER_PARTITION_HOST_KERNEL,
				CHARGER_PARTITION_INFO_2,
				sizeof(charger_partition_info_2));
			if (ret < 0) {
				mca_log_err(
					"[ChgPartition] failed to dealloc\n");
				return -1;
			}
			return -1;
		}
		val = info_2->eu_mode;

		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2,
			sizeof(charger_partition_info_2));
		if (ret < 0) {
			mca_log_err("[ChgPartition] failed to dealloc\n");
			return -1;
		}
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t charger_partition_sysfs_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	int val = 0, ret = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  charger_partition_sysfs_field_tbl,
					  CHARGER_PARTITION_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_CHARGER_PARTITION_MISHOW:
		ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
					      CHARGER_PARTITION_INFO_1,
					      sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to alloc\n");
			return -1;
		}

		charger_partition_info_1 info_1_a = { .power_off_mode = 2,
						      .zero_speed_mode = 2,
						      .mishow = val,
						      .reserved = 0 };

		ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
					      CHARGER_PARTITION_INFO_1,
					      (void *)&info_1_a,
					      sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to write\n");
			ret = charger_partition_dealloc(
				CHARGER_PARTITION_HOST_KERNEL,
				CHARGER_PARTITION_INFO_1,
				sizeof(charger_partition_info_1));
			if (ret < 0) {
				mca_log_err("failed to dealloc\n");
				return -1;
			}
			return -1;
		}

		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		break;
	case MCA_PROP_CHARGER_PARTITION_POWEROFFMODE:
		ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
					      CHARGER_PARTITION_INFO_1,
					      sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to alloc\n");
			return -1;
		}

		charger_partition_info_1 info_1_b = { .power_off_mode = val,
						      .zero_speed_mode = 2,
						      .mishow = 0x34567890,
						      .reserved = 0 };

		ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
					      CHARGER_PARTITION_INFO_1,
					      (void *)&info_1_b,
					      sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to write\n");
			ret = charger_partition_dealloc(
				CHARGER_PARTITION_HOST_KERNEL,
				CHARGER_PARTITION_INFO_1,
				sizeof(charger_partition_info_1));
			if (ret < 0) {
				mca_log_err("failed to dealloc\n");
				return -1;
			}
			return -1;
		}

		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		break;
	case MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE:
		charger_partition_get_info_2();
		charger_partition->info_2.eu_mode = val;
		charger_partition_set_info_2();
		break;
	default:
		break;
	}
	return count;
}

static int charger_partition_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(charger_partition_sysfs_attrs,
			     charger_partition_sysfs_field_tbl,
			     CHARGER_PARTITION_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group("charger", "charger_partition", dev,
					   &charger_partition_sysfs_attr_group);
}

static void charger_partition_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("charger", "charger_partition", dev,
				    &charger_partition_sysfs_attr_group);
}

static bool look_up_scsi_device(int lun)
{
	struct Scsi_Host *shost;

	shost = scsi_host_lookup(0);
	if (!shost)
		return false;

	charger_partition->sdev = scsi_device_lookup(shost, 0, 0, lun);
	if (!charger_partition->sdev)
		return false;

	scsi_host_put(shost);
	return true;
}

static bool check_device_is_correct(void)
{
	if (strncmp(charger_partition->sdev->host->hostt->proc_name, UFSHCD,
		    strlen(UFSHCD))) {
		/*check if the device is ufs. If not, just return directly.*/
		mca_log_err("proc name is not ufshcd, name: %s\n",
			    charger_partition->sdev->host->hostt->proc_name);
		return false;
	}

	return true;
}

static bool get_charger_partition_info(void)
{
	struct scsi_disk *sdkp = NULL;
	struct scsi_device *sdev = charger_partition->sdev;
	int part_number = charger_partition->part_info_part_number;
	struct block_device *part;

	if (!sdev->sdev_gendev.driver_data) {
		mca_log_err("scsi disk is null\n");
		return false;
	}

	sdkp = (struct scsi_disk *)sdev->sdev_gendev.driver_data;
	if (!sdkp->disk) {
		mca_log_err("gendisk is null\n");
		return false;
	}

	charger_partition->part_info_part_name = sdkp->disk->disk_name;
	mca_log_err("partion: %s part_number:%d\n",
		    charger_partition->part_info_part_name, part_number);

	part = xa_load(&sdkp->disk->part_tbl, part_number);
	if (!part) {
		mca_log_err("device is null\n");
		return false;
	}

	if (!part->bd_meta_info) {
		mca_log_err("part_num %d has no meta_info, skip\n",
			    part_number);
		return false;
	}

	if (strncmp(part->bd_meta_info->volname, "charger",
		    sizeof("charger"))) {
		mca_log_err("[charger] this is not lun0\n");
		return false;
	}

	part_info.part_start =
		part->bd_start_sect * PART_SECTOR_SIZE / PART_BLOCK_SIZE;
	part_info.part_size =
		bdev_nr_sectors(part) * PART_SECTOR_SIZE / PART_BLOCK_SIZE;

	mca_log_err("partion: %s start %llu(block) size %llu(block)\n",
		    charger_partition->part_info_part_name,
		    part_info.part_start, part_info.part_size);
	return true;
}

int check_charger_partition_header(void)
{
	int ret = 0;
	charger_partition_header *header = NULL;

	header = kzalloc(CHARGER_PARTITION_RWSIZE, GFP_KERNEL);
	if (!header) {
		mca_log_err("malloc buf error!\n");
		return -1;
	}

	ret = charger_scsi_read_partition(
		charger_partition->sdev, (void *)header,
		(part_info.part_start + CHARGER_PARTITION_HEADER),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger read error %d\n", ret);
		kfree(header);
		return -1;
	}
	mca_log_err("magic:0x%0x, version:%d\n", header->magic,
		    header->version);

	if (header->magic != CHARGER_PARTITION_MAGIC) {
		mca_log_err("magic error, set to default!\n");
		header->magic = CHARGER_PARTITION_MAGIC;
	}
	header->initialized = 1;
	header->avaliable = 1;

	ret = charger_scsi_write_partition(
		charger_partition->sdev, (void *)header,
		(part_info.part_start + CHARGER_PARTITION_HEADER),
		CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
		mca_log_err("charger write error %d\n", ret);
		kfree(header);
		return -1;
	}
	mca_log_err("initiablized ok\n");

	kfree(header);
	return ret;
}

static int charger_partition_get_info_2(void)
{
	int ret = 0;
	charger_partition_info_2 *info_2 = NULL;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2,
				      sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("failed to alloc\n");
		return -1;
	}

	info_2 = (charger_partition_info_2 *)charger_partition_read(
		CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2,
		sizeof(charger_partition_info_2));
	if (!info_2) {
		mca_log_err("failed to read\n");
		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2,
			sizeof(charger_partition_info_2));
		if (ret < 0) {
			mca_log_err("[ChgPartition] failed to dealloc\n");
			return -1;
		}
		return -1;
	}
	charger_partition->info_2 = *info_2;
	charger_partition->is_eu_model = info_2->eu_mode;
	mca_log_err(" ret: %d, info_2->eu_mode: %u\n", ret, info_2->eu_mode);

	if (info_2->eu_mode == 1) {
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG,
					  MCA_EVENT_IS_EU_MODEL,
					  info_2->eu_mode);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					  MCA_EVENT_IS_EU_MODEL,
					  info_2->eu_mode);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					  MCA_EVENT_IS_EU_MODEL,
					  info_2->eu_mode);
	}

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
					CHARGER_PARTITION_INFO_2,
					sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("[ChgPartition] failed to dealloc\n");
		return -1;
	}
	return 0;
}

static int charger_partition_set_info_2(void)
{
	int ret = 0;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2,
				      sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("failed to alloc\n");
		return -1;
	}

	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2,
				      (void *)&charger_partition->info_2,
				      sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("failed to write\n");
		ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
						CHARGER_PARTITION_INFO_2,
						sizeof(charger_partition_info_2));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		return -1;
	}

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
					CHARGER_PARTITION_INFO_2,
					sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("failed to dealloc\n");
		return -1;
	}
	return 0;
}

int charger_partition_read_ocd_count(u32 *ocd0, u32 *ocd1)
{
	charger_partition_get_info_2();
	*ocd0 = charger_partition->info_2.ocd_count[0];
	*ocd1 = charger_partition->info_2.ocd_count[1];
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_ocd_count);

int charger_partition_write_ocd_count(u32 ocd0, u32 ocd1)
{
	charger_partition_get_info_2();
	charger_partition->info_2.ocd_count[0] = ocd0;
	charger_partition->info_2.ocd_count[1] = ocd1;
	charger_partition_set_info_2();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_ocd_count);

int charger_partition_read_cuv_count(u32 *cuv0, u32 *cuv1)
{
	charger_partition_get_info_2();
	*cuv0 = charger_partition->info_2.cuv_count[0];
	*cuv1 = charger_partition->info_2.cuv_count[1];
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_cuv_count);

int charger_partition_write_cuv_count(u32 cuv0, u32 cuv1)
{
	charger_partition_get_info_2();
	charger_partition->info_2.cuv_count[0] = cuv0;
	charger_partition->info_2.cuv_count[1] = cuv1;
	charger_partition_set_info_2();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_cuv_count);

int charger_partition_read_hscd_count(u32 *hscd)
{
	charger_partition_get_info_2();
	*hscd = charger_partition->info_2.hscd_count;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_hscd_count);

int charger_partition_write_hcsd_count(u32 hscd)
{
	charger_partition_get_info_2();
	charger_partition->info_2.hscd_count = hscd;
	charger_partition_set_info_2();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_hcsd_count);

static int charger_partition_get_info_1(void)
{
	int ret = 0;
	charger_partition_info_1 *info_1 = NULL;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1,
				      sizeof(charger_partition_info_1));
	if (ret < 0) {
		mca_log_err("failed to alloc\n");
		return -1;
	}

	info_1 = (charger_partition_info_1 *)charger_partition_read(
		CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
		sizeof(charger_partition_info_1));
	if (!info_1) {
		mca_log_err("failed to read\n");
		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		return -1;
	}
	mca_log_err(
		"ret: %d, mishow: 0x%0x, zero_speed_mode: %u, power_off_mode: %u, double85: %u, remove_temp_limit: %u, memory_test: %u, soc_limit: %d\n",
		ret, info_1->mishow, info_1->zero_speed_mode,
		info_1->power_off_mode, info_1->double85,
		info_1->remove_temp_limit, info_1->memory_test,
		info_1->soc_limit);

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
					CHARGER_PARTITION_INFO_1,
					sizeof(charger_partition_info_1));
	if (ret < 0) {
		mca_log_err("failed to dealloc\n");
		return -1;
	}
	return 0;
}

static int charger_partition_set_info_1(void)
{
	int ret = 0;
	charger_partition_info_1 info_1 = { .power_off_mode = 2,
					    .zero_speed_mode = 2,
					    .mishow = 0x23456789,
					    .reserved = 0 };

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1,
				      sizeof(charger_partition_info_1));
	if (ret < 0) {
		mca_log_err("failed to alloc\n");
		return -1;
	}

	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1, (void *)&info_1,
				      sizeof(charger_partition_info_1));
	if (ret < 0) {
		mca_log_err("failed to write\n");
		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1,
			sizeof(charger_partition_info_1));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		return -1;
	}

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
					CHARGER_PARTITION_INFO_1,
					sizeof(charger_partition_info_1));
	if (ret < 0) {
		mca_log_err("failed to dealloc\n");
		return -1;
	}
	return 0;
}

static void charger_partition_prepare(void)
{
	charger_partition_info_1 info_1 = { 0 };

	charger_partition_get_info_1();
	charger_partition_set_info_1();
	charger_partition_get_info_1();

	(void)charger_partition_read_info_1(&info_1);
	if (info_1.double85)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO, MCA_EVENT_DEBUG_CTRL_DOUBLE85, &info_1.double85);
	if (info_1.remove_temp_limit)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO, MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT, &info_1.remove_temp_limit);
	if (info_1.memory_test)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO, MCA_EVENT_DEBUG_CTRL_MEMORY_TEST, &info_1.memory_test);
	if (info_1.soc_limit)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO, MCA_EVENT_DEBUG_CTRL_SOC_LIMIT, &info_1.soc_limit);

	charger_partition_get_info_2();
}

static void charger_partition_work(struct work_struct *work)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int lun = 0;
	static int retry;

	mca_log_err("get hw_country_ver: %u\n", get_hw_country_version());
	charger_partition->part_info_part_number =
		(hwid && hwid->platform_version == CHARGER_PART_PLATFORM_V9) ?
			CHARGER_PART_NUMBER_V9 :
			CHARGER_PART_NUMBER_DEFAULT;

	// 1. find charger partition
	for (lun = 0; lun < 6; lun++) {
		/*find charger partition scsi device*/
		if (!look_up_scsi_device(lun)) {
			mca_log_err("not find, continue...\n");
			continue;
		}

		/*check device is correct*/
		if (!check_device_is_correct()) {
			mca_log_err(
				"not find finally, won't read charger partition!!!\n");
			return;
		}

		if (get_charger_partition_info()) {
			mca_log_err("get partition info ok\n");
			check_charger_partition_header();
			charger_partition->is_charger_partition_rdy = true;
			/*get info_2: is_eu_model*/
			if (charger_partition->not_notify_module) {
				charger_partition_get_info_2();
				charger_partition->not_notify_module = false;
				charger_partition->is_charger_partition_rdy =
					false;
			} else {
				/*reset info_1 + publish debug-control flags*/
				charger_partition_prepare();
			}
			break;
		}
	}

	if (!charger_partition->is_charger_partition_rdy &&
	    retry++ < CHARGER_PARTITION_RETRY_TIMES) {
		mca_log_err("charger partition not ready, retry: %d/%d\n",
			    retry, CHARGER_PARTITION_RETRY_TIMES);
		schedule_delayed_work(
			&charger_partition->charger_partition_work,
			msecs_to_jiffies(CHARGER_WORK_DELAY_MS));
	}
	if (retry == CHARGER_PARTITION_RETRY_TIMES) {
		if (look_up_scsi_device(0)) {
			if (check_device_is_correct()) {
				if (get_charger_partition_info()) {
					mca_log_err("get partition info ok\n");
					check_charger_partition_header();
					charger_partition
						->is_charger_partition_rdy =
						true;
					/*get info_2: is_eu_model*/
					if (charger_partition
						    ->not_notify_module) {
						charger_partition_get_info_2();
						charger_partition
							->not_notify_module =
							false;
						charger_partition
							->is_charger_partition_rdy =
							false;
					} else {
						/*reset info_1: power_off_mode and zero_speed_mode*/
						charger_partition_get_info_1();
						charger_partition_set_info_1();
						charger_partition_get_info_1();
						charger_partition_get_info_2();
					}
				}
			}
		}
	}
}

static int charger_partition_probe(struct platform_device *pdev)
{
	charger_partition = devm_kzalloc(
		&pdev->dev, sizeof(struct ChargerPartition), GFP_KERNEL);
	if (!charger_partition) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}
	charger_partition->dev = &pdev->dev;
	platform_set_drvdata(pdev, charger_partition);

	charger_partition_sysfs_create_group(charger_partition->dev);

	charger_partition->not_notify_module = true;
	INIT_DELAYED_WORK(&charger_partition->charger_partition_work,
			  charger_partition_work);
	schedule_delayed_work(&charger_partition->charger_partition_work, 0);
	mca_log_err("probe ok\n");
	return 0;
}

static int charger_partition_remove(struct platform_device *pdev)
{
	cancel_delayed_work(&charger_partition->charger_partition_work);
	charger_partition_sysfs_remove_group(&pdev->dev);
	kfree(charger_partition);
	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "xiaomi,charger_partition" },
	{},
};

static struct platform_driver charger_partition_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "charger_partition",
		.of_match_table = match_table,
	},
	.probe = charger_partition_probe,
	.remove = charger_partition_remove,
};

static int __init charger_partition_init(void)
{
	return platform_driver_register(&charger_partition_driver);
}
module_init(charger_partition_init);

static void __exit charger_partition_exit(void)
{
	platform_driver_unregister(&charger_partition_driver);
}
module_exit(charger_partition_exit);

MODULE_DESCRIPTION("smart charge driver");
MODULE_AUTHOR("liluting@xiaomi.com");
MODULE_LICENSE("GPL v2");
