// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim DS28E30 battery authentication chip on the MCA onewire core.
 *
 * Commands go out as extended commands: 66h, a length, the sub-command and
 * its parameters, checked by an inverted CRC16 that the chip sends back.
 * After a release byte the chip runs the command with the bus driven high
 * to power it, then answers with a length, a result byte and the data.
 *
 * Authentication checks the certificate of the chip key, signed by the
 * Xiaomi system key over the key, rom id and man id, then has the chip sign
 * page 0 together with a random challenge.
 *
 * Written by sidharthify (Sidharth Sharma) <wednisegit@gmail.com>
 */

#include <linux/crc16.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <mca/common/mca_log.h>
#include <mca/hardware/onewire_auth.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "maxim_ds28e30"
#endif

#define DS_ROM_READ			0x33
#define DS_ROM_SKIP			0xcc
#define DS_XPC				0x66
#define DS_RELEASE			0xaa

#define DS_CMD_WRITE_MEM		0x96
#define DS_CMD_READ_MEM			0x44
#define DS_CMD_READ_STATUS		0xaa
#define DS_CMD_COMP_READ_AUTH		0xa5
#define DS_CMD_DECREMENT_CNT		0xc9

#define DS_RESULT_OK			0xaa
#define DS_RESULT_DISABLED		0x88
#define DS_CRC16_RESIDUE		0xb001

#define DS_PAGE_CERT_R			4
#define DS_PAGE_CERT_S			5
#define DS_PAGE_PUB_X			28
#define DS_PAGE_PUB_Y			29
#define DS_PAGE_COUNTER			106
#define DS_STATUS_MANID			0x80

#define DS_FAMILY_CODE			0xdb
#define DS_CID				0x04
#define DS_CID_MIN			0xf0
#define DS_MANID_LSB			0xd4
#define DS_MANID_MSB			0x00

#define DS_ROMID_LEN			8
#define DS_MANID_LEN			2
#define DS_KEY_LEN			32
#define DS_CERT_CONST_LEN		16
#define DS_FRAME_MAX			80
#define DS_RETRY			5

#define DS_DELAY_READ_MS		50
#define DS_DELAY_WRITE_MS		100
#define DS_DELAY_DECREMENT_MS		150
#define DS_DELAY_ECDSA_MS		200

struct ds28e30_info {
	struct device *dev;
	struct onewire_auth_chip chip;
	raw_spinlock_t lock;
	u8 romid[DS_ROMID_LEN];
	u8 manid[DS_MANID_LEN];
};

static const u8 ds_cert_const[DS_CERT_CONST_LEN] = {
	0x25, 0x1c, 0x51, 0x79, 0x91, 0x92, 0x67, 0x58,
	0xaf, 0xca, 0xd5, 0x44, 0x22, 0xa2, 0x98, 0xb9,
};

/* Xiaomi system key of the chip certificates */
static const u8 ds_system_key_x[DS_KEY_LEN] = {
	0xe4, 0xdd, 0xab, 0x00, 0x57, 0xa2, 0x11, 0xbc,
	0x4e, 0x7f, 0xe1, 0x70, 0xf9, 0xb0, 0x98, 0xb3,
	0x76, 0xe9, 0x84, 0x7b, 0xee, 0x7f, 0x69, 0xcf,
	0x98, 0xc8, 0xbb, 0xd7, 0xc1, 0x22, 0xb9, 0xa3,
};

static const u8 ds_system_key_y[DS_KEY_LEN] = {
	0x3a, 0xd6, 0xee, 0x23, 0x40, 0x12, 0xb9, 0x9e,
	0x61, 0x1c, 0x73, 0xc6, 0xed, 0x94, 0x47, 0x71,
	0x2a, 0x66, 0x7a, 0x9d, 0x1a, 0xac, 0x07, 0xc4,
	0xf9, 0x27, 0xcb, 0xf4, 0xfb, 0x22, 0x41, 0x3c,
};

