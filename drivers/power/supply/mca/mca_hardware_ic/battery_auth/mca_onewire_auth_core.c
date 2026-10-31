// SPDX-License-Identifier: GPL-2.0
/*
 * MCA onewire battery authentication core.
 *
 * The battery pack carries an authentication chip on a onewire bus, either
 * an ACL16 SLG2 or a Maxim DS28E30. Both keep the same user memory layout,
 * which this core owns: page 0 holds the serial number with the
 * manufacturing date in it, page 1 the first usage date and page 2 the UI
 * state of health, the UI SoC at shutdown and the low part of the cycle
 * count, which moves on to the decrementing counter of the chip once the
 * byte runs out.
 *
 * The chip drivers register their commands here. After probe the core looks
 * for the chip that answers, takes the authentication result of UEFI from
 * shared memory and only runs the ECDSA challenge itself when UEFI did not.
 *
 * Written by sidharthify (Sidharth Sharma) <wednisegit@gmail.com>
 */

#include <crypto/ecdh.h>
#include <crypto/internal/ecc.h>
#include <crypto/sha2.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <asm/unaligned.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_smem.h>
#include <mca/hardware/onewire_auth.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "onewire_auth_core"
#endif

#define OW_PAGE_SN			0
#define OW_PAGE_FIRST_USAGE		1
#define OW_PAGE_STATE			2
#define OW_STATE_SHUTDOWN_UISOC		29
#define OW_STATE_CYCLE_COUNT		30
/* the cycle byte counts up to here, then the chip counter takes over */
#define OW_CYCLE_BYTE_MAX		29
/* a field with this many erased bytes was never written */
#define OW_ERASED_MAX			4

#define OW_DETECT_DELAY_MS		500
#define OW_DETECT_RETRY_MS		1000
#define OW_DETECT_RETRY_MAX		10

struct onewire_auth_info {
	struct device *dev;
	struct gpio_desc *gpiod;
	/* serialises the bus and the chip list */
	struct mutex lock;
	struct list_head chips;
	struct onewire_auth_chip *chip;
	struct delayed_work detect_work;
	int retries;
	int shutdown_uisoc;
	bool auth_pass;
	bool chip_ok;
};

static struct onewire_auth_info *g_ow_info;
static DEFINE_MUTEX(ow_info_lock);

static void ow_be_to_vli(const u8 *in, u64 *out, unsigned int ndigits)
{
	unsigned int i;

	for (i = 0; i < ndigits; i++)
		out[i] = get_unaligned_be64(in + (ndigits - 1 - i) * sizeof(u64));
}

/*
 * Verifies an ECDSA NIST P-256 signature over the SHA-256 digest of @msg.
 * The key and signature halves are 32 bytes each, most significant first.
 */
