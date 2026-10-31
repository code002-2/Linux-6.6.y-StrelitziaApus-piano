/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _MSM_COMM_DEF_H_
#define _MSM_COMM_DEF_H_

#include <linux/types.h>
#include <linux/gunyah/gh_rm_drv.h>

enum op_mode {
	OP_NORMAL,
	OP_DRAINING,
	OP_FLUSH,
	OP_INVALID,
};

enum queue_state {
	QUEUE_INIT,
	QUEUE_ACTIVE = 1,
	QUEUE_START,
	QUEUE_STOP,
	QUEUE_INVALID,
};

#ifdef CONFIG_EVA_TVM

#else	/* LA target starts here */

//#define USE_PRESIL 1

#if defined(CONFIG_EVA_KALAMA) && !defined(USE_PRESIL)
#define CVP_SYNX_ENABLED 1
#define CVP_MMRM_ENABLED 1
#define CVP_FASTRPC_ENABLED 1
#endif	/* End of CONFIG_EVA_KALAMA */

/*SYNX MMRM and FASTRPC are removed temporarily*/
/*Need to put them back when dependencies are available*/
#if defined(CONFIG_EVA_PINEAPPLE) && !defined(USE_PRESIL)
#define CVP_SYNX_ENABLED 1
#endif	/* End of CONFIG_EVA_PINEAPPLE */

#if defined(CONFIG_EVA_SUN) && !defined(USE_PRESIL)
#define CVP_SYNX_ENABLED 1
#define CVP_FASTRPC_ENABLED 1
#define CVP_DSP_ENABLED 1
#define CVP_MMRM_ENABLED 1
#endif /* End of CONFIG_EVA_SUN*/

#if defined(CONFIG_EVA_WAIPIO) && !defined(USE_PRESIL)
#define CVP_MINIDUMP_ENABLED 1
#endif

#ifdef USE_PRESIL
   //#define CVP_SYNX_ENABLED 1
   //#define CVP_FASTRPC_ENABLED 1
   //#define CVP_DSP_ENABLED 1
   //#define CVP_MMRM_ENABLED 1
#endif

#endif	/* End CONFIG_EVA_TVM */

#endif
