// SPDX-License-Identifier: GPL-2.0
/*
 * ACL16 SLG2 battery authentication chip on the MCA onewire core.
 *
 * The chip talks a framed protocol over a bit-banged onewire bus: a command
 * byte, a payload length, the payload and an inverted CRC16, answered by a
 * length, a status byte, the data and another CRC16. It comes up in a fast
 * bit timing and gets moved to a slower one on the first reset.
 *
 * Authentication checks the certificate of the chip key against the Xiaomi
 * root key, then has the chip sign page 0 together with a random challenge.
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
#define MCA_LOG_TAG "acl16_slg2"
#endif

#define SLG2_ROM_READ			0x33
#define SLG2_ROM_SKIP			0xcc
#define SLG2_BAUDRATE_CFG		0xbe

#define SLG2_CMD_ECDSA_SIGN		0x2a
#define SLG2_CMD_DC_DECREASE		0x24
#define SLG2_CMD_ROMID_MANID		0xb2
#define SLG2_CMD_DC_GET			0xc0
#define SLG2_CMD_MEM_READ		0xca
#define SLG2_CMD_MEM_WRITE		0xda

#define SLG2_STATUS_OK			0xaa
#define SLG2_STATUS_CRC_ERROR		0x22

#define SLG2_FAMILY_CODE		0x8c
#define SLG2_CID			0x04
#define SLG2_CID_MIN			0xf0

#define SLG2_PAGE_CERT_R		0x13
#define SLG2_PAGE_CERT_S		0x14
#define SLG2_PAGE_PUB_X			0x15
#define SLG2_PAGE_PUB_Y			0x16

#define SLG2_ROMID_LEN			8
#define SLG2_MANID_LEN			2
#define SLG2_KEY_LEN			32
#define SLG2_FRAME_MAX			128
#define SLG2_CMD_RETRY			7
#define SLG2_RESET_RETRY		3
#define SLG2_CHECK_RETRY		5
#define SLG2_AUTH_RETRY			7
#define SLG2_BAUDRATE_RETRY		5

/* bit timings, the chip starts in the fastest one */
enum slg2_speed {
	SLG2_SPEED_FAST,
	SLG2_SPEED_MEDIUM,
	SLG2_SPEED_SLOW,
};

struct slg2_timing {
	unsigned int write0_low_us;
	unsigned int write0_rec_us;
	unsigned int write1_slot_us;
	unsigned int read_sample_us;
	unsigned int read_slot_us;
};

static const struct slg2_timing slg2_timings[] = {
	[SLG2_SPEED_FAST]	= { 8, 7, 15, 0, 15 },
	[SLG2_SPEED_MEDIUM]	= { 18, 14, 31, 1, 31 },
	[SLG2_SPEED_SLOW]	= { 32, 15, 47, 0, 46 },
};

struct slg2_info {
	struct device *dev;
	struct onewire_auth_chip chip;
	raw_spinlock_t lock;
	enum slg2_speed speed;
	/* the rom id was read once, from then on the rom is skipped */
	bool rom_known;
};

/* Xiaomi root key of the chip certificates, x then y */
static const u8 slg2_root_key[2 * SLG2_KEY_LEN] = {
	0x29, 0x02, 0x70, 0xd3, 0x01, 0x7a, 0x22, 0x39,
	0x94, 0x8e, 0xfc, 0xb2, 0xfd, 0xd4, 0xbe, 0xf6,
	0x10, 0xe9, 0xbc, 0x20, 0x5a, 0x8f, 0x47, 0xfb,
	0xa6, 0x9c, 0x1e, 0x84, 0x4f, 0x70, 0x9b, 0x21,
	0xde, 0x66, 0x51, 0x5f, 0x2d, 0x14, 0xa7, 0x72,
	0xd1, 0xef, 0x27, 0x43, 0x64, 0x6e, 0x20, 0xbc,
	0xdd, 0xd2, 0x69, 0xc6, 0x1c, 0x67, 0x52, 0xd8,
	0xb6, 0xdb, 0x66, 0x66, 0x47, 0x15, 0xb2, 0x4a,
};

static int slg2_config_baudrate(struct slg2_info *info, enum slg2_speed speed);

