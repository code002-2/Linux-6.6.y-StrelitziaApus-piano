// SPDX-License-Identifier: GPL-2.0
/*
 * StrelitziaApus-scheduling  v5
 *
 * 内核态线程亲和性调度器 —— 规则、类别、场景全部内建，不依赖任何用户态模块。
 *
 * ┌─ 完全自足的默认行为 ─────────────────────────────────────┐
 * │ 编译进内核一张默认规则表（sa_default_rules），init 时装载，      │
 * │ 开机的每个进程/线程都由钩子自动纳入管理，无需 Magisk 模块。      │
 * └──────────────────────────────────────────────────────┘
 *
 * 钩子
 *   android_rvh_sched_fork_init   fork 现场直接设置（新任务未上 CPU）
 *   android_rvh_set_task_comm     exec / prctl / pthread_setname_np 触发，
 *                                 登记后由工作队列应用（避免持锁重入）
 *   追溯扫描                      启动后延时扫一次 + 写 apply 手动触发
 *
 * 只用 GKI 已导出的接口，不新增导出符号，module_layout 与所有符号 CRC 不变。
 *
 * ── 规则语法（内建默认表与 debugfs 覆盖用同一套）──
 *   class <名字> <掩码>
 *   <uid|*> <comm> <类别|掩码> [exact|prefix] [场景[,场景...]]
 *
 *   场景: idle / touch / boost，省略=全部
 *   掩码: 0x0f 或 0-3
 *   comm 写 "." 表示该 uid 的兜底
 *
 * ── debugfs 覆盖 ──
 *   rules   写入新的规则表（整体替换内建默认表）
 *   scene   切场景 idle/touch/boost（自动重扫）
 *   apply   手动触发追溯扫描
 *   写 "reset" 回到内建默认表；写 "clear" 停用并还原所有线程
 */

#define pr_fmt(fmt) "sa-sched: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/jump_label.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/sched/signal.h>
#include <linux/kstrtox.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched/types.h>

#include <trace/hooks/sched.h>


/*
 * uclamp_eff_value() 是导出符号（0xe355c9a3），但原型只在 kernel/sched/sched.h
 * 里，模块拿不到 —— 这里自己声明一份。
 */
#ifdef CONFIG_UCLAMP_TASK
extern unsigned int uclamp_eff_value(struct task_struct *p,
				     enum uclamp_id clamp_id);
#else
static inline unsigned int uclamp_eff_value(struct task_struct *p,
					    enum uclamp_id clamp_id)
{
	return 0;
}
#endif

#define SA_MAX_RULES	512
#define SA_MAX_CLASSES	16
#define SA_MAX_SWEEP	1024
#define SA_COMM_LEN	TASK_COMM_LEN
#define SA_MASK_BUF	96
#define SA_WQ_MAX	1024
#define SA_BOOT_SWEEP_MS 2000		/* 开机后延时扫一次 */

/* 场景位 */
enum sa_scene {
	SA_SCENE_IDLE	= 0,
	SA_SCENE_TOUCH	= 1,
	SA_SCENE_BOOST	= 2,
	SA_SCENE_NR,
};

#define SA_SCENE_ALL	((1u << SA_SCENE_NR) - 1)

enum sa_match {
	SA_MATCH_EXACT = 0,
	SA_MATCH_PREFIX,
};

struct sa_class {
	char			name[16];
	cpumask_var_t		mask;
};

struct sa_rule {
	kuid_t			uid;
	bool			any_uid;	/* "*"：不区分 uid */
	char			comm[SA_COMM_LEN];	/* "." = 兜底 */
	enum sa_match		match;
	unsigned int		scenes;
	u32			uclamp_min;	/* 0 = 不设置 */
	cpumask_var_t		mask;
};

struct sa_table {
	struct sa_rule		*rules;
	unsigned int		nr;
	struct sa_class		classes[SA_MAX_CLASSES];
	unsigned int		nr_classes;
};

struct sa_pending {
	struct work_struct	work;
	struct task_struct	*task;
	cpumask_var_t		mask;
	u32			uclamp_min;
};

/* ---------------------------------------------------------------- 内建默认规则
 *
 * SM8750P 实测拓扑：policy0 = CPU0-5（max 3.53 GHz），policy6 = CPU6-7（max 4.32 GHz）
 *
 * 默认只在 touch / boost 场景把渲染类线程提到大核；idle 场景完全不动，
 * 这样即使规则不理想，最坏情况也只是触摸时多费一点电。
 * 想更省电可以把 touch 也去掉，只留 boost。
 */
