#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/vmstat.h>
#include <linux/mm_inline.h>
#include <linux/rmap.h>
#include <linux/rwsem.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/printk.h>
#include <linux/psi.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/memcontrol.h>
#include <linux/vmstat.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/signal.h>
#include <trace/hooks/vmscan.h>
#include "../../../../mm/internal.h"

LIST_HEAD(lru_inactive);
static bool mi_async_shrink_lruvec_setup = false;
static struct task_struct *shrink_lruvec_tsk = NULL;
static atomic_t shrink_lruvec_runnable = ATOMIC_INIT(0);
unsigned long shrink_lruvec_folios = 0;
unsigned long shrink_lruvec_folios_max = 0;
unsigned long shrink_lruvec_handle_folios = 0;
wait_queue_head_t shrink_lruvec_wait;
spinlock_t l_inactive_lock;
static struct kobject *kobj;

#define SHRINK_LRUVECD_HIGH (0x1000)

#define PG_nolockdelay (__NR_PAGEFLAGS + 2)
#define PG_skipped_lock (__NR_PAGEFLAGS + 3)

#define SetFolioNoLockDelay(folio) set_bit(PG_nolockdelay, &(folio)->flags)
#define TestFolioNoLockDelay(folio) test_bit(PG_nolockdelay, &(folio)->flags)
#define TestClearFolioNoLockDelay(folio) test_and_clear_bit(PG_nolockdelay, &(folio)->flags)
#define ClearFolioNoLockDelay(folio) clear_bit(PG_nolockdelay, &(folio)->flags)

#define SetFolioSkippedLock(folio) set_bit(PG_skipped_lock, &(folio)->flags)
#define TestFolioSkippedLock(folio) test_bit(PG_skipped_lock, &(folio)->flags)
#define TestClearFolioSkippedLock(folio) test_and_clear_bit(PG_skipped_lock, &(folio)->flags)
#define ClearFolioSkippedLock(folio) clear_bit(PG_skipped_lock, &(folio)->flags)

extern unsigned long reclaim_pages(struct list_head *folio_list, bool ignore_references);

static bool process_is_shrink_lruvecd(struct task_struct *tsk)
{
	return (shrink_lruvec_tsk->pid == tsk->pid);
}

static void add_to_lruvecd_inactive_list(struct folio *folio)
{
	list_move(&folio->lru, &lru_inactive);

	/* account how many folios in lru_inactive per recycle */
	shrink_lruvec_folios += folio_nr_pages(folio);
	if (shrink_lruvec_folios > shrink_lruvec_folios_max)
		shrink_lruvec_folios_max = shrink_lruvec_folios;
}

/* enable async thread to run on cores with low cpufreq */
void set_shrink_lruvecd_cpus(void)
{
	struct cpumask mask;
	struct cpumask *cpumask = &mask;
	pg_data_t *pgdat = NODE_DATA(0);
	unsigned int cpu = 0, cpufreq_max_tmp = 0;
	struct cpufreq_policy *policy_max;
	static bool set_cpus_success = false;

	if (unlikely(!mi_async_shrink_lruvec_setup))
		return;

	if (likely(set_cpus_success))
		return;

	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (policy == NULL)
			continue;

		if (policy->cpuinfo.max_freq >= cpufreq_max_tmp) {
			cpufreq_max_tmp = policy->cpuinfo.max_freq;
			policy_max = policy;
		}
	}

	cpumask_copy(cpumask, cpumask_of_node(pgdat->node_id));
	cpumask_andnot(cpumask, cpumask, policy_max->related_cpus);

	if (!cpumask_empty(cpumask)) {
		set_cpus_allowed_ptr(shrink_lruvec_tsk, cpumask);
		set_cpus_success = true;
	}
}