/* the bus idles driven high, which also powers the chip */
static int ds_reset(struct ds28e30_info *info)
{
	struct gpio_desc *gpiod = info->chip.gpiod;
	unsigned long flags;
	bool presence = false;
	int i;

	raw_spin_lock_irqsave(&info->lock, flags);
	gpiod_direction_output(gpiod, 0);
	udelay(54);
	gpiod_direction_input(gpiod);
	/* let the bus rise, then look for the presence pulse */
	for (i = 0; i < 4 && !gpiod_get_raw_value(gpiod); i++)
		udelay(1);
	for (i = 0; i < 20; i++) {
		if (!gpiod_get_raw_value(gpiod)) {
			presence = true;
			break;
		}
		udelay(1);
	}
	udelay(50);
	gpiod_direction_output(gpiod, 1);
	raw_spin_unlock_irqrestore(&info->lock, flags);

	return presence ? 0 : -ENODEV;
}

static void ds_write_byte(struct ds28e30_info *info, u8 val)
{
	struct gpio_desc *gpiod = info->chip.gpiod;
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&info->lock, flags);
	for (i = 0; i < 8; i++, val >>= 1) {
		gpiod_set_raw_value(gpiod, 0);
		ndelay(500);
		if (val & 1)
			gpiod_set_raw_value(gpiod, 1);
		udelay(7);
		gpiod_set_raw_value(gpiod, 1);
		udelay(9);
	}
	raw_spin_unlock_irqrestore(&info->lock, flags);
}

static u8 ds_read_byte(struct ds28e30_info *info)
{
	struct gpio_desc *gpiod = info->chip.gpiod;
	unsigned long flags;
	u8 val = 0;
	int i;

	raw_spin_lock_irqsave(&info->lock, flags);
	for (i = 0; i < 8; i++) {
		gpiod_set_raw_value(gpiod, 0);
		ndelay(300);
		gpiod_direction_input(gpiod);
		if (gpiod_get_raw_value(gpiod))
			val |= BIT(i);
		udelay(5);
		gpiod_direction_output(gpiod, 1);
		udelay(9);
	}
	raw_spin_unlock_irqrestore(&info->lock, flags);

	return val;
}

static int ds_read_rom(struct ds28e30_info *info, u8 *romid)
{
	int ret, i;

	ret = ds_reset(info);
	if (ret)
		return ret;

	ds_write_byte(info, DS_ROM_READ);
	udelay(10);
	for (i = 0; i < DS_ROMID_LEN; i++)
		romid[i] = ds_read_byte(info);

	if (onewire_auth_crc8(romid, DS_ROMID_LEN - 1) != romid[DS_ROMID_LEN - 1])
		return -EIO;

	return 0;
}

/*
 * Runs one extended command. @cmd holds the sub-command and its parameters,
 * @rsp receives the result byte and @rsp_len - 1 bytes of data.
 */
static int ds_command_once(struct ds28e30_info *info, const u8 *cmd, int len,
			   unsigned int delay_ms, u8 *rsp, int rsp_len)
{
	u8 frame[DS_FRAME_MAX + 4], buf[DS_FRAME_MAX + 3];
	int ret, i, n;

	ret = ds_reset(info);
	if (ret)
		return ret;

	ds_write_byte(info, DS_ROM_SKIP);

	frame[0] = DS_XPC;
	frame[1] = len;
	memcpy(&frame[2], cmd, len);
	for (i = 0; i < len + 2; i++)
		ds_write_byte(info, frame[i]);
	frame[len + 2] = ds_read_byte(info);
	frame[len + 3] = ds_read_byte(info);
	if (crc16(0, frame, len + 4) != DS_CRC16_RESIDUE)
		return -EBADMSG;

	ds_write_byte(info, DS_RELEASE);
	msleep(delay_ms);

	ds_read_byte(info);
	n = ds_read_byte(info);
	if (n != rsp_len)
		return n == 0xff ? -ENODEV : -EPROTO;

	buf[0] = n;
	for (i = 1; i < n + 3; i++)
		buf[i] = ds_read_byte(info);
	if (crc16(0, buf, n + 3) != DS_CRC16_RESIDUE)
		return -EBADMSG;

	memcpy(rsp, &buf[1], n);

	return 0;
}

static int ds_command(struct ds28e30_info *info, const u8 *cmd, int len,
		      unsigned int delay_ms, u8 *rsp, int rsp_len)
{
	int i, ret = -EIO;

	if (len > DS_FRAME_MAX || rsp_len > DS_FRAME_MAX)
		return -EINVAL;

	for (i = 0; i < DS_RETRY; i++) {
		ret = ds_command_once(info, cmd, len, delay_ms, rsp, rsp_len);
		if (!ret)
			return rsp[0] == DS_RESULT_OK ? 0 : -EIO;
	}

	mca_log_info("command %#x failed: %d\n", cmd[0], ret);
	return ret;
}