static void slg2_write_byte(struct slg2_info *info, u8 val)
{
	const struct slg2_timing *t = &slg2_timings[info->speed];
	struct gpio_desc *gpiod = info->chip.gpiod;
	unsigned long flags;
	int i;

	for (i = 0; i < 8; i++, val >>= 1) {
		raw_spin_lock_irqsave(&info->lock, flags);
		gpiod_direction_output(gpiod, 0);
		if (val & 1) {
			gpiod_direction_input(gpiod);
			raw_spin_unlock_irqrestore(&info->lock, flags);
			udelay(t->write1_slot_us);
		} else {
			udelay(t->write0_low_us);
			gpiod_direction_output(gpiod, 1);
			raw_spin_unlock_irqrestore(&info->lock, flags);
			udelay(t->write0_rec_us);
		}
	}
}

static u8 slg2_read_byte(struct slg2_info *info)
{
	const struct slg2_timing *t = &slg2_timings[info->speed];
	struct gpio_desc *gpiod = info->chip.gpiod;
	unsigned long flags;
	u8 val = 0;
	int i;

	for (i = 0; i < 8; i++) {
		raw_spin_lock_irqsave(&info->lock, flags);
		gpiod_direction_output(gpiod, 0);
		gpiod_direction_input(gpiod);
		if (t->read_sample_us)
			udelay(t->read_sample_us);
		if (gpiod_get_raw_value(gpiod))
			val |= BIT(i);
		raw_spin_unlock_irqrestore(&info->lock, flags);
		udelay(t->read_slot_us);
	}

	return val;
}

/* waits up to 50us for the slave to release the bus after its presence */
static int slg2_wait_release(struct slg2_info *info)
{
	int i;

	for (i = 0; i < 50; i++) {
		if (gpiod_get_raw_value(info->chip.gpiod))
			return 0;
		udelay(1);
	}

	return -EBUSY;
}

static int slg2_reset(struct slg2_info *info)
{
	struct gpio_desc *gpiod = info->chip.gpiod;
	unsigned long flags;
	int retry = SLG2_RESET_RETRY;
	int ret;

	while (retry) {
		raw_spin_lock_irqsave(&info->lock, flags);
		gpiod_direction_output(gpiod, 0);
		udelay(90);
		gpiod_direction_input(gpiod);
		udelay(30);
		if (gpiod_get_raw_value(gpiod)) {
			raw_spin_unlock_irqrestore(&info->lock, flags);
			mca_log_info("no presence pulse, %d retries left\n",
				     --retry);
			msleep(10);
			continue;
		}

		udelay(35);
		/* a chip still on the fast timing has ended its presence */
		if (info->speed == SLG2_SPEED_FAST &&
		    gpiod_get_raw_value(gpiod)) {
			raw_spin_unlock_irqrestore(&info->lock, flags);
			mca_log_info("bit timing not configured yet\n");
			if (slg2_config_baudrate(info, SLG2_SPEED_MEDIUM)) {
				mca_log_err("failed to configure the bit timing\n");
				retry--;
				msleep(10);
			}
			continue;
		}
		if (info->speed == SLG2_SPEED_FAST)
			info->speed = SLG2_SPEED_MEDIUM;

		ret = slg2_wait_release(info);
		raw_spin_unlock_irqrestore(&info->lock, flags);
		if (ret)
			mca_log_err("the bus does not come back high\n");
		return ret;
	}

	return -ENODEV;
}

