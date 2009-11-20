/*
 * Copyright (c) 2001-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */

#ifndef __LINUX_PERFMON_KERN_H__
#define __LINUX_PERFMON_KERN_H__
/*
 * This file contains all the definitions of data structures, variables, macros
 * that are to be shared between generic code and arch-specific code
 *
 * For generic only definitions, use perfmon/perfmon_priv.h
 */
#ifdef CONFIG_PERFMON

#include <linux/file.h>
#include <linux/sched.h>
#include <linux/perfmon.h>

/*
 * system adminstrator configuration controls available via
 * the /sys/kernel/perfmon interface
 */
struct pfm_controls {
	u32	debug;		/* debugging control bitmask */
	gid_t	sys_group;	/* gid to create a syswide context */
	gid_t	task_group;	/* gid to create a per-task context */
	u32	flags;		/* control flags (see below) */
	size_t	arg_mem_max;	/* maximum vector argument size */
	size_t	smpl_buffer_mem_max; /* max buf mem, -1 for infinity */
};
extern struct pfm_controls pfm_controls;

/*
 * control flags
 */
#define PFM_CTRL_FL_RW_EXPERT	0x1 /* bypass reserved fields on read/write */

/*
 * software PMD
 */
struct pfm_pmd {
	u64 value;			/* 64-bit value */
	u64 lval;			/* last reset value */
	u64 ovflsw_thres;		/* #ovfls left before switch */
	u64 long_reset;			/* long reset value on overflow */
	u64 short_reset;		/* short reset value on overflow */
	u64 reset_pmds[PFM_PMD_BV];	/* pmds to reset on overflow */
	u64 smpl_pmds[PFM_PMD_BV];	/* pmds to record on overflow */
	u64 mask;			/* range mask for random value */
	u64 ovflsw_ref_thres;		/* #ovfls before next set */
	u64 eventid;			/* opaque event identifier */
	u32 flags;			/* notify/do not notify */
};

/*
 * event_set: encapsulates the full PMU state
 */
struct pfm_event_set {
	struct list_head list;		/* ordered chain of sets */
	u16 id;				/* set identification */
	u16 pad0;			/* paddding */
	u32 flags;			/* public flags */
	u32 priv_flags;			/* private flags (see below) */
	u64 runs;			/* # of activations */
	u32 npend_ovfls;		/* number of pending PMD overflow */
	u32 pad2;			/* padding */
	u64 used_pmds[PFM_PMD_BV];	/* used PMDs */
	u64 povfl_pmds[PFM_PMD_BV];	/* pending overflowed PMDs */
	u64 ovfl_pmds[PFM_PMD_BV];	/* last overflowed PMDs */
	u64 reset_pmds[PFM_PMD_BV];	/* PMDs to reset after overflow */
	u64 ovfl_notify[PFM_PMD_BV];	/* notify on overflow */
	u64 used_pmcs[PFM_PMC_BV];	/* used PMCs */
	u64 pmcs[PFM_MAX_PMCS];		/* PMC values */

	struct pfm_pmd pmds[PFM_MAX_PMDS];

	ktime_t hrtimer_exp;		/* switch timeout reference */
	ktime_t hrtimer_rem;		/* per-thread remainder timeout */

	u64 duration_start;		/* start time in ns */
	u64 duration;			/* total active ns */
};

/*
 * common private event set flags (priv_flags)
 *
 * upper 16 bits: for arch-specific use
 * lower 16 bits: for common use
 */
#define PFM_SETFL_PRIV_MOD_PMDS 0x1 /* PMD register(s) modified */
#define PFM_SETFL_PRIV_MOD_PMCS 0x2 /* PMC register(s) modified */
#define PFM_SETFL_PRIV_SWITCH	0x4 /* must switch set on restart */
#define PFM_SETFL_PRIV_MOD_BOTH	(PFM_SETFL_PRIV_MOD_PMDS \
				| PFM_SETFL_PRIV_MOD_PMCS)

/*
 * context flags
 */