/* async thread */
static int shrink_lruvecd(void *p)
{
	LIST_HEAD(tmp_lru_inactive);
	pg_data_t *pgdat = NULL;
	struct task_struct *tsk = current;
	struct folio *folio, *next;

	pgdat = (pg_data_t *)p;
	tsk->flags |= PF_MEMALLOC | PF_KSWAPD;

	set_freezable();

	while (!kthread_should_stop()) {
		wait_event_freezable(shrink_lruvec_wait,
			(atomic_read(&shrink_lruvec_runnable) == 1));

		set_shrink_lruvecd_cpus();

retry_reclaim:
		spin_lock_irq(&l_inactive_lock);

		if (list_empty(&lru_inactive)) {
			spin_unlock_irq(&l_inactive_lock);
			atomic_set(&shrink_lruvec_runnable, 0);
			continue;
		}

		list_for_each_entry_safe(folio, next, &lru_inactive, lru) {
			list_move(&folio->lru, &tmp_lru_inactive);

			/* tmp variable */
			shrink_lruvec_folios -= folio_nr_pages(folio);

			/* total handle folios */
			shrink_lruvec_handle_folios += folio_nr_pages(folio);
		}
		spin_unlock_irq(&l_inactive_lock);

		reclaim_pages(&tmp_lru_inactive, false);
		goto retry_reclaim;
	}

	tsk->flags &= ~(PF_MEMALLOC | PF_KSWAPD);

	return 0;
}

/* handle ret_folios */
static void handle_trylock_failed_folio(void *data, struct list_head *folio_list)
{
	LIST_HEAD(tmp_lru_inactive);
	struct folio *folio, *next;
	bool shrink_lruvecd_is_full = false;
	bool folios_should_be_reclaim = false;

	if (unlikely(!mi_async_shrink_lruvec_setup))
		return;

	if (list_empty(folio_list))
		return;

	if (unlikely(shrink_lruvec_folios > SHRINK_LRUVECD_HIGH))
		shrink_lruvecd_is_full = true;

	list_for_each_entry_safe(folio, next, folio_list, lru) {
		ClearFolioNoLockDelay(folio);

		if (unlikely(TestClearFolioSkippedLock(folio))) {
			folio_clear_active(folio);
			if (!shrink_lruvecd_is_full)
				list_move(&folio->lru, &tmp_lru_inactive);
		}
	}

	if (unlikely(!list_empty(&tmp_lru_inactive))) {
		/* lock critical section */
		spin_lock_irq(&l_inactive_lock);

		list_for_each_entry_safe(folio, next, &tmp_lru_inactive, lru) {
			if (likely(!shrink_lruvecd_is_full)) {
				folios_should_be_reclaim = true;
				add_to_lruvecd_inactive_list(folio);
			}
		}
		/* unlock critical section */
		spin_unlock_irq(&l_inactive_lock);
	}

	if (shrink_lruvecd_is_full || !folios_should_be_reclaim)
		return;

	if (atomic_read(&shrink_lruvec_runnable) == 1)
		return;

	/* synchronous operation */
	atomic_set(&shrink_lruvec_runnable, 1);
	wake_up_interruptible(&shrink_lruvec_wait);
}

/* set folio flag before rmap */
static void folio_trylock_set(void *data, struct folio *folio)
{
	if (unlikely(!mi_async_shrink_lruvec_setup))
		return;

	ClearFolioSkippedLock(folio);

	/* in case of repeat handle */
	if (unlikely(process_is_shrink_lruvecd(current))) {
		ClearFolioNoLockDelay(folio);
		return;
	}

	SetFolioNoLockDelay(folio);
}

static void do_folio_trylock(void *data, struct folio *folio, struct rw_semaphore *sem, bool *got_lock, bool *success)
{
	*success = false;

	if (unlikely(!mi_async_shrink_lruvec_setup))
		return;

	if (TestClearFolioNoLockDelay(folio)) {
		*success = true;

		if (sem == NULL)
			return;

		/* try lock successful */
		if (down_read_trylock(sem)) {
			*got_lock = true;

		/* try lock failed and skipped */
		} else {
			SetFolioSkippedLock(folio);
			*got_lock = false;
		}
	}
}

static void folio_trylock_get_result(void *data, struct folio *folio, bool *trylock_fail)
{
	ClearFolioNoLockDelay(folio);

	if (unlikely(!mi_async_shrink_lruvec_setup) ||
			unlikely(process_is_shrink_lruvecd(current))) {
		*trylock_fail = false;
		return;
	}

	/* folio try lock failed and skipped */
	if (TestFolioSkippedLock(folio))
		*trylock_fail = true;
}

static void folio_trylock_clear(void *data, struct folio *folio)
{
	ClearFolioNoLockDelay(folio);
	ClearFolioSkippedLock(folio);
}