int onewire_auth_ecdsa_verify(const u8 *pub_x, const u8 *pub_y,
			      const u8 *msg, size_t len,
			      const u8 *sig_r, const u8 *sig_s)
{
	const struct ecc_curve *curve = ecc_get_curve(ECC_CURVE_NIST_P256);
	unsigned int ndigits = curve->g.ndigits;
	u64 x[ECC_MAX_DIGITS], y[ECC_MAX_DIGITS];
	u64 r[ECC_MAX_DIGITS], s[ECC_MAX_DIGITS], hash[ECC_MAX_DIGITS];
	u64 s1[ECC_MAX_DIGITS], u1[ECC_MAX_DIGITS], u2[ECC_MAX_DIGITS];
	u64 rx[ECC_MAX_DIGITS], ry[ECC_MAX_DIGITS];
	struct ecc_point pub = ECC_POINT_INIT(x, y, ndigits);
	struct ecc_point res = ECC_POINT_INIT(rx, ry, ndigits);
	u8 digest[SHA256_DIGEST_SIZE];

	ow_be_to_vli(pub_x, x, ndigits);
	ow_be_to_vli(pub_y, y, ndigits);
	ow_be_to_vli(sig_r, r, ndigits);
	ow_be_to_vli(sig_s, s, ndigits);

	if (ecc_is_pubkey_valid_partial(curve, &pub))
		return -EKEYREJECTED;

	if (vli_is_zero(r, ndigits) || vli_cmp(r, curve->n, ndigits) >= 0 ||
	    vli_is_zero(s, ndigits) || vli_cmp(s, curve->n, ndigits) >= 0)
		return -EBADMSG;

	sha256(msg, len, digest);
	ow_be_to_vli(digest, hash, ndigits);

	/* u1 = hash / s, u2 = r / s, then R = u1 * G + u2 * Q */
	vli_mod_inv(s1, s, curve->n, ndigits);
	vli_mod_mult_slow(u1, hash, s1, curve->n, ndigits);
	vli_mod_mult_slow(u2, r, s1, curve->n, ndigits);
	ecc_point_mult_shamir(&res, u1, &curve->g, u2, &pub, curve);

	if (vli_cmp(res.x, curve->n, ndigits) == 1)
		vli_sub(res.x, res.x, curve->n, ndigits);

	return vli_cmp(res.x, r, ndigits) ? -EKEYREJECTED : 0;
}
EXPORT_SYMBOL_GPL(onewire_auth_ecdsa_verify);

/* the Dallas/Maxim CRC8 of rom ids */
u8 onewire_auth_crc8(const u8 *buf, size_t len)
{
	u8 crc = 0;
	int i;

	while (len--) {
		crc ^= *buf++;
		for (i = 0; i < 8; i++)
			crc = crc & 1 ? (crc >> 1) ^ 0x8c : crc >> 1;
	}

	return crc;
}
EXPORT_SYMBOL_GPL(onewire_auth_crc8);

int onewire_auth_add_chip(struct onewire_auth_chip *chip)
{
	struct onewire_auth_info *info;

	mutex_lock(&ow_info_lock);
	info = g_ow_info;
	if (!info) {
		mutex_unlock(&ow_info_lock);
		return -EPROBE_DEFER;
	}

	mutex_lock(&info->lock);
	chip->gpiod = info->gpiod;
	list_add_tail(&chip->node, &info->chips);
	/* a chip driver that loads after detection gave up gets another try */
	if (!info->chip) {
		info->retries = 0;
		mod_delayed_work(system_wq, &info->detect_work,
				 msecs_to_jiffies(OW_DETECT_DELAY_MS));
	}
	mutex_unlock(&info->lock);
	mutex_unlock(&ow_info_lock);

	mca_log_info("%s registered\n", chip->name);
	return 0;
}
EXPORT_SYMBOL_GPL(onewire_auth_add_chip);

void onewire_auth_remove_chip(struct onewire_auth_chip *chip)
{
	struct onewire_auth_info *info;

	mutex_lock(&ow_info_lock);
	info = g_ow_info;
	if (info) {
		mutex_lock(&info->lock);
		if (info->chip == chip)
			info->chip = NULL;
		list_del(&chip->node);
		mutex_unlock(&info->lock);
	}
	mutex_unlock(&ow_info_lock);
}
EXPORT_SYMBOL_GPL(onewire_auth_remove_chip);

/*
 * Takes the bus and returns the detected chip, or NULL with the bus released
 * when there is none.
 */
static struct onewire_auth_info *ow_get(void)
{
	struct onewire_auth_info *info;

	mutex_lock(&ow_info_lock);
	info = g_ow_info;
	if (info) {
		mutex_lock(&info->lock);
		if (!info->chip) {
			mutex_unlock(&info->lock);
			info = NULL;
		}
	}
	mutex_unlock(&ow_info_lock);

	return info;
}

static void ow_put(struct onewire_auth_info *info)
{
	mutex_unlock(&info->lock);
}

static int ow_read_page(struct onewire_auth_info *info, int page, u8 *buf)
{
	struct onewire_auth_chip *chip = info->chip;

	return chip->ops->read_page(chip->data, page, buf);
}

