/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _MSM_VIDC_RESOURCES_H_
#define _MSM_VIDC_RESOURCES_H_

struct icc_path;
struct regulator;
struct clk;
struct reset_control;
struct llcc_slice_desc;
struct iommu_domain;
struct device;
struct msm_vidc_core;

enum msm_vidc_branch_mem_flags {
	MSM_VIDC_CLKFLAG_RETAIN_PERIPH,
	MSM_VIDC_CLKFLAG_NORETAIN_PERIPH,
	MSM_VIDC_CLKFLAG_RETAIN_MEM,
	MSM_VIDC_CLKFLAG_NORETAIN_MEM,
	MSM_VIDC_CLKFLAG_PERIPH_OFF_SET,
	MSM_VIDC_CLKFLAG_PERIPH_OFF_CLEAR,
};

struct bus_info {
	struct icc_path           *icc;
	const char                *name;
	u32                        min_kbps;
	u32                        max_kbps;
};

struct regulator_info {
	struct regulator          *regulator;
	const char                *name;
	bool                       hw_power_collapse;
};

struct power_domain_info {
	struct device             *genpd_dev;
	const char                *name;
};

struct clock_residency {
	struct list_head           list;
	u64                        rate;
	u64                        start_time_us;
	u64                        total_time_us;
};

struct clock_info {
	struct clk                *clk;
	const char                *name;
	u32                        clk_id;
	bool                       has_scaling;
	u64                        prev;
#ifdef CONFIG_MSM_MMRM
	struct mmrm_client        *mmrm_client;
#endif
	struct list_head           residency_list;  /* list of struct clock_residency */
};

struct reset_info {
	struct reset_control      *rst;
	const char                *name;
	bool                       exclusive_release;
};

struct subcache_info {
	struct llcc_slice_desc    *subcache;
	const char                *name;
	u32                        llcc_id;
	bool                       isactive;
};

struct addr_range {
	u32                        start;
	u32                        size;
};

struct context_bank_info {
	const char                *name;
	struct addr_range          addr_range;
	bool                       secure;
	bool                       dma_coherant;
	struct device             *dev;
	struct iommu_domain       *domain;
	u32                        region;
	u64                        dma_mask;
};

struct frequency_table {
	unsigned long freq;
};

struct device_region_info {
	const char          *name;
	phys_addr_t          phy_addr;
	u32                  size;
	u32                  dev_addr;
	u32                  region;
};

#define call_res_op(c, op, ...)                  \
	(((c) && (c)->res_ops && (c)->res_ops->op) ? \
	((c)->res_ops->op(__VA_ARGS__)) : 0)

struct msm_vidc_resources_ops {
	int (*init)(struct msm_vidc_core *core);

	int (*reset_bridge)(struct msm_vidc_core *core);
	int (*reset_control_acquire)(struct msm_vidc_core *core,
				     const char *name);
	int (*reset_control_release)(struct msm_vidc_core *core,
				     const char *name);
	int (*reset_control_assert)(struct msm_vidc_core *core,
				    const char *name);
	int (*reset_control_deassert)(struct msm_vidc_core *core,
				      const char *name);

	int (*gdsc_init)(struct msm_vidc_core *core);
	int (*gdsc_on)(struct msm_vidc_core *core, const char *name);
	int (*gdsc_off)(struct msm_vidc_core *core, const char *name);
	int (*gdsc_hw_ctrl)(struct msm_vidc_core *core);
	int (*gdsc_sw_ctrl)(struct msm_vidc_core *core);

	int (*llcc)(struct msm_vidc_core *core, bool enable);
	int (*set_bw)(struct msm_vidc_core *core, unsigned long bw_ddr,
		      unsigned long bw_llcc);
	int (*set_clks)(struct msm_vidc_core *core, u64 rate);

	int (*clk_disable)(struct msm_vidc_core *core, const char *name);
	int (*clk_enable)(struct msm_vidc_core *core, const char *name);
	int (*clk_set_flag)(struct msm_vidc_core *core,
			    const char *name,
			    enum msm_vidc_branch_mem_flags flag);
	int (*clk_print_residency_stats)(struct msm_vidc_core *core);
	int (*clk_reset_residency_stats)(struct msm_vidc_core *core);
	int (*clk_update_residency_stats)(struct msm_vidc_core *core,
					  struct clock_info *cl, u64 rate);
};

const struct msm_vidc_resources_ops *get_resources_ops(void);

#endif
