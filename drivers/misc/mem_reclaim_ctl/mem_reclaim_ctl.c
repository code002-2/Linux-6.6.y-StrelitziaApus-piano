#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/atomic/atomic-instrumented.h>

#include <trace/hooks/mm.h>

enum mi_folio_references {
	MI_FOLIOREF_RECLAIM,
	MI_FOLIOREF_RECLAIM_CLEAN,
	MI_FOLIOREF_KEEP,
	MI_FOLIOREF_ACTIVATE,
};

static unsigned int mi_mapcount_thres __read_mostly = 32;
static bool mi_rmap_check_enable __read_mostly = true;
//static bool debug __read_mostly = false;
static struct kobject *kobj;

#define MI_TOO_MANY_SKIPPED_SHIFT 4
#define MI_SKIP_THRES_MIN 3
#define MI_SKIP_THRES_LOW 2
#define MI_SKIP_THRES_HIGH 1

static void mi_folio_check_mapcount(void *data, struct folio *folio, unsigned long nr_scanned,
	s8 priority, u64 *nr_skipped, int *control)
{
	int mapcount = 0;
	int activate_thres = 0;
	int keep_thres = 0;
	bool too_many_skipped = false;

	if (!mi_rmap_check_enable) {
                *control = MI_FOLIOREF_RECLAIM;
                return;
        }

	if (nr_scanned < *nr_skipped)
		*nr_skipped = 0;

	if ((priority < DEF_PRIORITY - 2) && ((nr_scanned >> MI_TOO_MANY_SKIPPED_SHIFT) > 1)) {
		too_many_skipped = *nr_skipped > (nr_scanned >> MI_TOO_MANY_SKIPPED_SHIFT);
		if (too_many_skipped) {
                        *control = MI_FOLIOREF_RECLAIM;
                        return;
                }
	}

	activate_thres = mi_mapcount_thres;
	if (priority != DEF_PRIORITY) {
		if (priority <= DEF_PRIORITY / 4)
			keep_thres = max(mi_mapcount_thres >> MI_SKIP_THRES_MIN, 2u);
		else if (priority <= DEF_PRIORITY / 2)
			keep_thres = max(mi_mapcount_thres >> MI_SKIP_THRES_LOW, 2u);
		else
			keep_thres = max(mi_mapcount_thres >> MI_SKIP_THRES_HIGH, 2u);
	}

	mapcount = folio_mapcount(folio);
	if (mapcount >= activate_thres) {
		unsigned int nr_pages;

		nr_pages = folio_nr_pages(folio);
		*nr_skipped += nr_pages;
        *control = MI_FOLIOREF_ACTIVATE;
		return;
	}
    else if (keep_thres >= 2 && mapcount >= keep_thres) {
        *control = MI_FOLIOREF_KEEP;
        return;
    }
    *control = MI_FOLIOREF_RECLAIM;
	return;
}

static ssize_t show_mi_mapcount_thres(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", mi_mapcount_thres);
}

static ssize_t store_mi_mapcount_thres(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t len)
{
	unsigned int thres;

	if (kstrtouint(buf, 0, &thres))
		return -EINVAL;

	if (thres < 2)
		return -EINVAL;
	mi_mapcount_thres = thres;

	return len;
}

static struct kobj_attribute mi_mapcount_thres_attr = __ATTR(
	mapcount_thres, 0644, show_mi_mapcount_thres, store_mi_mapcount_thres
);

static ssize_t show_mi_rmap_check_enable(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", mi_rmap_check_enable ? "true" : "false");
}

static ssize_t store_mi_rmap_check_enable(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t len)
{
	ssize_t ret;

  	ret = kstrtobool(buf, &mi_rmap_check_enable);
	if (ret)
		return ret;

	return len;
}

static struct kobj_attribute mi_rmap_check_enable_attr = __ATTR(
	enabled, 0644, show_mi_rmap_check_enable, store_mi_rmap_check_enable
);

static struct attribute *mi_rmap_check_attrs[] = {
	&mi_mapcount_thres_attr.attr,
	&mi_rmap_check_enable_attr.attr,
	NULL
};

static struct attribute_group mi_rmap_check_attr_group = {
	.name = "mi_rmap_check",
	.attrs = mi_rmap_check_attrs,
};

static int init_sysfs(void)
{
        kobj = kobject_create_and_add("folio_ref_ctl", &THIS_MODULE->mkobj.kobj);
	if (kobj) {
	        if (sysfs_create_group(kobj, &mi_rmap_check_attr_group))
		        pr_err("mi_rmap_check: failed to create sysfs group\n");
	}

	return 0;
};

static void destroy_sysfs(void)
{
        if (kobj) {
		sysfs_remove_group(kobj, &mi_rmap_check_attr_group);
		kobject_put(kobj);
	}
}

static int __init mi_mem_reclaim_ctl_init(void)
{
        init_sysfs();
        register_trace_android_vh_page_should_be_protected(mi_folio_check_mapcount, NULL);
        pr_info("mi_mem_reclaim_ctl: module init");
        return 0;
}

static void __exit mi_mem_reclaim_ctl_exit(void)
{
        destroy_sysfs();
        unregister_trace_android_vh_page_should_be_protected(mi_folio_check_mapcount, NULL);
        pr_info("mi_mem_reclaim_ctl: module exit");
}

late_initcall(mi_mem_reclaim_ctl_init);
module_exit(mi_mem_reclaim_ctl_exit);
MODULE_LICENSE("GPL");