static int async_kshrink_lruvecd_status_show(struct seq_file *m, void *arg)
{
	seq_printf(m,
		   "mi_async_kshrink_lruvecd_setup:     %s\n"
		   "mi_shrink_lruvec_folios:     %lu\n"
		   "mi_shrink_lruvec_handle_folios:     %lu\n"
		   "mi_shrink_lruvec_folios_max:     %lu\n",
		   mi_async_shrink_lruvec_setup ? "enable" : "disable",
		   shrink_lruvec_folios,
		   shrink_lruvec_handle_folios,
		   shrink_lruvec_folios_max);
	seq_putc(m, '\n');

	return 0;
}

static ssize_t show_mi_async_shrink_lruvecd_enable(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", mi_async_shrink_lruvec_setup ? "yes" : "no");
}

static ssize_t store_mi_async_shrink_lruvecd_enable(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t len)
{
	ssize_t ret;

	ret = kstrtobool(buf, &mi_async_shrink_lruvec_setup);
	/* convert failed */
	if (ret) {
		return ret;
	}

	return len;
}

static struct kobj_attribute mi_async_kshrink_lruvecd_check_enable_attr = __ATTR(
	enabled, 0644, show_mi_async_shrink_lruvecd_enable, store_mi_async_shrink_lruvecd_enable
);

static struct attribute *mi_async_reclaim_check_attr[] = {
	&mi_async_kshrink_lruvecd_check_enable_attr.attr,
	NULL
};

static struct attribute_group mi_async_reclaim_check_attr_group = {
	.name = "mi_async_kshrink_lruvecd",
	.attrs = mi_async_reclaim_check_attr,
};

/* init related sysfs */
static int init_async_kshrink_lruvecd_sysfs(void)
{
	kobj = kobject_create_and_add("async_kshrink_lruvecd", &THIS_MODULE->mkobj.kobj);
	if (kobj) {
		if (sysfs_create_group(kobj, &mi_async_reclaim_check_attr_group)) {
			pr_err("async_kshrink_lruvecd: failed to create related sysfs\n");
		}
	}

	return 0;
}

static void destroy_async_kshrink_lruvecd_sysfs(void)
{
	if (kobj) {
		sysfs_remove_group(kobj, &mi_async_reclaim_check_attr_group);
		kobject_put(kobj);
	}
}

static int __init async_kshrink_lruvec_init(void)
{
	pg_data_t *pgdat = NODE_DATA(0);
	int ret;

	init_async_kshrink_lruvecd_sysfs();

	register_trace_android_vh_handle_trylock_failed_folio(handle_trylock_failed_folio, NULL);
	register_trace_android_vh_folio_trylock_set(folio_trylock_set, NULL);
	register_trace_android_vh_folio_trylock_clear(folio_trylock_clear, NULL);
	register_trace_android_vh_get_folio_trylock_result(folio_trylock_get_result, NULL);
	register_trace_android_vh_do_folio_trylock(do_folio_trylock, NULL);

	init_waitqueue_head(&shrink_lruvec_wait);
	spin_lock_init(&l_inactive_lock);

	shrink_lruvec_tsk = kthread_run(shrink_lruvecd, pgdat, "kshrink_lruvecd");
	if (IS_ERR_OR_NULL(shrink_lruvec_tsk)) {
		pr_err("Failed to launch async_shrink_lruvecd on Node 0\n");
		ret = PTR_ERR(shrink_lruvec_tsk);
		shrink_lruvec_tsk = NULL;
		return ret;
	}

	proc_create_single("async_kshrink_lruvecd_status", 0, NULL, async_kshrink_lruvecd_status_show);

	mi_async_shrink_lruvec_setup = true;
	printk(KERN_EMERG "initialize async_kshrink_lruvecd voom voom!!!!\n");

	return 0;
}

void async_kshrink_lruvec_exit(void)
{
	destroy_async_kshrink_lruvecd_sysfs();

	unregister_trace_android_vh_handle_trylock_failed_folio(handle_trylock_failed_folio, NULL);
	unregister_trace_android_vh_folio_trylock_set(folio_trylock_set, NULL);
	unregister_trace_android_vh_folio_trylock_clear(folio_trylock_clear, NULL);
	unregister_trace_android_vh_get_folio_trylock_result(folio_trylock_get_result, NULL);
	unregister_trace_android_vh_do_folio_trylock(do_folio_trylock, NULL);
}

module_init(async_kshrink_lruvec_init);
module_exit(async_kshrink_lruvec_exit);

MODULE_AUTHOR("maminghui5 <maminghui5@xiaomi.com>");
MODULE_LICENSE("GPL");