struct pfm_context_flags {
	unsigned int block:1;		/* task blocks on user notifications */
	unsigned int system:1;		/* do system wide monitoring */
	unsigned int no_msg:1;		/* no message sent on overflow */
	unsigned int switch_ovfl:1;	/* switch set on counter ovfl */
	unsigned int switch_time:1;	/* switch set on timeout */
	unsigned int started:1;		/* pfm_start() issued */
	unsigned int work_type:2;	/* type of work for pfm_handle_work */
	unsigned int mmap_nlock:1;	/* no lock in pfm_release_buf_space */
	unsigned int ia64_v20_compat:1;	/* context is IA-64 v2.0 mode */
	unsigned int can_restart:8;	/* allowed to issue a PFM_RESTART */
	unsigned int reset_count:8;	/* number of pending resets */
	unsigned int is_self:1;		/* per-thread and self-montoring */
	unsigned int reserved:5;	/* for future use */
};

/*
 * values for work_type (TIF_PERFMON_WORK must be set)
 */
#define PFM_WORK_NONE	0	/* nothing to do */
#define PFM_WORK_RESET	1	/* reset overflowed counters */
#define PFM_WORK_BLOCK	2	/* block current thread */
#define PFM_WORK_ZOMBIE	3	/* cleanup zombie context */

/*
 * overflow description argument passed to sampling format
 */
struct pfm_ovfl_arg {
	u16 ovfl_pmd;		/* index of overflowed PMD  */
	u16 active_set;		/* set active at the time of the overflow */
	u32 ovfl_ctrl;		/* control flags */
	u64 pmd_last_reset;	/* last reset value of overflowed PMD */
	u64 smpl_pmds_values[PFM_MAX_PMDS]; /* values of other PMDs */
	u64 pmd_eventid;	/* eventid associated with PMD */
	u16 num_smpl_pmds;	/* number of PMDS in smpl_pmd_values */
};
/*
 * depth of message queue
 *
 * Depth cannot be bigger than 255 (see reset_count)
 */
#define PFM_MSGS_ORDER		3 /* log2(number of messages) */
#define PFM_MSGS_COUNT		(1<<PFM_MSGS_ORDER) /* number of messages */
#define PFM_MSGQ_MASK		(PFM_MSGS_COUNT-1)

/*
 * perfmon context state
 */
#define PFM_CTX_UNLOADED	1 /* context is not loaded onto any task */
#define PFM_CTX_LOADED		2 /* context is loaded onto a task */
#define PFM_CTX_MASKED		3 /* context is loaded, monitoring is masked */
#define PFM_CTX_ZOMBIE		4 /* context lost owner but still attached */

/*
 * registers description
 */
struct pfm_regdesc {
	u64 pmcs[PFM_PMC_BV];		/* available PMC */
	u64 pmds[PFM_PMD_BV];		/* available PMD */
	u64 rw_pmds[PFM_PMD_BV];	/* available RW PMD */
	u64 intr_pmds[PFM_PMD_BV];	/* PMD generating intr */
	u64 cnt_pmds[PFM_PMD_BV];	/* PMD counters */
	u16 max_pmc;			/* highest+1 avail PMC */
	u16 max_pmd;			/* highest+1 avail PMD */
	u16 max_rw_pmd;			/* highest+1 avail RW PMD */
	u16 first_intr_pmd;		/* first intr PMD */
	u16 max_intr_pmd;		/* highest+1 intr PMD */
	u16 num_rw_pmd;			/* number of avail RW PMD */
	u16 num_pmcs;			/* number of logical PMCS */
	u16 num_pmds;			/* number of logical PMDS */
	u16 num_counters;		/* number of counting PMD */
};

/*
 * context: contains all the state of a session
 */
struct pfm_context {
	spinlock_t		lock;		/* context protection */

	struct pfm_context_flags flags;
	u32			state;		/* current state */
	struct task_struct 	*task;		/* attached task */

	struct completion       restart_complete;/* block on notification */
	u64 			last_act;	/* last activation */
	u32			last_cpu;   	/* last CPU used (SMP only) */
	u32			cpu;		/* cpu bound to context */

