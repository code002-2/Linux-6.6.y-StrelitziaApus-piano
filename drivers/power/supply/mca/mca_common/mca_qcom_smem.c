#include <linux/module.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/soc/qcom/smem.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_smem.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_smem"
#endif

#define SMEM_ID_VENDOR_BATTERY_INFO 0x51

struct mca_smem_battery_info {
	u32 reserved[8]; /* 0x00 .. 0x1f */
	u32 zero_speed_start_mode; /* 0x20 */
	u8 verify_result; /* 0x24 : bit5 verified, bit4 chip_ok */
	u8 cell_sn; /* 0x25 */
};

int get_smem_battery_info(int *is_zero_speed)
{
	struct mca_smem_battery_info *info;
	int ret;

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			      sizeof(struct mca_smem_battery_info));
	if (ret < 0 && ret != -EEXIST) {
		mca_log_err("unable to allocate shared state entry\n");
		return 0;
	}

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			     NULL);
	if (IS_ERR(info)) {
		mca_log_err("Unable to acquire shared state entry\n");
		return 0;
	}

	*is_zero_speed = info->zero_speed_start_mode;
	mca_log_err("zero_speed_start_mode: %d\n", *is_zero_speed);

	return 0;
}
EXPORT_SYMBOL(get_smem_battery_info);

int get_smem_battery_verify_result(u8 *verified, u8 *chip_ok)
{
	struct mca_smem_battery_info *info;
	int ret;

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			      sizeof(struct mca_smem_battery_info));
	if (ret < 0 && ret != -EEXIST) {
		mca_log_err("unable to allocate shared state entry\n");
		return 0;
	}

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			     NULL);
	if (IS_ERR(info)) {
		mca_log_err("Unable to acquire shared state entry\n");
		return 0;
	}

	*verified = (info->verify_result >> 5) & 1;
	*chip_ok = (info->verify_result >> 4) & 1;
	mca_log_err("uefi_batt_verfitied: %d, uefi_batt_verfitied_chip_ok: %d\n",
		    *verified, *chip_ok);
	return 0;
}
EXPORT_SYMBOL(get_smem_battery_verify_result);

int get_smem_battery_cell_sn(u32 *cell_sn)
{
	struct mca_smem_battery_info *info;
	int ret;

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			      sizeof(struct mca_smem_battery_info));
	if (ret < 0 && ret != -EEXIST) {
		mca_log_err("unable to allocate shared state entry\n");
		return 0;
	}

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			     NULL);
	if (IS_ERR(info)) {
		mca_log_err("Unable to acquire shared state entry\n");
		return 0;
	}

	*cell_sn = info->cell_sn;
	mca_log_err("uefi_battery_cell_sn: %d\n", *cell_sn);
	return 0;
}
EXPORT_SYMBOL(get_smem_battery_cell_sn);

MODULE_DESCRIPTION("mca get smem info");
MODULE_AUTHOR("liuliang10@xiaomi.com");
MODULE_LICENSE("GPL v2");