static const char sa_default_rules[] =
	"class big   0xc0\n"
	"class mid   0x3f\n"
	"class all   0xff\n"
	"\n"
	"# ── 渲染路径：常驻大核 + uclamp 拉满 ──\n"
	"* RenderThread     big prefix u=1024\n"
	"* hwui             big prefix u=1024\n"
	"* GLThread         big prefix u=1024\n"
	"* RHIThread        big prefix u=768\n"
	"* RHIGpu           big prefix u=768\n"
	"* VulkanRHI        big prefix u=768\n"
	"* Compositor       big prefix\n"
	"* SurfaceSwapChain big prefix\n"
	"\n"
	"# ── 游戏主线程/引擎线程 ──\n"
	"* UnityMain        big exact  u=1024\n"
	"* UnityGfxDevice   big prefix u=768\n"
	"* GameThread       big prefix u=1024\n"
	"* NativeThread     big prefix\n"
	"* dotnet           big prefix\n"
	"\n"
	"# ── Flutter 引擎（小米桌面等）──\n"
	"* 1.ui             big prefix u=1024\n"
	"* 1.raster         big prefix u=1024\n"
	"* DartWorker       big prefix u=512\n";

static DEFINE_SPINLOCK(sa_lock);
static struct sa_table	 sa_tab;
static DEFINE_STATIC_KEY_FALSE(sa_active);
static struct workqueue_struct *sa_wq;
static atomic_t		 sa_pending_cnt = ATOMIC_INIT(0);
static unsigned int	 sa_scene_cur = SA_SCENE_IDLE;
static bool		 sa_uclamp_on = true;
static DEFINE_PER_CPU(int, sa_in_hook);	/* 纠偏钩子重入保护 */

static struct dentry	*sa_debugfs;

/* ---------------------------------------------------------------- 工具 */

static inline unsigned long sa_uid_of(struct task_struct *p)
{
	const struct cred *cred = READ_ONCE(p->real_cred);

	if (cred)
		return __kuid_val(cred->uid) % 100000;

	return 0;
}

static bool sa_comm_match(const struct sa_rule *r, const char *comm)
{
	if (r->comm[0] == '.' && r->comm[1] == '\0')
		return true;
	if (r->match == SA_MATCH_EXACT)
		return !strncmp(r->comm, comm, SA_COMM_LEN);

	return !strncmp(r->comm, comm, strlen(r->comm));
}

/* 调用者需持有 sa_lock。优先级：精确 > 名前缀更长 > 兜底 */
static const struct sa_rule *sa_lookup(unsigned long uid, const char *comm)
{
	const struct sa_rule *best = NULL;
	unsigned int i;

	for (i = 0; i < sa_tab.nr; i++) {
		const struct sa_rule *r = &sa_tab.rules[i];

		if (!r->any_uid && __kuid_val(r->uid) != uid)
			continue;
		if (!(r->scenes & (1u << sa_scene_cur)))
			continue;
		if (!sa_comm_match(r, comm))
			continue;

		if (!best) {
			best = r;
			continue;
		}
		if (best->comm[0] == '.' && best->comm[1] == '\0') {
			best = r;
		} else if (r->comm[0] == '.' && r->comm[1] == '\0') {
			continue;
		} else if (r->match < best->match) {
			best = r;
		} else if (r->match == best->match &&
			   strlen(r->comm) > strlen(best->comm)) {
			best = r;
		}
	}

	return best;
}

/* 是否被任何规则覆盖（忽略场景）—— 还原时判断线程是否被我们改过 */
static bool sa_matches_any(unsigned long uid, const char *comm)
{
	unsigned int i;

	for (i = 0; i < sa_tab.nr; i++) {
		const struct sa_rule *r = &sa_tab.rules[i];

		if (!r->any_uid && __kuid_val(r->uid) != uid)
			continue;
		if (sa_comm_match(r, comm))
			return true;
	}
	return false;
}

static inline bool sa_skip(struct task_struct *p)
{
	if (!p || (p->flags & PF_KTHREAD))
		return true;
	if (p->policy == SCHED_FIFO || p->policy == SCHED_RR)
		return true;
	return false;
}

static void sa_mask_to_str(const struct cpumask *m, char *buf, size_t len)
{
	int cpu, n = 0;

	buf[0] = '\0';
	for_each_cpu(cpu, m) {
		n += scnprintf(buf + n, len - n, "%s%d", n ? "," : "", cpu);
		if (n >= len)
			break;
	}
	if (!n)
		scnprintf(buf, len, "-");
}

static int sa_parse_mask(const char *str, struct cpumask *mask)
{
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
		unsigned long long v;
		int cpu;

		if (kstrtoull(str + 2, 16, &v))
			return -EINVAL;
		cpumask_clear(mask);
		for_each_set_bit(cpu, (unsigned long *)&v, 64) {
			if (cpu >= nr_cpu_ids)
				break;
			cpumask_set_cpu(cpu, mask);
		}
		return 0;
	}
	return cpumask_parse(str, mask);
}