	struct pfm_smpl_fmt	*smpl_fmt;	/* sampling format callbacks */
	void			*smpl_addr;	/* user smpl buffer base */
	size_t			smpl_size;	/* user smpl buffer size */
	void			*smpl_real_addr;/* actual smpl buffer base */
	size_t			smpl_real_size; /* actual smpl buffer size */

	wait_queue_head_t 	msgq_wait;	/* pfm_read() wait queue */

	union pfarg_msg		msgq[PFM_MSGS_COUNT];
	int			msgq_head;
	int			msgq_tail;

	struct fasync_struct	*async_queue;	/* async notification */

	struct pfm_event_set	*active_set;	/* active set */
	struct list_head	set_list;	/* ordered list of sets */

	struct pfm_regdesc	regs;		/* registers available to context */

	/*
	 * save stack space by allocating temporary variables for
	 * pfm_overflow_handler() in pfm_context
	 */
	struct pfm_ovfl_arg 	ovfl_arg;
	u64			tmp_ovfl_notify[PFM_PMD_BV];
};

/*
 * ovfl_ctrl bitmask (used by interrupt handler)
 */
#define PFM_OVFL_CTRL_NOTIFY	0x1	/* notify user */
#define PFM_OVFL_CTRL_RESET	0x2	/* reset overflowed pmds */
#define PFM_OVFL_CTRL_MASK	0x4	/* mask monitoring */
#define PFM_OVFL_CTRL_SWITCH	0x8	/* switch sets */

/*
 * logging
 */
#define PFM_ERR(f, x...)  pr_err(f "\n", ## x)
#define PFM_WARN(f, x...) pr_warning(f "\n", ## x)
#define PFM_LOG(f, x...)  pr_notice(f "\n", ## x)
#define PFM_INFO(f, x...) pr_info(f "\n", ## x)

/*
 * debugging
 *
 * Printk rate limiting is enforced to avoid getting flooded with too many
 * error messages on the console (which could render the machine unresponsive).
 * To get full debug output (turn off ratelimit):
 * 	$ echo 0 >/proc/sys/kernel/printk_ratelimit
 *
 * debug is a bitmask where bits are defined as follows:
 * bit  0: enable non-interrupt code degbug messages
 * bit  1: enable interrupt code debug messages
 */