static int slg2_config_baudrate(struct slg2_info *info, enum slg2_speed speed)
{
	static const u8 medium[] = { 0x88, 0x4a, 0x2c, 0x17, 0x79 };
	static const u8 slow[] = { 0xf0, 0xb4, 0xc2, 0x7d, 0xb8 };
	const u8 *v = speed == SLG2_SPEED_MEDIUM ? medium : slow;
	const u8 frame[] = {
		SLG2_BAUDRATE_CFG, 0x09, v[0], v[1], 0x01, 0x00, 0x02, 0x58,
		0x01, v[2], 0x0e, v[3], v[4],
	};
	static const u8 expect[] = { 0x01, SLG2_STATUS_OK, 0x10, 0x7e };
	int i;

	udelay(100);
	slg2_write_byte(info, SLG2_ROM_SKIP);
	udelay(100);
	for (i = 0; i < ARRAY_SIZE(frame); i++)
		slg2_write_byte(info, frame[i]);
	udelay(1000);

	for (i = 0; i < ARRAY_SIZE(expect); i++) {
		u8 val = slg2_read_byte(info);

		if (val != expect[i]) {
			mca_log_err("bit timing response byte %d is %#x, not %#x\n",
				    i, val, expect[i]);
			return -EIO;
		}
	}
	udelay(100);
	info->speed = speed;

	return 0;
}

static int slg2_read_rom(struct slg2_info *info, u8 *romid)
{
	int ret, i;

	ret = slg2_reset(info);
	if (ret)
		return ret;

	udelay(100);
	slg2_write_byte(info, SLG2_ROM_READ);
	udelay(100);
	for (i = 0; i < SLG2_ROMID_LEN; i++)
		romid[i] = slg2_read_byte(info);

	if (onewire_auth_crc8(romid, SLG2_ROMID_LEN - 1) != romid[SLG2_ROMID_LEN - 1]) {
		mca_log_err("rom id crc mismatch\n");
		return -EIO;
	}
	info->rom_known = true;

	return 0;
}

/*
 * Sends @cmd, whose second byte is the payload length, and reads back the
 * status byte plus @rsp_len bytes into @rsp. @wait_ms is the time the chip
 * needs for the command, spent with the bus driven high to power it.
 */
static int slg2_command(struct slg2_info *info, const u8 *cmd, int len,
			u8 *rsp, int rsp_len, unsigned int wait_ms)
{
	struct gpio_desc *gpiod = info->chip.gpiod;
	u8 frame[SLG2_FRAME_MAX + 2], buf[SLG2_FRAME_MAX + 3];
	u8 romid[SLG2_ROMID_LEN];
	int retry, ret = -EIO, i, n;
	u16 crc;

	if (len > SLG2_FRAME_MAX || cmd[1] + 2 != len ||
	    rsp_len + 1 > SLG2_FRAME_MAX)
		return -EINVAL;

	memcpy(frame, cmd, len);
	crc = ~crc16(0, frame, len);
	frame[len] = crc >> 8;
	frame[len + 1] = crc & 0xff;

	for (retry = 0; retry < SLG2_CMD_RETRY; retry++) {
		if (info->rom_known) {
			ret = slg2_reset(info);
			if (ret)
				return ret;
			udelay(100);
			slg2_write_byte(info, SLG2_ROM_SKIP);
			udelay(100);
		} else {
			ret = slg2_read_rom(info, romid);
			if (ret)
				return ret;
		}

		udelay(100);
		for (i = 0; i < len + 2; i++)
			slg2_write_byte(info, frame[i]);

		gpiod_direction_output_raw(gpiod, 1);
		if (wait_ms)
			msleep(wait_ms);

		n = slg2_read_byte(info);
		if (n != 1 && n != rsp_len + 1) {
			mca_log_info("command %#x: length %d, not %d\n",
				     cmd[0], n, rsp_len + 1);
			ret = -EPROTO;
			continue;
		}

		buf[0] = n;
		for (i = 1; i < n + 3; i++)
			buf[i] = slg2_read_byte(info);

		crc = ~crc16(0, buf, n + 1);
		if (crc != (buf[n + 1] << 8 | buf[n + 2])) {
			mca_log_info("command %#x: response crc mismatch\n",
				     cmd[0]);
			ret = -EBADMSG;
			continue;
		}

		if (buf[1] == SLG2_STATUS_CRC_ERROR) {
			mca_log_info("command %#x: the chip saw a crc error\n",
				     cmd[0]);
			ret = -EBADMSG;
			continue;
		}

		if (buf[1] != SLG2_STATUS_OK)
			return -EIO;
		if (n != rsp_len + 1)
			return -EPROTO;

		if (rsp_len)
			memcpy(rsp, &buf[2], rsp_len);
		return 0;
	}

	mca_log_err("command %#x failed: %d\n", cmd[0], ret);
	return ret;
}

