#ifndef _MCA_COMMON_MCA_HWID_H_
#define _MCA_COMMON_MCA_HWID_H_

#include <linux/types.h>

struct mca_hwid {
	u32 platform_version;
	u32 country_version;
	u32 major_version;
	u32 minor_version;
	u32 build_version;
	u32 product_adc;
	u32 build_adc;
	u32 hwid_value;
	const char *product_name;
};

const struct mca_hwid *mca_get_hwid_info(void);

#endif /* _MCA_COMMON_MCA_HWID_H_ */