static int sa_resolve_mask(struct sa_table *t, const char *tok,
			   struct cpumask *out)
{
	unsigned int i;

	for (i = 0; i < t->nr_classes; i++) {
		if (!strcmp(t->classes[i].name, tok)) {
			cpumask_copy(out, t->classes[i].mask);
			return 0;
		}
	}
	return sa_parse_mask(tok, out);
}

/* 支持 "touch"、"touch,boost"、"all" */
static unsigned int sa_parse_scenes(const char *tok)
{
	unsigned int mask = 0;
	char buf[32];
	char *p, *one;

	if (!tok || !*tok || !strcmp(tok, "all"))
		return SA_SCENE_ALL;

	strscpy(buf, tok, sizeof(buf));
	p = buf;
	while ((one = strsep(&p, ","))) {
		if (!strcmp(one, "idle"))
			mask |= 1u << SA_SCENE_IDLE;
		else if (!strcmp(one, "touch"))
			mask |= 1u << SA_SCENE_TOUCH;
		else if (!strcmp(one, "boost"))
			mask |= 1u << SA_SCENE_BOOST;
		else if (!strcmp(one, "all"))
			mask = SA_SCENE_ALL;
	}
	return mask ? mask : SA_SCENE_ALL;
}

/* ------------------------------------------------------- 工作队列应用 */

/*
 * 用 sched_setattr_nocheck 设 uclamp_min：让 WALT/schedutil 立刻把线程
 * 当成"需要满算力"来对待 —— 比 Uperf 直接给 RT 98 温和，出错最坏是费电。
 */
static void sa_apply_uclamp(struct task_struct *p, u32 min)
{
	struct sched_attr attr = {
		.size		= sizeof(attr),
		.sched_policy	= SCHED_NORMAL,
		.sched_flags	= SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_KEEP_ALL,
		.sched_nice	= PRIO_TO_NICE(p->static_prio),
		.sched_util_min	= min,
	};

	if (!sa_uclamp_on)
		return;

	/* 只对普通调度类动手；RT/DEADLINE 或内核线程一律跳过 */
	if (!p || (p->flags & PF_KTHREAD))
		return;
	if (p->policy != SCHED_NORMAL && p->policy != SCHED_BATCH &&
	    p->policy != SCHED_IDLE)
		return;
	if (min > 1024)
		min = 1024;

	attr.sched_util_min = min;
	sched_setattr_nocheck(p, &attr);
}

static void sa_apply_work(struct work_struct *w)
{
	struct sa_pending *pd = container_of(w, struct sa_pending, work);
	struct task_struct *p = pd->task;

	if (p && pid_alive(p)) {
		set_cpus_allowed_ptr(p, pd->mask);
		if (pd->uclamp_min)
			sa_apply_uclamp(p, pd->uclamp_min);
	}

	if (p)
		put_task_struct(p);
	free_cpumask_var(pd->mask);
	kfree(pd);
	atomic_dec(&sa_pending_cnt);
}

static void sa_queue_apply(struct task_struct *p, const struct cpumask *mask,
			   u32 uclamp_min)
{
	struct sa_pending *pd;

	if (!p || !mask)
		return;
	if (cpumask_equal(&p->cpus_mask, mask) && !uclamp_min)
		return;
	if (atomic_read(&sa_pending_cnt) >= SA_WQ_MAX)
		return;

	pd = kmalloc(sizeof(*pd), GFP_ATOMIC);
	if (!pd)
		return;
	if (!alloc_cpumask_var(&pd->mask, GFP_ATOMIC)) {
		kfree(pd);
		return;
	}

	cpumask_copy(pd->mask, mask);
	pd->uclamp_min = uclamp_min;
	pd->task = p;
	get_task_struct(p);
	atomic_inc(&sa_pending_cnt);

	INIT_WORK(&pd->work, sa_apply_work);
	queue_work(sa_wq, &pd->work);
}

/* ------------------------------------------------------- 追溯扫描 */

/* 扫描时的掩码暂存（cpumask_var_t 形态随配置而变，动态分配更稳） */
static cpumask_var_t	*sa_batch_mask;

/*
 * 遍历现有全部线程，命中规则的重绑一次。
 * 两趟：RCU 下只做匹配并取引用，解锁后再动亲和性（后者可能睡眠）。
 */
