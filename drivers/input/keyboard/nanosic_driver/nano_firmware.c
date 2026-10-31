// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware loader of the 803x
 *
 * The 803x runs its keyboard firmware from RAM, so after a reset it sits
 * in its boot loader until the host uploads MCU_Upgrade.bin again. The
 * boot loader takes commands prefixed with BOOT_PREFIX on the usual i2c
 * address. The file carries a 64 byte header at FW_HEADER_OFFSET and the
 * image right after it.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/unaligned.h>
#include "nano_macro.h"

#define FW_NAME "MCU_Upgrade.bin"
#define FW_HEADER_OFFSET 0x7fc0
#define FW_HEADER_SIZE 0x40
#define FW_HEADER_MAGIC "NANO IC"
#define FW_HEADER_IMAGE_SIZE 0x30
#define FW_HEADER_ENTRY 0x15
#define FW_HEADER_IDS 0x19
#define FW_RETRIES 4

#define BOOT_PREFIX 0x5c
#define BOOT_ID 0xc8
#define BOOT_CMD_READ_ID 0x04
#define BOOT_CMD_READ_STATUS 0x05
#define BOOT_CMD_HEADER 0x06
#define BOOT_CMD_DATA 0x07
#define BOOT_CMD_START 0x08
#define BOOT_CMD_RUN 0x09

#define BOOT_PACKET_SIZE 0x200
#define BOOT_DATA_OFFSET 9
#define BOOT_DATA_SIZE (BOOT_PACKET_SIZE - BOOT_DATA_OFFSET)

static int Nanosic_firmware_write(struct nano_i2c_client *i2c_client,
				  const u8 *data, size_t len)
{
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	u8 *buf;
	int ret;

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = BOOT_PREFIX;
	memcpy(buf + 1, data, len);

	msg.addr = i2c_client->i2c_slave_addr;
	msg.flags = 0;
	msg.len = len + 1;
	msg.buf = buf;

	adap = i2c_get_adapter(i2c_client->i2c_bus_id);
	if (!adap) {
		kfree(buf);
		return -ENODEV;
	}

	mutex_lock(&i2c_client->write_mutex);
	ret = i2c_transfer(adap, &msg, 1);
	mutex_unlock(&i2c_client->write_mutex);

	i2c_put_adapter(adap);
	kfree(buf);

	/* the boot loader needs a moment after every command */
	msleep(3);

	return ret < 0 ? ret : 0;
}

static int Nanosic_firmware_query(struct nano_i2c_client *i2c_client, u8 cmd)
{
	struct i2c_adapter *adap;
	struct i2c_msg msg[2];
	u8 prefix = BOOT_PREFIX;
	u8 val = 0;
	int ret;

	ret = Nanosic_firmware_write(i2c_client, &cmd, 1);
	if (ret)
		return ret;

	msg[0].addr = i2c_client->i2c_slave_addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &prefix;
	msg[1].addr = i2c_client->i2c_slave_addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &val;

	adap = i2c_get_adapter(i2c_client->i2c_bus_id);
	if (!adap)
		return -ENODEV;

	mutex_lock(&i2c_client->read_mutex);
	ret = i2c_transfer(adap, msg, 2);
	mutex_unlock(&i2c_client->read_mutex);

	i2c_put_adapter(adap);

	return ret < 0 ? ret : val;
}

static int Nanosic_firmware_write_header(struct nano_i2c_client *i2c_client,
					 const u8 *hdr, const u8 *image,
					 u32 image_size)
{
	u8 cmd[FW_HEADER_SIZE + 1] = { 0 };
	u32 sum = 0;
	u32 i;

	cmd[0] = BOOT_CMD_HEADER;
	memcpy(&cmd[1], hdr, 7);
	/* chip id */
	cmd[8] = 0x40;
	cmd[9] = 0x81;
	/* where the boot loader copies the image from and to */
	put_unaligned_le32(0x2345, &cmd[10]);
	put_unaligned_le32(0x40, &cmd[14]);
	put_unaligned_le32(image_size, &cmd[18]);
	/* entry address, then pid, vid and version */
	memcpy(&cmd[22], hdr + FW_HEADER_ENTRY, 4);
	memcpy(&cmd[26], hdr + FW_HEADER_IDS, 6);

	for (i = 0; i + 4 <= image_size; i += 4)
		sum += get_unaligned_le32(image + i);
	put_unaligned_le32(sum, &cmd[53]);

	for (i = 1; i < FW_HEADER_SIZE; i++)
		cmd[FW_HEADER_SIZE] += cmd[i];

	return Nanosic_firmware_write(i2c_client, cmd, sizeof(cmd));
}