static int slg2_romid_manid(struct slg2_info *info, u8 *romid, u8 *manid)
{
	const u8 cmd[] = { SLG2_CMD_ROMID_MANID, 0x00 };
	u8 rsp[SLG2_ROMID_LEN + SLG2_MANID_LEN];
	int ret;

	ret = slg2_command(info, cmd, sizeof(cmd), rsp, sizeof(rsp), 1);
	if (ret)
		return ret;

	memcpy(romid, rsp, SLG2_ROMID_LEN);
	memcpy(manid, rsp + SLG2_ROMID_LEN, SLG2_MANID_LEN);

	return 0;
}

static int slg2_read_page(void *data, int page, u8 *buf)
{
	const u8 cmd[] = { SLG2_CMD_MEM_READ, 0x01, page };

	return slg2_command(data, cmd, sizeof(cmd), buf, ONEWIRE_PAGE_SIZE, 6);
}

static int slg2_write_page(void *data, int page, const u8 *buf)
{
	u8 cmd[3 + ONEWIRE_PAGE_SIZE] = { SLG2_CMD_MEM_WRITE,
					  1 + ONEWIRE_PAGE_SIZE, page };

	memcpy(&cmd[3], buf, ONEWIRE_PAGE_SIZE);

	return slg2_command(data, cmd, sizeof(cmd), NULL, 0, 8);
}

static int slg2_read_counter(void *data, u32 *val)
{
	const u8 cmd[] = { SLG2_CMD_DC_GET, 0x00 };
	u8 rsp[4];
	int ret;

	ret = slg2_command(data, cmd, sizeof(cmd), rsp, sizeof(rsp), 6);
	if (ret)
		return ret;

	if (rsp[0] || rsp[1] > 1)
		return -ERANGE;

	*val = rsp[1] << 16 | rsp[2] << 8 | rsp[3];

	return 0;
}

static int slg2_decrement_counter(void *data)
{
	const u8 cmd[] = { SLG2_CMD_DC_DECREASE, 0x00 };

	return slg2_command(data, cmd, sizeof(cmd), NULL, 0, 8);
}

static int slg2_check_chip(void *data)
{
	struct slg2_info *info = data;
	u8 romid[SLG2_ROMID_LEN], manid[SLG2_MANID_LEN];
	int i;

	for (i = 0; i < SLG2_CHECK_RETRY; i++) {
		if (!slg2_romid_manid(info, romid, manid) &&
		    romid[7] == SLG2_FAMILY_CODE && romid[1] == SLG2_CID &&
		    romid[2] >= SLG2_CID_MIN)
			return 0;
	}

	return -ENODEV;
}