static int ow_write_page(struct onewire_auth_info *info, int page,
			 const u8 *buf)
{
	struct onewire_auth_chip *chip = info->chip;

	return chip->ops->write_page(chip->data, page, buf);
}

static int ow_count_erased(const u8 *buf, int len)
{
	int i, n = 0;

	for (i = 0; i < len; i++)
		n += buf[i] == 0xff;

	return n;
}

static void onewire_auth_detect_work(struct work_struct *work)
{
	struct onewire_auth_info *info = container_of(to_delayed_work(work),
					struct onewire_auth_info, detect_work);
	struct onewire_auth_chip *chip;
	u8 verified = 0, chip_ok = 0;
	u8 page[ONEWIRE_PAGE_SIZE];

	mutex_lock(&info->lock);
	list_for_each_entry(chip, &info->chips, node) {
		if (!chip->ops->check_chip(chip->data)) {
			info->chip = chip;
			break;
		}
		mca_log_info("%s does not answer\n", chip->name);
	}

	if (!info->chip) {
		mutex_unlock(&info->lock);
		if (++info->retries < OW_DETECT_RETRY_MAX)
			schedule_delayed_work(&info->detect_work,
					msecs_to_jiffies(OW_DETECT_RETRY_MS));
		else
			mca_log_err("no battery authentication chip found\n");
		return;
	}

	chip = info->chip;
	mca_log_info("%s found\n", chip->name);

	if (!ow_read_page(info, OW_PAGE_STATE, page)) {
		info->shutdown_uisoc = page[OW_STATE_SHUTDOWN_UISOC];
		if (info->shutdown_uisoc == 0xff)
			info->shutdown_uisoc = 0;
	}

	get_smem_battery_verify_result(&verified, &chip_ok);
	if (verified) {
		mca_log_info("battery authenticated by UEFI\n");
		info->auth_pass = true;
	} else if (!chip->ops->authenticate(chip->data)) {
		mca_log_info("%s authentication passed\n", chip->name);
		info->auth_pass = true;
	} else {
		mca_log_err("%s authentication failed\n", chip->name);
	}

	info->chip_ok = chip_ok || info->auth_pass;
	mutex_unlock(&info->lock);
}

void onewire_check_chip_ok(bool *ok)
{
	struct onewire_auth_info *info = ow_get();
	struct onewire_auth_chip *chip;

	*ok = false;
	if (!info)
		return;

	chip = info->chip;
	if (!chip->ops->check_chip(chip->data)) {
		info->chip_ok = true;
		*ok = true;
	}
	ow_put(info);
}
EXPORT_SYMBOL_GPL(onewire_check_chip_ok);

void onewire_check_auth_pass(bool *pass)
{
	struct onewire_auth_info *info;

	*pass = false;

	mutex_lock(&ow_info_lock);
	info = g_ow_info;
	/* do not wait for an authentication in progress, ask again later */
	if (info && mutex_trylock(&info->lock)) {
		*pass = info->auth_pass;
		mutex_unlock(&info->lock);
	}
	mutex_unlock(&ow_info_lock);
}
EXPORT_SYMBOL_GPL(onewire_check_auth_pass);

/* fills ONEWIRE_PAGE_SIZE bytes */
int onewire_get_batt_sn(u8 *buf)
{
	struct onewire_auth_info *info = ow_get();
	int ret;

	if (!info)
		return -EOPNOTSUPP;

	ret = ow_read_page(info, OW_PAGE_SN, buf);
	ow_put(info);

	return ret;
}
EXPORT_SYMBOL_GPL(onewire_get_batt_sn);

int onewire_get_manufacturing_date(u8 *buf)
{
	return onewire_get_batt_sn(buf);
}
EXPORT_SYMBOL_GPL(onewire_get_manufacturing_date);