static int Nanosic_firmware_write_image(struct nano_i2c_client *i2c_client,
					const u8 *image, u32 image_size)
{
	u8 *packet;
	u32 offset, len;
	int ret = 0;

	packet = kmalloc(BOOT_PACKET_SIZE, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;

	for (offset = 0; offset < image_size; offset += len) {
		len = min_t(u32, image_size - offset, BOOT_DATA_SIZE);

		memset(packet, 0, BOOT_PACKET_SIZE);
		packet[0] = BOOT_CMD_DATA;
		put_unaligned_be32(offset, &packet[1]);
		put_unaligned_be32(len, &packet[5]);
		memcpy(&packet[BOOT_DATA_OFFSET], image + offset, len);

		ret = Nanosic_firmware_write(i2c_client, packet,
					     BOOT_PACKET_SIZE);
		if (ret)
			break;
	}

	kfree(packet);

	return ret;
}

static int Nanosic_firmware_upload(struct nano_i2c_client *i2c_client,
				   const struct firmware *fw)
{
	const u8 *hdr = fw->data + FW_HEADER_OFFSET;
	const u8 *image = hdr + FW_HEADER_SIZE;
	u8 cmd;
	u32 image_size;
	int ret;

	if (fw->size < FW_HEADER_OFFSET + FW_HEADER_SIZE ||
	    memcmp(hdr, FW_HEADER_MAGIC, strlen(FW_HEADER_MAGIC))) {
		dbgprint(ERROR_LEVEL, "invalid firmware\n");
		return -EINVAL;
	}

	image_size = get_unaligned_le32(hdr + FW_HEADER_IMAGE_SIZE);
	if (image_size > fw->size - FW_HEADER_OFFSET - FW_HEADER_SIZE) {
		dbgprint(ERROR_LEVEL, "firmware image of %u bytes is truncated\n",
			 image_size);
		return -EINVAL;
	}

	ret = Nanosic_firmware_query(i2c_client, BOOT_CMD_READ_ID);
	if (ret != BOOT_ID) {
		dbgprint(ERROR_LEVEL, "boot loader id %d does not match\n", ret);
		return ret < 0 ? ret : -ENODEV;
	}

	Nanosic_firmware_query(i2c_client, BOOT_CMD_READ_STATUS);

	ret = Nanosic_firmware_write_header(i2c_client, hdr, image, image_size);
	if (ret)
		return ret;

	Nanosic_firmware_query(i2c_client, BOOT_CMD_READ_STATUS);

	ret = Nanosic_firmware_write_image(i2c_client, image, image_size);
	if (ret)
		return ret;

	cmd = BOOT_CMD_START;
	ret = Nanosic_firmware_write(i2c_client, &cmd, 1);
	if (ret)
		return ret;

	Nanosic_firmware_query(i2c_client, BOOT_CMD_READ_STATUS);

	cmd = BOOT_CMD_RUN;
	return Nanosic_firmware_write(i2c_client, &cmd, 1);
}

int Nanosic_firmware_load(struct nano_i2c_client *i2c_client)
{
	const struct firmware *fw;
	int i, ret;

	ret = request_firmware(&fw, FW_NAME, i2c_client->dev);
	if (ret) {
		dbgprint(ERROR_LEVEL, "failed to load %s: %d\n", FW_NAME, ret);
		return ret;
	}

	for (i = 0; i < FW_RETRIES; i++) {
		ret = Nanosic_firmware_upload(i2c_client, fw);
		if (!ret || ret == -EINVAL)
			break;
	}

	release_firmware(fw);

	if (ret)
		dbgprint(ERROR_LEVEL, "firmware upload failed: %d\n", ret);
	else
		dbgprint(ALERT_LEVEL, "firmware uploaded\n");

	return ret;
}