static int slg2_authenticate_once(struct slg2_info *info)
{
	u8 romid[SLG2_ROMID_LEN], manid[SLG2_MANID_LEN];
	u8 pub[2 * SLG2_KEY_LEN], cert[2 * SLG2_KEY_LEN], sig[2 * SLG2_KEY_LEN];
	u8 page0[ONEWIRE_PAGE_SIZE], challenge[1 + SLG2_KEY_LEN];
	u8 cmd[2 + sizeof(challenge)];
	u8 msg[SLG2_ROMID_LEN + ONEWIRE_PAGE_SIZE + 1 + SLG2_MANID_LEN +
	       SLG2_KEY_LEN];
	int ret, n;

	ret = slg2_read_rom(info, romid);
	if (ret)
		return ret;
	if (romid[0] != SLG2_FAMILY_CODE)
		return -ENODEV;

	ret = slg2_romid_manid(info, romid, manid);
	if (ret)
		return ret;

	ret = slg2_read_page(info, SLG2_PAGE_PUB_X, pub) ?:
	      slg2_read_page(info, SLG2_PAGE_PUB_Y, pub + SLG2_KEY_LEN) ?:
	      slg2_read_page(info, SLG2_PAGE_CERT_R, cert) ?:
	      slg2_read_page(info, SLG2_PAGE_CERT_S, cert + SLG2_KEY_LEN);
	if (ret)
		return ret;

	/* the certificate signs rom id | man id | chip key */
	n = 0;
	memcpy(&msg[n], romid, SLG2_ROMID_LEN);
	n += SLG2_ROMID_LEN;
	memcpy(&msg[n], manid, SLG2_MANID_LEN);
	n += SLG2_MANID_LEN;
	memcpy(&msg[n], pub, sizeof(pub));
	n += sizeof(pub);
	ret = onewire_auth_ecdsa_verify(slg2_root_key,
					slg2_root_key + SLG2_KEY_LEN, msg, n,
					cert, cert + SLG2_KEY_LEN);
	if (ret) {
		mca_log_err("chip certificate rejected: %d\n", ret);
		return ret;
	}

	ret = slg2_read_page(info, 0, page0);
	if (ret)
		return ret;

	/* sign page 0 together with a fresh challenge */
	challenge[0] = 0;
	get_random_bytes(&challenge[1], SLG2_KEY_LEN);
	cmd[0] = SLG2_CMD_ECDSA_SIGN;
	cmd[1] = sizeof(challenge);
	memcpy(&cmd[2], challenge, sizeof(challenge));
	ret = slg2_command(info, cmd, sizeof(cmd), sig, sizeof(sig), 280);
	if (ret)
		return ret;

	n = 0;
	memcpy(&msg[n], romid, SLG2_ROMID_LEN);
	n += SLG2_ROMID_LEN;
	memcpy(&msg[n], page0, ONEWIRE_PAGE_SIZE);
	n += ONEWIRE_PAGE_SIZE;
	msg[n++] = 0;
	memcpy(&msg[n], manid, SLG2_MANID_LEN);
	n += SLG2_MANID_LEN;
	memcpy(&msg[n], &challenge[1], SLG2_KEY_LEN);
	n += SLG2_KEY_LEN;
	ret = onewire_auth_ecdsa_verify(pub, pub + SLG2_KEY_LEN, msg, n,
					sig, sig + SLG2_KEY_LEN);
	if (ret)
		mca_log_err("page signature rejected: %d\n", ret);

	return ret;
}

static int slg2_authenticate(void *data)
{
	struct slg2_info *info = data;
	int i, ret = -ENODEV;

	for (i = 0; i < SLG2_AUTH_RETRY; i++) {
		ret = slg2_authenticate_once(info);
		if (!ret)
			break;
		msleep(10);
	}
	if (ret)
		return ret;

	/* settle on the slow bit timing for the rest of the uptime */
	for (i = 0; i < SLG2_BAUDRATE_RETRY; i++) {
		if (!slg2_reset(info) &&
		    !slg2_config_baudrate(info, SLG2_SPEED_SLOW))
			break;
	}

	return 0;
}

static const struct onewire_auth_ops slg2_ops = {
	.check_chip = slg2_check_chip,
	.authenticate = slg2_authenticate,
	.read_page = slg2_read_page,
	.write_page = slg2_write_page,
	.read_counter = slg2_read_counter,
	.decrement_counter = slg2_decrement_counter,
};

static int slg2_probe(struct platform_device *pdev)
{
	struct slg2_info *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	raw_spin_lock_init(&info->lock);
	info->chip.name = "acl16_slg2";
	info->chip.ops = &slg2_ops;
	info->chip.data = info;
	platform_set_drvdata(pdev, info);

	ret = onewire_auth_add_chip(&info->chip);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register with the onewire core\n");

	return 0;
}

static int slg2_remove(struct platform_device *pdev)
{
	struct slg2_info *info = platform_get_drvdata(pdev);

	onewire_auth_remove_chip(&info->chip);

	return 0;
}

static const struct of_device_id slg2_of_match[] = {
	{ .compatible = "mca,acl16-slg2" },
	{ }
};
MODULE_DEVICE_TABLE(of, slg2_of_match);

static struct platform_driver slg2_driver = {
	.driver = {
		.name = "acl16_slg2",
		.of_match_table = slg2_of_match,
	},
	.probe = slg2_probe,
	.remove = slg2_remove,
};
module_platform_driver(slg2_driver);

MODULE_DESCRIPTION("ACL16 SLG2 battery authentication chip");
MODULE_LICENSE("GPL");