#ifdef CONFIG_PERFMON_DEBUG
#define _PFM_DBG(lm, f, x...) \
			pr_debug("%s.%d: CPU%d [%d]: " f "\n", \
			       __func__, __LINE__, \
			       smp_processor_id(), current->pid , ## x)

#define PFM_DBG(f, x...) _PFM_DBG(0x1, f, ##x)
#define PFM_DBG_ovfl(f, x...) _PFM_DBG(0x2, f, ## x)
#else
#define PFM_DBG(f, x...)	do {} while (0)
#define PFM_DBG_ovfl(f, x...)	do {} while (0)
#endif

extern struct pfm_pmu_config  *pfm_pmu_conf;
extern int perfmon_disabled;

static inline struct pfm_arch_context *pfm_ctx_arch(struct pfm_context *c)
{
	return (struct pfm_arch_context *)(c+1);
}

int  pfm_get_args(void __user *ureq, size_t sz, size_t lsz, void *laddr,
		  void **req, void **to_free);

int pfm_get_smpl_arg(char __user *fmt_uname, void __user *uaddr, size_t usize,
		     void **arg, struct pfm_smpl_fmt **fmt);

int __pfm_write_pmcs(struct pfm_context *ctx, struct pfarg_pmc *req,
		     int count);
int __pfm_write_pmds(struct pfm_context *ctx, struct pfarg_pmd *req, int count,
		     int compat);
int __pfm_read_pmds(struct pfm_context *ctx, struct pfarg_pmd *req, int count);

int __pfm_load_context(struct pfm_context *ctx, struct pfarg_load *req,
		       struct task_struct *task);
int __pfm_unload_context(struct pfm_context *ctx, int *can_release);

int __pfm_stop(struct pfm_context *ctx, int *release_info);
int  __pfm_restart(struct pfm_context *ctx, int *unblock);
int __pfm_start(struct pfm_context *ctx, struct pfarg_start *start);

void pfm_free_context(struct pfm_context *ctx);

void pfm_smpl_buf_space_release(struct pfm_context *ctx, size_t size);

int pfm_check_task_state(struct pfm_context *ctx, int check_mask,
			 unsigned long *flags, void **resume);
/*
 * check_mask bitmask values for pfm_check_task_state()
 */
#define PFM_CMD_STOPPED		0x01	/* command needs thread stopped */
#define PFM_CMD_UNLOADED	0x02	/* command needs ctx unloaded */
#define PFM_CMD_UNLOAD		0x04	/* command is unload */

int __pfm_create_context(struct pfarg_ctx *req,
			 struct pfm_smpl_fmt *fmt,
			 void *fmt_arg,
			 int mode,
			 struct pfm_context **new_ctx);

struct pfm_event_set *pfm_find_set(struct pfm_context *ctx, u16 set_id,
				   int alloc);

int pfm_pmu_conf_get(int autoload);
void pfm_pmu_conf_put(void);

int pfm_session_allcpus_acquire(void);
void pfm_session_allcpus_release(void);

int pfm_smpl_buf_alloc(struct pfm_context *ctx, size_t rsize);
void pfm_smpl_buf_free(struct pfm_context *ctx);

struct pfm_smpl_fmt *pfm_smpl_fmt_get(char *name);
void pfm_smpl_fmt_put(struct pfm_smpl_fmt *fmt);

void pfm_interrupt_handler(unsigned long iip, struct pt_regs *regs);

void pfm_resume_task(struct task_struct *t, void *data);

#include <linux/perfmon_pmu.h>
#include <linux/perfmon_fmt.h>

extern const struct file_operations pfm_file_ops;
/*
 * upper limit for count in calls that take vector arguments. This is used
 * to prevent for multiplication overflow when we compute actual storage size
 */
#define PFM_MAX_ARG_COUNT(m) (INT_MAX/sizeof(*(m)))

#define cast_ulp(_x) ((unsigned long *)_x)

#define PFM_NORMAL      0
#define PFM_COMPAT      1

void __pfm_exit_thread(void);
void pfm_ctxsw_in(struct task_struct *prev, struct task_struct *next);
void pfm_ctxsw_out(struct task_struct *prev, struct task_struct *next);
void pfm_handle_work(struct pt_regs *regs);
void __pfm_init_percpu(void *dummy);
void pfm_save_pmds(struct pfm_context *ctx, struct pfm_event_set *set);

static inline void pfm_exit_thread(void)
{
	if (current->pfm_context)
		__pfm_exit_thread();
}

/*
 * include arch-specific kernel level definitions
 */
#include <asm/perfmon_kern.h>

static inline void pfm_copy_thread(struct task_struct *task)
{
	/*
	 * context or perfmon TIF state  is NEVER inherited
	 * in child task. Holds for per-thread and system-wide
	 */
	task->pfm_context = NULL;
	clear_tsk_thread_flag(task, TIF_PERFMON_CTXSW);
	clear_tsk_thread_flag(task, TIF_PERFMON_WORK);
	pfm_arch_disarm_handle_work(task);
}


/*
 * read a single PMD register.
 *
 * virtual PMD registers have special handler.
 * Depends on definitions in asm/perfmon_kern.h
 */
static inline u64 pfm_read_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	if (unlikely(pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_V))
		return pfm_pmu_conf->pmd_sread(ctx, cnum);

	return pfm_arch_read_pmd(ctx, cnum);
}
/*
 * write a single PMD register.
 *
 * virtual PMD registers have special handler.
 * Depends on definitions in asm/perfmon_kern.h
 */
static inline void pfm_write_pmd(struct pfm_context *ctx, unsigned int cnum,
				 u64 value)
{
	/*
	 * PMD writes are ignored for read-only registers
	 */
	if (pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_RO)
		return;

	if (pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_V) {
		pfm_pmu_conf->pmd_swrite(ctx, cnum, value);
		return;
	}
	/*
	 * clear unimplemented bits
	 */
	value &= ~pfm_pmu_conf->pmd_desc[cnum].rsvd_msk;

	pfm_arch_write_pmd(ctx, cnum, value);
}