static void sa_sweep_work(struct work_struct *w)
{
	static struct task_struct *batch[SA_MAX_SWEEP];
	static u32 batch_uclamp[SA_MAX_SWEEP];
	unsigned int n = 0, i, hit = 0;
	struct task_struct *g, *t;

	rcu_read_lock();
	for_each_process_thread(g, t) {
		const struct sa_rule *r;

		if (n >= SA_MAX_SWEEP)
			break;
		if (sa_skip(t))
			continue;

		spin_lock(&sa_lock);
		r = sa_lookup(sa_uid_of(t), t->comm);
		spin_unlock(&sa_lock);
		if (!r)
			continue;

		get_task_struct(t);
		batch[n] = t;
		if (!alloc_cpumask_var(&sa_batch_mask[n], GFP_ATOMIC)) {
			put_task_struct(t);
			continue;
		}
		cpumask_copy(sa_batch_mask[n], r->mask);
		batch_uclamp[n] = r->uclamp_min;
		n++;
	}
	rcu_read_unlock();

	for (i = 0; i < n; i++) {
		if (pid_alive(batch[i])) {
			if (!cpumask_equal(&batch[i]->cpus_mask, sa_batch_mask[i])) {
				set_cpus_allowed_ptr(batch[i], sa_batch_mask[i]);
				hit++;
			}
			if (batch_uclamp[i])
				sa_apply_uclamp(batch[i], batch_uclamp[i]);
		}
		put_task_struct(batch[i]);
		free_cpumask_var(sa_batch_mask[i]);
	}

	pr_info("sweep: %u matched, %u re-applied (scene=%u)\n", n, hit,
		sa_scene_cur);
}

static DECLARE_WORK(sa_sweep, sa_sweep_work);

/* 开机后延时扫一次，兜住 init 到钩子生效之间新建的线程 */
static void sa_boot_sweep_work(struct work_struct *w)
{
	queue_work(sa_wq, &sa_sweep);
}

static DECLARE_DELAYED_WORK(sa_boot_sweep, sa_boot_sweep_work);

/*
 * 还原：把当前规则覆盖到的线程全部放回 cpu_present_mask。
 * clear 之前调用，否则线程会停在我们上次设置的簇上。
 */
static void sa_revert_all(void)
{
	static struct task_struct *batch[SA_MAX_SWEEP];
	unsigned int n = 0, i;
	struct task_struct *g, *t;

	rcu_read_lock();
	for_each_process_thread(g, t) {
		bool hit;

		if (n >= SA_MAX_SWEEP)
			break;
		if (sa_skip(t))
			continue;

		spin_lock(&sa_lock);
		hit = sa_matches_any(sa_uid_of(t), t->comm);
		spin_unlock(&sa_lock);
		if (!hit)
			continue;

		get_task_struct(t);
		batch[n++] = t;
	}
	rcu_read_unlock();

	for (i = 0; i < n; i++) {
		if (pid_alive(batch[i]) &&
		    !cpumask_equal(&batch[i]->cpus_mask, cpu_present_mask))
			set_cpus_allowed_ptr(batch[i], cpu_present_mask);
		put_task_struct(batch[i]);
	}

	pr_info("revert: %u task(s) restored\n", n);
}

/* ------------------------------------------------------------- 钩子 */

static void sa_on_fork_init(void *data, struct task_struct *p)
{
	const struct sa_rule *r = NULL;
	cpumask_var_t mask;
	u32 uclamp = 0;

	if (!static_branch_unlikely(&sa_active))
		return;
	if (sa_skip(p))
		return;

	/* 规则只在锁内取一份快照，避免持锁时做会睡眠的操作 */
	if (!alloc_cpumask_var(&mask, GFP_ATOMIC))
		return;

	spin_lock(&sa_lock);
	r = sa_lookup(sa_uid_of(p), p->comm);
	if (r) {
		cpumask_copy(mask, r->mask);
		uclamp = r->uclamp_min;
	}
	spin_unlock(&sa_lock);

	if (r) {
		set_cpus_allowed_ptr(p, mask);
		/* uclamp 需要拿 rq 锁，放到工作队列里做 */
		if (uclamp)
			sa_queue_apply(p, mask, uclamp);
	}
	free_cpumask_var(mask);
}

static void sa_on_set_task_comm(void *data, struct task_struct *p, bool exec)
{
	const struct sa_rule *r = NULL;
	cpumask_var_t mask;
	u32 uclamp = 0;

	if (!static_branch_unlikely(&sa_active))
		return;
	if (sa_skip(p))
		return;

	if (!alloc_cpumask_var(&mask, GFP_ATOMIC))
		return;

	spin_lock(&sa_lock);
	r = sa_lookup(sa_uid_of(p), p->comm);
	if (r) {
		cpumask_copy(mask, r->mask);
		uclamp = r->uclamp_min;
	}
	spin_unlock(&sa_lock);

	if (r)
		sa_queue_apply(p, mask, uclamp);
	free_cpumask_var(mask);
}


