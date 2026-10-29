/* SPDX-License-Identifier: GPL-2.0 */
/*
 * List of cgroup subsystems.
 *
 * DO NOT ADD ANY SUBSYSTEM WITHOUT EXPLICIT ACKS FROM CGROUP MAINTAINERS.
 */

/*
 * This file *must* be included with SUBSYS() defined.
 */

#if IS_ENABLED(CONFIG_CPUSETS)
SUBSYS(cpuset)
#endif

#if IS_ENABLED(CONFIG_CGROUP_SCHED)
SUBSYS(cpu)
#endif

#if IS_ENABLED(CONFIG_CGROUP_CPUACCT)
SUBSYS(cpuacct)
#endif

#if IS_ENABLED(CONFIG_BLK_CGROUP)
SUBSYS(io)
#endif

#if IS_ENABLED(CONFIG_MEMCG)
SUBSYS(memory)
#endif


#if IS_ENABLED(CONFIG_CGROUP_FREEZER)
SUBSYS(freezer)
#endif

#if IS_ENABLED(CONFIG_CGROUP_NET_CLASSID) && \
	!IS_ENABLED(CONFIG_ANDROID_CGROUP_NET_CLASSID_KMI_EXT)
SUBSYS(net_cls)
#endif

#if IS_ENABLED(CONFIG_CGROUP_PERF) && \
	!IS_ENABLED(CONFIG_ANDROID_CGROUP_PERF_KMI_EXT)
SUBSYS(perf_event)
#endif

#if IS_ENABLED(CONFIG_CGROUP_NET_PRIO)
SUBSYS(net_prio)
#endif

#if IS_ENABLED(CONFIG_CGROUP_HUGETLB) && \
	!IS_ENABLED(CONFIG_ANDROID_CGROUP_HUGETLB_KMI_EXT)
SUBSYS(hugetlb)
#endif

#if IS_ENABLED(CONFIG_CGROUP_RDMA)
SUBSYS(rdma)
#endif

#if IS_ENABLED(CONFIG_CGROUP_MISC)
SUBSYS(misc)
#endif

/*
 * The following subsystems are not supported on the default hierarchy.
 */
#if IS_ENABLED(CONFIG_CGROUP_DEBUG)
SUBSYS(debug)
#endif

/*
 * PIDS is appended after the frozen Android subsystem IDs.  Its state is
 * kept out of the ABI-visible per-subsystem arrays; see cgroup-defs.h.
 */
#if IS_ENABLED(CONFIG_CGROUP_PIDS) && !defined(CGROUP_SUBSYS_KABI)
SUBSYS(pids)
#endif

/*
 * code002: DEVICES 同样追加在冻结的 Android 子系统 ID 之后，
 * 运行时状态放旁路存储；见 cgroup-defs.h。
 */
#if IS_ENABLED(CONFIG_CGROUP_DEVICE) && !defined(CGROUP_SUBSYS_KABI)
SUBSYS(devices)
#endif

/*
 * DO NOT ADD ANY SUBSYSTEM WITHOUT EXPLICIT ACKS FROM CGROUP MAINTAINERS.
 */