/* fills ONEWIRE_DATE_LEN bytes, YYMMDD */
int onewire_get_first_usage_date(u8 *buf)
{
	struct onewire_auth_info *info = ow_get();
	u8 page[ONEWIRE_PAGE_SIZE];
	int ret;

	if (!info)
		return -EOPNOTSUPP;

	ret = ow_read_page(info, OW_PAGE_FIRST_USAGE, page);
	if (ret)
		goto out;

	if (ow_count_erased(page, ONEWIRE_DATE_LEN) > OW_ERASED_MAX) {
		/* never set: store zeros so that userspace sets it once */
		memset(page, '0', sizeof(page));
		ret = ow_write_page(info, OW_PAGE_FIRST_USAGE, page);
		if (ret)
			goto out;
	}

	memcpy(buf, page, ONEWIRE_DATE_LEN);
out:
	ow_put(info);
	return ret;
}
EXPORT_SYMBOL_GPL(onewire_get_first_usage_date);

int onewire_set_first_usage_date(u8 *buf)
{
	struct onewire_auth_info *info = ow_get();
	u8 page[ONEWIRE_PAGE_SIZE];
	int ret;

	if (!info)
		return -EOPNOTSUPP;

	ret = ow_read_page(info, OW_PAGE_FIRST_USAGE, page);
	if (!ret) {
		memcpy(page, buf, ONEWIRE_DATE_LEN);
		ret = ow_write_page(info, OW_PAGE_FIRST_USAGE, page);
	}
	ow_put(info);

	return ret;
}
EXPORT_SYMBOL_GPL(onewire_set_first_usage_date);

int onewire_get_batt_cycle_count(int *count)
{
	struct onewire_auth_info *info = ow_get();
	struct onewire_auth_chip *chip;
	u8 page[ONEWIRE_PAGE_SIZE];
	u32 counter;
	int ret, val;

	if (!info)
		return -EOPNOTSUPP;

	chip = info->chip;
	ret = ow_read_page(info, OW_PAGE_STATE, page);
	if (ret)
		goto out;

	val = page[OW_STATE_CYCLE_COUNT];
	if (val == 0xff) {
		page[OW_STATE_CYCLE_COUNT] = 0;
		ret = ow_write_page(info, OW_PAGE_STATE, page);
		if (ret)
			goto out;
		val = 0;
	} else if (val >= OW_CYCLE_BYTE_MAX) {
		ret = chip->ops->read_counter(chip->data, &counter);
		if (ret)
			goto out;
		val += ONEWIRE_COUNTER_MAX - counter;
	}

	*count = val;
out:
	ow_put(info);
	return ret;
}
EXPORT_SYMBOL_GPL(onewire_get_batt_cycle_count);

int onewire_set_batt_cycle_count(void)
{
	struct onewire_auth_info *info = ow_get();
	struct onewire_auth_chip *chip;
	u8 page[ONEWIRE_PAGE_SIZE];
	int ret;

	if (!info)
		return -EOPNOTSUPP;

	chip = info->chip;
	ret = ow_read_page(info, OW_PAGE_STATE, page);
	if (ret)
		goto out;

	if (page[OW_STATE_CYCLE_COUNT] < OW_CYCLE_BYTE_MAX) {
		page[OW_STATE_CYCLE_COUNT]++;
		ret = ow_write_page(info, OW_PAGE_STATE, page);
	} else {
		ret = chip->ops->decrement_counter(chip->data);
	}
out:
	ow_put(info);
	return ret;
}
EXPORT_SYMBOL_GPL(onewire_set_batt_cycle_count);

/* the counter of the chip only counts down, so this cannot be undone */
int onewire_reset_batt_cycle_count(void)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(onewire_reset_batt_cycle_count);