/*
 * 纠偏：Android 的 cpuset、vip_sched、mi_game 都可能在我们之后重设亲和性。
 * 命中规则的线程被改走时拉回来。
 * 自己调用 set_cpus_allowed_ptr() 会再次触发本钩子，用 per-cpu 标志防重入。
 */
static void sa_on_setaffinity(void *data, struct task_struct *p,
			      const struct cpumask *in_mask, int *retval)
{
	const struct sa_rule *r = NULL;
	cpumask_var_t mask;
	int cpu;

	if (!static_branch_unlikely(&sa_active))
		return;
	if (sa_skip(p))
		return;
	if (retval && *retval)
		return;
	if (this_cpu_read(sa_in_hook))
		return;

	if (!alloc_cpumask_var(&mask, GFP_ATOMIC))
		return;

	spin_lock(&sa_lock);
	r = sa_lookup(sa_uid_of(p), p->comm);
	if (r)
		cpumask_copy(mask, r->mask);
	spin_unlock(&sa_lock);

	if (r && !cpumask_equal(in_mask, mask)) {
		cpu = get_cpu();
		this_cpu_write(sa_in_hook, 1);
		set_cpus_allowed_ptr(p, mask);
		this_cpu_write(sa_in_hook, 0);
		put_cpu();
	}
	free_cpumask_var(mask);
}


/*
 * cpuset 走的是 set_cpus_allowed_ptr() 而不是 sched_setaffinity()，
 * 上面那个钩子抓不到。这里补一个：如果新掩码仍然覆盖我们的目标，
 * 说明我们被"放宽"了（比如 cpuset 把 6,7 改回 0-7），拉回去；
 * 如果新掩码里根本没有我们的目标（进了受限 cpuset），就不强求。
 */
static void sa_on_set_cpus_allowed(void *data, const struct cpumask *cpu_valid_mask,
				   const struct cpumask *new_mask,
				   struct task_struct *p, unsigned int *dest_cpu)
{
	const struct sa_rule *r;

	if (!static_branch_unlikely(&sa_active))
		return;
	if (sa_skip(p))
		return;
	if (this_cpu_read(sa_in_hook))
		return;
	if (!new_mask)
		return;

	spin_lock(&sa_lock);
	r = sa_lookup(sa_uid_of(p), p->comm);
	if (r && cpumask_subset(r->mask, new_mask) &&
	    !cpumask_subset(new_mask, r->mask))
		sa_queue_apply(p, r->mask, sa_uclamp_on ? r->uclamp_min : 0);
	spin_unlock(&sa_lock);
}

/* ------------------------------------------------------------- 解析装载 */

static void sa_free_table(struct sa_table *t)
{
	unsigned int i;

	for (i = 0; i < t->nr; i++)
		free_cpumask_var(t->rules[i].mask);
	for (i = 0; i < t->nr_classes; i++)
		free_cpumask_var(t->classes[i].mask);
	kfree(t->rules);
	t->rules = NULL;
	t->nr = 0;
	t->nr_classes = 0;
}

/*
 * 解析并安装一张规则表（内建默认表与用户写入共用）。
 * text 由调用者保证以 NUL 结尾且可写。
 */