static int ds_read_status_manid(struct ds28e30_info *info, u8 *manid)
{
	const u8 cmd[] = { DS_CMD_READ_STATUS, DS_STATUS_MANID };
	u8 rsp[5] = { 0 };
	int ret;

	ret = ds_command(info, cmd, sizeof(cmd), DS_DELAY_READ_MS,
			 rsp, sizeof(rsp));
	if (ret && rsp[0] != DS_RESULT_DISABLED)
		return ret;

	memcpy(manid, &rsp[1], DS_MANID_LEN);

	return 0;
}

static int ds_read_page(void *data, int page, u8 *buf)
{
	const u8 cmd[] = { DS_CMD_READ_MEM, page };
	u8 rsp[1 + ONEWIRE_PAGE_SIZE];
	int ret;

	ret = ds_command(data, cmd, sizeof(cmd), DS_DELAY_READ_MS,
			 rsp, sizeof(rsp));
	if (!ret)
		memcpy(buf, &rsp[1], ONEWIRE_PAGE_SIZE);

	return ret;
}

static int ds_write_page(void *data, int page, const u8 *buf)
{
	u8 cmd[2 + ONEWIRE_PAGE_SIZE] = { DS_CMD_WRITE_MEM, page };
	u8 rsp[1];

	memcpy(&cmd[2], buf, ONEWIRE_PAGE_SIZE);

	return ds_command(data, cmd, sizeof(cmd), DS_DELAY_WRITE_MS,
			  rsp, sizeof(rsp));
}

/* the counter page holds the count in its first three bytes, LSB first */
static int ds_read_counter(void *data, u32 *val)
{
	u8 page[ONEWIRE_PAGE_SIZE];
	int ret;

	ret = ds_read_page(data, DS_PAGE_COUNTER, page);
	if (ret)
		return ret;

	*val = page[2] << 16 | page[1] << 8 | page[0];

	return 0;
}

static int ds_decrement_counter(void *data)
{
	const u8 cmd[] = { DS_CMD_DECREMENT_CNT };
	u8 rsp[1];

	return ds_command(data, cmd, sizeof(cmd), DS_DELAY_DECREMENT_MS,
			  rsp, sizeof(rsp));
}

/*
 * Reads the rom id and man id. A chip that powers up for the first time
 * reports an empty rom id until its status has been read once.
 */
static int ds_read_ids(struct ds28e30_info *info)
{
	u8 manid[DS_MANID_LEN];
	int ret, i;

	ret = ds_read_rom(info, info->romid);
	if (ret)
		return ret;

	if (!memchr_inv(&info->romid[1], 0, 6)) {
		ds_read_status_manid(info, manid);
		ret = ds_read_rom(info, info->romid);
		if (ret)
			return ret;
	}

	ret = ds_read_status_manid(info, info->manid);
	if (ret)
		return ret;

	for (i = 0; i < DS_ROMID_LEN; i++)
		if (info->romid[i])
			return 0;

	return -ENODEV;
}

static bool ds_ids_match(struct ds28e30_info *info)
{
	return info->romid[0] == DS_FAMILY_CODE &&
	       info->romid[6] == DS_CID &&
	       (info->romid[5] & 0xf0) == DS_CID_MIN &&
	       info->manid[0] == DS_MANID_LSB &&
	       info->manid[1] == DS_MANID_MSB;
}

static int ds_check_chip(void *data)
{
	struct ds28e30_info *info = data;
	int i;

	for (i = 0; i < DS_RETRY; i++) {
		if (!ds_read_ids(info) && ds_ids_match(info))
			return 0;
	}

	return -ENODEV;
}