/* fills ONEWIRE_UI_SOH_LEN bytes */
int onewire_get_batt_ui_soh(u8 *buf)
{
	struct onewire_auth_info *info = ow_get();
	u8 page[ONEWIRE_PAGE_SIZE];
	int ret;

	if (!info)
		return -EOPNOTSUPP;

	ret = ow_read_page(info, OW_PAGE_STATE, page);
	if (!ret) {
		if (ow_count_erased(page, ONEWIRE_UI_SOH_LEN) > OW_ERASED_MAX)
			memset(page, 0, ONEWIRE_UI_SOH_LEN);
		memcpy(buf, page, ONEWIRE_UI_SOH_LEN);
	}
	ow_put(info);

	return ret;
}
EXPORT_SYMBOL_GPL(onewire_get_batt_ui_soh);

int onewire_set_batt_ui_soh(u8 *buf)
{
	struct onewire_auth_info *info = ow_get();
	u8 page[ONEWIRE_PAGE_SIZE];
	int ret;

	if (!info)
		return -EOPNOTSUPP;

	ret = ow_read_page(info, OW_PAGE_STATE, page);
	if (!ret) {
		memcpy(page, buf, ONEWIRE_UI_SOH_LEN);
		ret = ow_write_page(info, OW_PAGE_STATE, page);
	}
	ow_put(info);

	return ret;
}
EXPORT_SYMBOL_GPL(onewire_set_batt_ui_soh);

/* the UI SoC stored at the last shutdown, read once at detection */
int onewire_get_shutdown_uisoc(int *uisoc)
{
	struct onewire_auth_info *info = ow_get();

	if (!info)
		return -EOPNOTSUPP;

	*uisoc = info->shutdown_uisoc;
	ow_put(info);

	return 0;
}
EXPORT_SYMBOL_GPL(onewire_get_shutdown_uisoc);

int onewire_set_shutdown_uisoc(int uisoc)
{
	struct onewire_auth_info *info = ow_get();
	u8 page[ONEWIRE_PAGE_SIZE];
	int ret;

	if (!info)
		return -EOPNOTSUPP;

	ret = ow_read_page(info, OW_PAGE_STATE, page);
	if (!ret && page[OW_STATE_SHUTDOWN_UISOC] != uisoc) {
		page[OW_STATE_SHUTDOWN_UISOC] = uisoc;
		ret = ow_write_page(info, OW_PAGE_STATE, page);
	}
	ow_put(info);

	return ret;
}
EXPORT_SYMBOL_GPL(onewire_set_shutdown_uisoc);

static int onewire_auth_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct onewire_auth_info *info;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	mutex_init(&info->lock);
	INIT_LIST_HEAD(&info->chips);
	INIT_DELAYED_WORK(&info->detect_work, onewire_auth_detect_work);

	/* the chips draw their power from the bus, keep it high when idle */
	info->gpiod = devm_gpiod_get(dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(info->gpiod))
		return dev_err_probe(dev, PTR_ERR(info->gpiod),
				     "failed to get the onewire gpio\n");

	platform_set_drvdata(pdev, info);

	mutex_lock(&ow_info_lock);
	g_ow_info = info;
	mutex_unlock(&ow_info_lock);

	schedule_delayed_work(&info->detect_work,
			      msecs_to_jiffies(OW_DETECT_DELAY_MS));

	return 0;
}

static int onewire_auth_remove(struct platform_device *pdev)
{
	struct onewire_auth_info *info = platform_get_drvdata(pdev);

	mutex_lock(&ow_info_lock);
	g_ow_info = NULL;
	mutex_unlock(&ow_info_lock);

	cancel_delayed_work_sync(&info->detect_work);

	return 0;
}

static const struct of_device_id onewire_auth_of_match[] = {
	{ .compatible = "mca,onewire-core" },
	{ }
};
MODULE_DEVICE_TABLE(of, onewire_auth_of_match);

static struct platform_driver onewire_auth_driver = {
	.driver = {
		.name = "mca_onewire_auth_core",
		.of_match_table = onewire_auth_of_match,
	},
	.probe = onewire_auth_probe,
	.remove = onewire_auth_remove,
};
module_platform_driver(onewire_auth_driver);

MODULE_DESCRIPTION("MCA onewire battery authentication core");
MODULE_LICENSE("GPL");