static int sa_load_table(char *text)
{
	struct sa_table new = { };
	char *cursor = text, *line;
	int ret = 0;

	new.rules = kcalloc(SA_MAX_RULES, sizeof(*new.rules), GFP_KERNEL);
	if (!new.rules)
		return -ENOMEM;

	while ((line = strsep(&cursor, "\n"))) {
		char t0[32] = { }, t1[SA_COMM_LEN] = { }, t2[SA_MASK_BUF] = { };
		char t3[16] = { }, t4[24] = { }, t5[16] = { };
		int nf;

		if (!*line || *line == '#' || *line == '\r')
			continue;

		nf = sscanf(line, "%31s %15s %95s %15s %23s %15s", t0, t1, t2, t3, t4, t5);

		if (!strcmp(t0, "class")) {
			struct sa_class *c;

			if (nf < 3 || new.nr_classes >= SA_MAX_CLASSES)
				continue;
			c = &new.classes[new.nr_classes];
			strscpy(c->name, t1, sizeof(c->name));
			if (!zalloc_cpumask_var(&c->mask, GFP_KERNEL)) {
				ret = -ENOMEM;
				goto out;
			}
			if (sa_resolve_mask(&new, t2, c->mask)) {
				free_cpumask_var(c->mask);
				continue;
			}
			cpumask_and(c->mask, c->mask, cpu_present_mask);
			if (cpumask_empty(c->mask)) {
				free_cpumask_var(c->mask);
				continue;
			}
			new.nr_classes++;
			continue;
		}

		if (nf < 3 || new.nr >= SA_MAX_RULES)
			continue;

		{
			struct sa_rule *r = &new.rules[new.nr];
			unsigned int uid = 0;

			if (!strcmp(t0, "*")) {
				r->any_uid = true;
			} else if (kstrtouint(t0, 10, &uid)) {
				continue;
			}
			r->uid = KUIDT_INIT(uid);
			strscpy(r->comm, t1, SA_COMM_LEN);

			/* t3: exact/prefix 或直接写场景；含 u= 的 token 视为 uclamp */
			if (!strcmp(t3, "prefix")) {
				r->match = SA_MATCH_PREFIX;
			} else if (!strcmp(t3, "exact")) {
				r->match = SA_MATCH_EXACT;
			} else {
				r->match = SA_MATCH_EXACT;
			}

			/* 从 t3/t4/t5 里挑出场景与 uclamp */
			r->scenes = SA_SCENE_ALL;
			r->uclamp_min = 0;
			{
				const char *toks[3];
				int ti;

				toks[0] = (!strcmp(t3, "prefix") ||
					   !strcmp(t3, "exact")) ? NULL : t3;
				toks[1] = t4;
				toks[2] = t5;
				for (ti = 0; ti < 3; ti++) {
					const char *tk = toks[ti];

					if (!tk || !*tk)
						continue;
					if (tk[0] == 'u' && tk[1] == '=') {
						u32 v = 0;

						if (!kstrtou32(tk + 2, 10, &v))
							r->uclamp_min =
								min_t(u32, v, 1024);
					} else if (!strncmp(tk, "all", 3) ||
						   !strncmp(tk, "idle", 4) ||
						   !strncmp(tk, "touch", 5) ||
						   !strncmp(tk, "boost", 5)) {
						r->scenes = sa_parse_scenes(tk);
					}
				}
			}

			if (!zalloc_cpumask_var(&r->mask, GFP_KERNEL)) {
				ret = -ENOMEM;
				goto out;
			}
			if (sa_resolve_mask(&new, t2, r->mask)) {
				free_cpumask_var(r->mask);
				continue;
			}
			cpumask_and(r->mask, r->mask, cpu_present_mask);
			if (cpumask_empty(r->mask)) {
				free_cpumask_var(r->mask);
				continue;
			}
			new.nr++;
		}
	}

	spin_lock(&sa_lock);
	sa_free_table(&sa_tab);
	sa_tab = new;
	spin_unlock(&sa_lock);

	if (sa_tab.nr)
		static_branch_enable(&sa_active);
	else
		static_branch_disable(&sa_active);

	pr_info("table loaded: %u rule(s), %u class(es)\n",
		sa_tab.nr, sa_tab.nr_classes);
	return 0;

out:
	sa_free_table(&new);
	return ret;
}

/* ------------------------------------------------------------- debugfs */

static int sa_rules_show(struct seq_file *m, void *v)
{
	char buf[SA_MASK_BUF];
	unsigned int i;

	spin_lock(&sa_lock);
	seq_printf(m, "# scene=%s rules=%u classes=%u pending=%d%s\n",
		   sa_scene_cur == SA_SCENE_IDLE ? "idle" :
		   sa_scene_cur == SA_SCENE_TOUCH ? "touch" : "boost",
		   sa_tab.nr, sa_tab.nr_classes, atomic_read(&sa_pending_cnt),
		   static_branch_unlikely(&sa_active) ? "" : " (inactive)");

	for (i = 0; i < sa_tab.nr_classes; i++) {
		sa_mask_to_str(sa_tab.classes[i].mask, buf, sizeof(buf));
		seq_printf(m, "class %s %s\n", sa_tab.classes[i].name, buf);
	}
	for (i = 0; i < sa_tab.nr; i++) {
		const struct sa_rule *r = &sa_tab.rules[i];
		char scenebuf[32] = "";
		char ubuf[16] = "";

		sa_mask_to_str(r->mask, buf, sizeof(buf));
		if (r->scenes & (1u << SA_SCENE_IDLE))
			strlcat(scenebuf, "idle,", sizeof(scenebuf));
		if (r->scenes & (1u << SA_SCENE_TOUCH))
			strlcat(scenebuf, "touch,", sizeof(scenebuf));
		if (r->scenes & (1u << SA_SCENE_BOOST))
			strlcat(scenebuf, "boost,", sizeof(scenebuf));
		if (scenebuf[0])
			scenebuf[strlen(scenebuf) - 1] = '\0';

		if (r->uclamp_min)
			snprintf(ubuf, sizeof(ubuf), " u=%u", r->uclamp_min);
		else
			ubuf[0] = '\0';

		if (r->any_uid)
			seq_printf(m, "* %s %s %s %s%s\n", r->comm, buf,
				   r->match == SA_MATCH_PREFIX ? "prefix" : "exact",
				   scenebuf, ubuf);
		else
			seq_printf(m, "%u %s %s %s %s%s\n", __kuid_val(r->uid),
				   r->comm, buf,
				   r->match == SA_MATCH_PREFIX ? "prefix" : "exact",
				   scenebuf, ubuf);
	}
	spin_unlock(&sa_lock);
	return 0;
}