static int ds_authenticate(void *data)
{
	struct ds28e30_info *info = data;
	u8 page0[ONEWIRE_PAGE_SIZE], challenge[DS_KEY_LEN];
	u8 pub_x[DS_KEY_LEN], pub_y[DS_KEY_LEN];
	u8 cert_r[DS_KEY_LEN], cert_s[DS_KEY_LEN];
	u8 cmd[3 + DS_KEY_LEN], rsp[1 + 2 * DS_KEY_LEN];
	u8 msg[2 * DS_KEY_LEN + DS_CERT_CONST_LEN + DS_ROMID_LEN +
	       DS_MANID_LEN];
	int ret, n;

	ret = ds_check_chip(info);
	if (ret)
		return ret;

	ret = ds_read_page(info, 0, page0) ?:
	      ds_read_page(info, DS_PAGE_PUB_X, pub_x) ?:
	      ds_read_page(info, DS_PAGE_PUB_Y, pub_y) ?:
	      ds_read_page(info, DS_PAGE_CERT_R, cert_r) ?:
	      ds_read_page(info, DS_PAGE_CERT_S, cert_s);
	if (ret)
		return ret;

	/* the chip signs rom id | page 0 | challenge | page | man id */
	get_random_bytes(challenge, sizeof(challenge));
	cmd[0] = DS_CMD_COMP_READ_AUTH;
	cmd[1] = 0;
	cmd[2] = 0x03;
	memcpy(&cmd[3], challenge, sizeof(challenge));
	ret = ds_command(info, cmd, sizeof(cmd), DS_DELAY_ECDSA_MS,
			 rsp, sizeof(rsp));
	if (ret)
		return ret;

	n = 0;
	memcpy(&msg[n], info->romid, DS_ROMID_LEN);
	n += DS_ROMID_LEN;
	memcpy(&msg[n], page0, ONEWIRE_PAGE_SIZE);
	n += ONEWIRE_PAGE_SIZE;
	memcpy(&msg[n], challenge, sizeof(challenge));
	n += sizeof(challenge);
	msg[n++] = 0;
	memcpy(&msg[n], info->manid, DS_MANID_LEN);
	n += DS_MANID_LEN;
	/* the signature comes as s, then r */
	ret = onewire_auth_ecdsa_verify(pub_x, pub_y, msg, n,
					&rsp[1 + DS_KEY_LEN], &rsp[1]);
	if (ret) {
		mca_log_err("page signature rejected: %d\n", ret);
		return ret;
	}

	/* the certificate signs chip key | constant | rom id | man id */
	n = 0;
	memcpy(&msg[n], pub_x, DS_KEY_LEN);
	n += DS_KEY_LEN;
	memcpy(&msg[n], pub_y, DS_KEY_LEN);
	n += DS_KEY_LEN;
	memcpy(&msg[n], ds_cert_const, DS_CERT_CONST_LEN);
	n += DS_CERT_CONST_LEN;
	memcpy(&msg[n], info->romid, DS_ROMID_LEN);
	n += DS_ROMID_LEN;
	memcpy(&msg[n], info->manid, DS_MANID_LEN);
	n += DS_MANID_LEN;
	ret = onewire_auth_ecdsa_verify(ds_system_key_x, ds_system_key_y,
					msg, n, cert_r, cert_s);
	if (ret)
		mca_log_err("chip certificate rejected: %d\n", ret);

	return ret;
}

static const struct onewire_auth_ops ds_ops = {
	.check_chip = ds_check_chip,
	.authenticate = ds_authenticate,
	.read_page = ds_read_page,
	.write_page = ds_write_page,
	.read_counter = ds_read_counter,
	.decrement_counter = ds_decrement_counter,
};

static int ds28e30_probe(struct platform_device *pdev)
{
	struct ds28e30_info *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	raw_spin_lock_init(&info->lock);
	info->chip.name = "maxim_ds28e30";
	info->chip.ops = &ds_ops;
	info->chip.data = info;
	platform_set_drvdata(pdev, info);

	ret = onewire_auth_add_chip(&info->chip);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register with the onewire core\n");

	return 0;
}

static int ds28e30_remove(struct platform_device *pdev)
{
	struct ds28e30_info *info = platform_get_drvdata(pdev);

	onewire_auth_remove_chip(&info->chip);

	return 0;
}

static const struct of_device_id ds28e30_of_match[] = {
	{ .compatible = "mca,maxim-ds28e30" },
	{ }
};
MODULE_DEVICE_TABLE(of, ds28e30_of_match);

static struct platform_driver ds28e30_driver = {
	.driver = {
		.name = "maxim_ds28e30",
		.of_match_table = ds28e30_of_match,
	},
	.probe = ds28e30_probe,
	.remove = ds28e30_remove,
};
module_platform_driver(ds28e30_driver);

MODULE_DESCRIPTION("Maxim DS28E30 battery authentication chip");
MODULE_LICENSE("GPL");