void __pfm_init_percpu(void *dummy);

static inline void pfm_init_percpu(void)
{
	__pfm_init_percpu(NULL);
}

/*
 * pfm statistics are available via debugfs
 * and perfmon subdir.
 *
 * When adding/removing new stats, make sure you also
 * update the name table in perfmon_debugfs.c
 */
enum pfm_stats_names {
	PFM_ST_ovfl_intr_all_count = 0,
	PFM_ST_ovfl_intr_ns,
	PFM_ST_ovfl_intr_spurious_count,
	PFM_ST_ovfl_intr_replay_count,
	PFM_ST_ovfl_intr_regular_count,
	PFM_ST_handle_work_count,
	PFM_ST_ovfl_notify_count,
	PFM_ST_reset_pmds_count,
	PFM_ST_pfm_restart_count,
	PFM_ST_fmt_handler_calls,
	PFM_ST_fmt_handler_ns,
	PFM_ST_set_switch_count,
	PFM_ST_set_switch_ns,
	PFM_ST_set_switch_exp,
	PFM_ST_ctxswin_count,
	PFM_ST_ctxswin_ns,
	PFM_ST_handle_timeout_count,
	PFM_ST_ovfl_intr_nmi_count,
	PFM_ST_ctxswout_count,
	PFM_ST_ctxswout_ns,
	PFM_ST_LAST	/* last entry marked */
};
#define PFM_NUM_STATS PFM_ST_LAST

struct pfm_stats {
	u64 v[PFM_NUM_STATS];
	struct dentry *dirs[PFM_NUM_STATS];
	struct dentry *cpu_dir;
	char cpu_name[8];
};

#ifdef CONFIG_PERFMON_DEBUG_FS
#define pfm_stats_get(x)  __get_cpu_var(pfm_stats).v[PFM_ST_##x]
#define pfm_stats_inc(x)  __get_cpu_var(pfm_stats).v[PFM_ST_##x]++
#define pfm_stats_add(x, y)  __get_cpu_var(pfm_stats).v[PFM_ST_##x] += (y)
void pfm_reset_stats(int cpu);
#else
#define pfm_stats_get(x)
#define pfm_stats_inc(x)
#define pfm_stats_add(x, y)
static inline void pfm_reset_stats(int cpu)
{}
#endif



DECLARE_PER_CPU(struct pfm_context *, pmu_ctx);
DECLARE_PER_CPU(struct pfm_stats, pfm_stats);
DECLARE_PER_CPU(struct task_struct *, pmu_owner);

void pfm_cpu_disable(void);


/*
 * max vector argument elements for local storage (no kmalloc/kfree)
 * The PFM_ARCH_PM*_ARG should be defined in perfmon_kern.h.
 * If not, default (conservative) values are used
 */
#ifndef PFM_ARCH_PMC_STK_ARG
#define PFM_ARCH_PMC_STK_ARG	1
#endif

#ifndef PFM_ARCH_PMD_STK_ARG
#define PFM_ARCH_PMD_STK_ARG	1
#endif

#define PFM_PMC_STK_ARG	PFM_ARCH_PMC_STK_ARG
#define PFM_PMD_STK_ARG	PFM_ARCH_PMD_STK_ARG

#else /* !CONFIG_PERFMON */


/*
 * perfmon hooks are nops when CONFIG_PERFMON is undefined
 */
static inline void pfm_cpu_disable(void)
{}

static inline void pfm_exit_thread(void)
{}

static inline void pfm_handle_work(struct pt_regs *regs)
{}

static inline void pfm_copy_thread(struct task_struct *t)
{}

static inline void pfm_ctxsw_in(struct task_struct *p, struct task_struct *n)
{}

static inline void pfm_ctxsw_out(struct task_struct *p, struct task_struct *n)
{}

static inline void pfm_session_allcpus_release(void)
{}

static inline int pfm_session_allcpus_acquire(void)
{
	return 0;
}

static inline void pfm_init_percpu(void)
{}

#endif /* CONFIG_PERFMON */

#endif /* __LINUX_PERFMON_KERN_H__ */