static int sa_rules_open(struct inode *inode, struct file *file)
{
	return single_open(file, sa_rules_show, NULL);
}

static ssize_t sa_rules_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	char *kbuf;
	int ret;

	if (!count || count > 128 * 1024)
		return -EINVAL;

	kbuf = memdup_user_nul(ubuf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	strim(kbuf);

	if (!strncmp(kbuf, "clear", 5)) {
		sa_revert_all();
		spin_lock(&sa_lock);
		sa_free_table(&sa_tab);
		spin_unlock(&sa_lock);
		static_branch_disable(&sa_active);
		pr_info("cleared (disabled)\n");
		kfree(kbuf);
		return count;
	}

	if (!strncmp(kbuf, "reset", 5)) {
		char *copy = kstrdup(sa_default_rules, GFP_KERNEL);

		if (!copy) {
			kfree(kbuf);
			return -ENOMEM;
		}
		sa_revert_all();
		ret = sa_load_table(copy);
		kfree(copy);
		kfree(kbuf);
		if (ret)
			return ret;
		queue_work(sa_wq, &sa_sweep);
		return count;
	}

	ret = sa_load_table(kbuf);
	kfree(kbuf);
	if (ret)
		return ret;

	queue_work(sa_wq, &sa_sweep);
	return count;
}

static const struct file_operations sa_rules_fops = {
	.owner		= THIS_MODULE,
	.open		= sa_rules_open,
	.read		= seq_read,
	.write		= sa_rules_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int sa_scene_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", sa_scene_cur == SA_SCENE_IDLE ? "idle" :
			       sa_scene_cur == SA_SCENE_TOUCH ? "touch" : "boost");
	return 0;
}

static int sa_scene_open(struct inode *inode, struct file *file)
{
	return single_open(file, sa_scene_show, NULL);
}

static ssize_t sa_scene_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	char buf[16] = { };
	unsigned int s;

	if (!count || count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[strnlen(buf, sizeof(buf))] = '\0';
	strim(buf);

	if (!strncmp(buf, "idle", 4))
		s = SA_SCENE_IDLE;
	else if (!strncmp(buf, "touch", 5))
		s = SA_SCENE_TOUCH;
	else if (!strncmp(buf, "boost", 5))
		s = SA_SCENE_BOOST;
	else
		return -EINVAL;

	if (s != sa_scene_cur) {
		sa_scene_cur = s;
		pr_info("scene -> %s\n", buf);
		queue_work(sa_wq, &sa_sweep);
	}
	return count;
}

static const struct file_operations sa_scene_fops = {
	.owner		= THIS_MODULE,
	.open		= sa_scene_open,
	.read		= seq_read,
	.write		= sa_scene_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t sa_apply_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	queue_work(sa_wq, &sa_sweep);
	return count;
}

static const struct file_operations sa_apply_fops = {
	.owner		= THIS_MODULE,
	.write		= sa_apply_write,
};


static int sa_dump_show(struct seq_file *m, void *v)
{
	struct task_struct *g, *t;
	unsigned int n = 0;

	seq_printf(m, "%-7s %-16s %-14s %-9s %s\n",
		   "pid", "comm", "cpus_allowed", "uclamp_eff", "命中规则");
	rcu_read_lock();
	for_each_process_thread(g, t) {
		const struct sa_rule *r;
		char buf[SA_MASK_BUF];

		if (sa_skip(t))
			continue;
		spin_lock(&sa_lock);
		r = sa_lookup(sa_uid_of(t), t->comm);
		spin_unlock(&sa_lock);
		if (!r)
			continue;

		sa_mask_to_str(&t->cpus_mask, buf, sizeof(buf));
		seq_printf(m, "%-7d %-16s %-14s %-9u %s\n",
			   t->pid, t->comm, buf,
			   uclamp_eff_value(t, 0), r->comm);
		if (++n >= 200)
			break;
	}
	rcu_read_unlock();
	seq_printf(m, "\n# %u task(s) matched\n", n);
	return 0;
}

