#ifndef _MCA_PLATFORM_PLATFORM_BC12_CLASS_H_
#define _MCA_PLATFORM_PLATFORM_BC12_CLASS_H_

#include <linux/types.h>

/* BC1.2 detector role (order = index into the per-role ops array). */
enum bc12_role {
	BC12_MAIN_ROLE = 0,
	BC12_AUX_ROLE,
	BC12_MAX_ROLE,
};

struct platform_bc12_class_ops {
	int (*bc12_det_en)(int en, void *data);
	int (*get_charge_type)(int *value, void *data);
};

int platform_bc12_class_ops_register(unsigned int role,
				     struct platform_bc12_class_ops *ops,
				     void *data);

#endif /* _MCA_PLATFORM_PLATFORM_BC12_CLASS_H_ */
