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

#ifndef __PERFMON_PRIV_H__
#define __PERFMON_PRIV_H__
/*
 * This file contains all the definitions of data structures, variables, macros
 * that are to private to the generic code, i.e., not shared with any code that
 * lives under arch/ or include/asm-XX
 *
 * For shared definitions, use include/linux/perfmon_kern.h
 */

#ifdef CONFIG_PERFMON

/*
 * type of PMD reset for pfm_reset_pmds() or pfm_switch_sets*()
 */
#define PFM_PMD_RESET_SHORT	1	/* use short reset value */
#define PFM_PMD_RESET_LONG	2	/* use long reset value  */

/*
 * context lazy save/restore activation count
 */
#define PFM_INVALID_ACTIVATION	((u64)~0)

DECLARE_PER_CPU(u64, pmu_activation_number);
DECLARE_PER_CPU(struct hrtimer, pfm_hrtimer);

static inline void pfm_set_pmu_owner(struct task_struct *task,
				     struct pfm_context *ctx)
{
	__get_cpu_var(pmu_owner) = task;
	__get_cpu_var(pmu_ctx) = ctx;
}

static inline int pfm_msgq_is_empty(struct pfm_context *ctx)
{
	return ctx->msgq_head == ctx->msgq_tail;
}

void pfm_get_next_msg(struct pfm_context *ctx, union pfarg_msg *m);
int pfm_end_notify(struct pfm_context *ctx);
int pfm_ovfl_notify(struct pfm_context *ctx, struct pfm_event_set *set,
		    unsigned long ip);

int pfm_alloc_fd(struct pfm_context *ctx);

int __pfm_delete_evtsets(struct pfm_context *ctx, void *arg, int count);
int __pfm_getinfo_evtsets(struct pfm_context *ctx, struct pfarg_setinfo *req,
			  int count);
int __pfm_create_evtsets(struct pfm_context *ctx, struct pfarg_setdesc *req,
			int count);


int pfm_init_ctx(void);

int pfm_pmu_acquire(struct pfm_context *ctx);
void pfm_pmu_release(void);

int pfm_session_acquire(int is_system, u32 cpu);
void pfm_session_release(int is_system, u32 cpu);

int pfm_smpl_buf_space_acquire(struct pfm_context *ctx, size_t size);
int pfm_smpl_buf_load_context(struct pfm_context *ctx);
void pfm_smpl_buf_unload_context(struct pfm_context *ctx);

int  pfm_init_sysfs(void);

#ifdef CONFIG_PERFMON_DEBUG_FS
int  pfm_init_debugfs(void);
int pfm_debugfs_add_cpu(int mycpu);
void pfm_debugfs_del_cpu(int mycpu);
#else
static inline int pfm_init_debugfs(void)
{
	return 0;
}
static inline int pfm_debugfs_add_cpu(int mycpu)
{
	return 0;
}

static inline void pfm_debugfs_del_cpu(int mycpu)
{}
#endif


void pfm_reset_pmds(struct pfm_context *ctx, struct pfm_event_set *set,
		    int num_pmds,
		    int reset_mode);

struct pfm_event_set *pfm_prepare_sets(struct pfm_context *ctx, u16 load_set);
int pfm_init_sets(void);

ssize_t pfm_sysfs_res_show(char *buf, size_t sz, int what);

void pfm_free_sets(struct pfm_context *ctx);
int pfm_create_initial_set(struct pfm_context *ctx);
void pfm_switch_sets_from_intr(struct pfm_context *ctx);
void pfm_restart_timer(struct pfm_context *ctx, struct pfm_event_set *set);
enum hrtimer_restart pfm_handle_switch_timeout(struct hrtimer *t);

enum hrtimer_restart pfm_switch_sets(struct pfm_context *ctx,
		    struct pfm_event_set *new_set,
		    int reset_mode,
		    int no_restart);

/**
 * pfm_save_prev_ctx - check if previous context exists and save state
 *
 * called from pfm_load_ctx_thread() and __pfm_ctxsin_thread() to
 * check if previous context exists. If so saved its PMU state. This is used
 * only for UP kernels.
 *
 * PMU ownership is not cleared because the function is always called while
 * trying to install a new owner.
 */
static inline void pfm_check_save_prev_ctx(void)
{
#ifndef CONFIG_SMP
	struct pfm_event_set *set;
	struct pfm_context *ctxp;

	ctxp = __get_cpu_var(pmu_ctx);
	if (!ctxp)
		return;
	/*
	 * in UP per-thread, due to lazy save
	 * there could be a context from another
	 * task. We need to push it first before
	 * installing our new state
	 */
	set = ctxp->active_set;
	pfm_save_pmds(ctxp, set);
	/*
	 * do not clear ownership because we rewrite
	 * right away
	 */
#endif
}

int pfm_init_hotplug(void);
int pfm_init_control(void);

void pfm_mask_monitoring(struct pfm_context *ctx, struct pfm_event_set *set);
void pfm_resume_after_ovfl(struct pfm_context *ctx);
int pfm_setup_smpl_fmt(struct pfm_context *ctx, u32 ctx_flags, void *fmt_arg, int fd);

static inline void pfm_post_work(struct task_struct *task,
				 struct pfm_context *ctx, int type)
{
	ctx->flags.work_type = type;
	set_tsk_thread_flag(task, TIF_PERFMON_WORK);
	pfm_arch_arm_handle_work(task);
}

#define PFM_PMC_STK_ARG	PFM_ARCH_PMC_STK_ARG
#define PFM_PMD_STK_ARG	PFM_ARCH_PMD_STK_ARG

/* these used to be in linux/syscalls.h, now accessed via ioctl interface */
asmlinkage long sys_pfm_create_context(struct pfarg_ctx __user *ureq,
				       char __user *fmt_name,
				       void __user *fmt_uarg, size_t fmt_size);
asmlinkage long sys_pfm_write_pmcs(int fd, struct pfarg_pmc __user *ureq,
				   int count);
asmlinkage long sys_pfm_write_pmds(int fd, struct pfarg_pmd __user *ureq,
				   int count);
asmlinkage long sys_pfm_read_pmds(int fd, struct pfarg_pmd __user *ureq,
				  int count);
asmlinkage long sys_pfm_restart(int fd);
asmlinkage long sys_pfm_stop(int fd);
asmlinkage long sys_pfm_start(int fd, struct pfarg_start __user *ureq);
asmlinkage long sys_pfm_load_context(int fd, struct pfarg_load __user *ureq);
asmlinkage long sys_pfm_unload_context(int fd);
asmlinkage long sys_pfm_delete_evtsets(int fd,
				       struct pfarg_setinfo __user *ureq,
				       int count);
asmlinkage long sys_pfm_create_evtsets(int fd,
				       struct pfarg_setdesc __user *ureq,
				       int count);
asmlinkage long sys_pfm_getinfo_evtsets(int fd,
					struct pfarg_setinfo __user *ureq,
					int count);
#endif /* CONFIG_PERFMON */

#endif /* __PERFMON_PRIV_H__ */