static int sa_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, sa_dump_show, NULL);
}

static const struct file_operations sa_dump_fops = {
	.owner		= THIS_MODULE,
	.open		= sa_dump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t sa_enable_write(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	char buf[8] = { };

	if (!count || count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	if (buf[0] == '0') {
		sa_revert_all();
		static_branch_disable(&sa_active);
		pr_info("disabled by user\n");
	} else {
		if (sa_tab.nr)
			static_branch_enable(&sa_active);
		queue_work(sa_wq, &sa_sweep);
		pr_info("enabled by user\n");
	}
	return count;
}

static const struct file_operations sa_enable_fops = {
	.owner		= THIS_MODULE,
	.write		= sa_enable_write,
};


static int sa_uclamp_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", sa_uclamp_on ? 1 : 0);
	return 0;
}

static int sa_uclamp_open(struct inode *inode, struct file *file)
{
	return single_open(file, sa_uclamp_show, NULL);
}

static ssize_t sa_uclamp_write(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	char buf[8] = { };

	if (!count || count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	sa_uclamp_on = (buf[0] == '1');
	pr_info("uclamp %s\n", sa_uclamp_on ? "on" : "off");
	queue_work(sa_wq, &sa_sweep);
	return count;
}

static const struct file_operations sa_uclamp_fops = {
	.owner		= THIS_MODULE,
	.open		= sa_uclamp_open,
	.read		= seq_read,
	.write		= sa_uclamp_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* ------------------------------------------------------------- 模块入口 */

static int __init sa_init(void)
{
	int ret = -ENOMEM;
	char *copy;

	sa_wq = alloc_workqueue("sa-sched", WQ_UNBOUND | WQ_HIGHPRI, 1);
	if (!sa_wq)
		return -ENOMEM;

	sa_batch_mask = kcalloc(SA_MAX_SWEEP, sizeof(cpumask_var_t), GFP_KERNEL);
	if (!sa_batch_mask)
		goto err_wq;

	sa_debugfs = debugfs_create_dir("strelitzia_scheduling", NULL);
	debugfs_create_file("rules", 0600, sa_debugfs, NULL, &sa_rules_fops);
	debugfs_create_file("scene", 0600, sa_debugfs, NULL, &sa_scene_fops);
	debugfs_create_file("apply", 0200, sa_debugfs, NULL, &sa_apply_fops);
	debugfs_create_file("dump", 0400, sa_debugfs, NULL, &sa_dump_fops);
	debugfs_create_file("enable", 0200, sa_debugfs, NULL, &sa_enable_fops);
	debugfs_create_file("uclamp", 0600, sa_debugfs, NULL, &sa_uclamp_fops);

	/* 内建默认规则：开机即生效，不需要任何用户态配合 */
	copy = kstrdup(sa_default_rules, GFP_KERNEL);
	if (copy) {
		ret = sa_load_table(copy);
		kfree(copy);
		if (ret)
			pr_err("builtin rules failed: %d\n", ret);
	}

	ret = register_trace_android_rvh_sched_fork_init(sa_on_fork_init, NULL);
	if (ret) {
		pr_err("register sched_fork_init failed: %d\n", ret);
		goto err;
	}

	ret = register_trace_android_rvh_set_task_comm(sa_on_set_task_comm, NULL);
	if (ret) {
		pr_err("register set_task_comm failed: %d\n", ret);
		goto err;
	}

	ret = register_trace_android_rvh_set_cpus_allowed_by_task(
		sa_on_set_cpus_allowed, NULL);
	if (ret)
		pr_warn("register set_cpus_allowed_by_task failed: %d\n", ret);

	ret = register_trace_android_rvh_sched_setaffinity(sa_on_setaffinity, NULL);
	if (ret) {
		pr_warn("register sched_setaffinity failed: %d (纠偏关闭)\n", ret);
		ret = 0;
	}

	schedule_delayed_work(&sa_boot_sweep, msecs_to_jiffies(SA_BOOT_SWEEP_MS));

	pr_info("ready (v5: builtin rules, %u entries)\n", sa_tab.nr);
	return 0;

err:
	debugfs_remove_recursive(sa_debugfs);
	kfree(sa_batch_mask);
err_wq:
	destroy_workqueue(sa_wq);
	return ret;
}

/*
 * 刻意不提供 module_exit：restricted vendor hook 注册后无法反注册，
 * 允许 rmmod 会让钩子指向已释放的代码段。内核在 exit 缺失时拒绝卸载。
 */
module_init(sa_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("code002-2");
MODULE_DESCRIPTION("StrelitziaApus-scheduling: builtin per-thread CPU affinity scheduler");
