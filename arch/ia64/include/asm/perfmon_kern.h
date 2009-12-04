/*
 * Copyright (c) 2001-2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file contains Itanium Processor Family specific definitions
 * for the perfmon interface.
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
#ifndef _ASM_IA64_PERFMON_KERN_H_
#define _ASM_IA64_PERFMON_KERN_H_

#ifdef __KERNEL__

#ifdef CONFIG_PERFMON
#include <asm/unistd.h>
#include <asm/hw_irq.h>

/*
 * describe the content of the pfm_syst_info field
 * layout:
 * bits[00-15] : generic flags
 * bits[16-31] : arch-specific flags
 */
#define PFM_ITA_CPUINFO_IDLE_EXCL 0x10000 /* stop monitoring in idle loop */

/*
 * For some CPUs, the upper bits of a counter must be set in order for the
 * overflow interrupt to happen. On overflow, the counter has wrapped around,
 * and the upper bits are cleared. This function may be used to set them back.
 */
static inline void pfm_arch_ovfl_reset_pmd(struct pfm_context *ctx,
					   unsigned int cnum)
{}

/*
 * called from __pfm_interrupt_handler(). ctx is not NULL.
 * ctx is locked. PMU interrupt is masked.
 *
 * must stop all monitoring to ensure handler has consistent view.
 * must collect overflowed PMDs bitmask  into povfls_pmds and
 * npend_ovfls. If no interrupt detected then npend_ovfls
 * must be set to zero.
 */
static inline void pfm_arch_intr_freeze_pmu(struct pfm_context *ctx,
					    struct pfm_event_set *set)
{
	u64 tmp;

	/*
	 * do not overwrite existing value, must
	 * process those first (coming from context switch replay)
	 */
	if (set->npend_ovfls)
		return;

	ia64_srlz_d();

	tmp =  ia64_get_pmc(0) & ~0xf;

	set->povfl_pmds[0] = tmp;

	set->npend_ovfls = ia64_popcnt(tmp);
}

static inline int pfm_arch_init_pmu_config(void)
{
	return 0;
}

static inline void pfm_arch_resend_irq(struct pfm_context *ctx)
{
	ia64_resend_irq(IA64_PERFMON_VECTOR);
}

static inline void pfm_arch_clear_pmd_ovfl_cond(struct pfm_context *ctx,
					        struct pfm_event_set *set)
{}

static inline void pfm_arch_serialize(void)
{
	ia64_srlz_d();
}

static inline void pfm_arch_intr_unfreeze_pmu(struct pfm_context *ctx)
{
	PFM_DBG_ovfl("state=%d", ctx->state);
	ia64_set_pmc(0, 0);
	/* no serialization */
}

static inline void pfm_arch_write_pmc(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	if (cnum < 256) {
		ia64_set_pmc(pfm_pmu_conf->pmc_desc[cnum].hw_addr, value);
	} else if (cnum < 264) {
		ia64_set_ibr(cnum-256, value);
		ia64_dv_serialize_instruction();
	} else {
		ia64_set_dbr(cnum-264, value);
		ia64_dv_serialize_instruction();
	}
}

/*
 * On IA-64, for per-thread context which have the ITA_FL_INSECURE
 * flag, it is possible to start/stop monitoring directly from user evel
 * without calling pfm_start()/pfm_stop. This allows very lightweight
 * control yet the kernel sometimes needs to know if monitoring is actually
 * on or off.
 *
 * Tracking of this information is normally done by pfm_start/pfm_stop
 * in flags.started. Here we need to compensate by checking actual
 * psr bit.
 */
static inline int pfm_arch_is_active(struct pfm_context *ctx)
{
	return ctx->flags.started
	       || ia64_getreg(_IA64_REG_PSR) & (IA64_PSR_UP|IA64_PSR_PP);
}

static inline void pfm_arch_write_pmd(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	/*
	 * for a counting PMD, overflow bit must be cleared
	 */
	if (pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_C64)
		value &= pfm_pmu_conf->ovfl_mask;

	/*
	 * for counters, write to upper bits are ignored, no need to mask
	 */
	ia64_set_pmd(pfm_pmu_conf->pmd_desc[cnum].hw_addr, value);
}

static inline u64 pfm_arch_read_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	return ia64_get_pmd(pfm_pmu_conf->pmd_desc[cnum].hw_addr);
}

static inline u64 pfm_arch_read_pmc(struct pfm_context *ctx, unsigned int cnum)
{
	return ia64_get_pmc(pfm_pmu_conf->pmc_desc[cnum].hw_addr);
}

static inline void pfm_arch_ctxswout_sys(struct task_struct *task,
					 struct pfm_context *ctx)
{
	struct pt_regs *regs;

	regs = task_pt_regs(task);
	ia64_psr(regs)->pp = 0;
}

static inline void pfm_arch_ctxswin_sys(struct task_struct *task,
					struct pfm_context *ctx)
{
	struct pt_regs *regs;

	if (!(ctx->active_set->flags & PFM_ITA_SETFL_INTR_ONLY)) {
		regs = task_pt_regs(task);
		ia64_psr(regs)->pp = 1;
	}
}

/*
 * On IA-64, the PMDs are NOT saved by pfm_arch_freeze_pmu()
 * when entering the PMU interrupt handler, thus, we need
 * to save them in pfm_switch_sets_from_intr()
 */
static inline void pfm_arch_save_pmds_from_intr(struct pfm_context *ctx,
					   struct pfm_event_set *set)
{
	pfm_save_pmds(ctx, set);
}

int pfm_arch_context_create(struct pfm_context *ctx, u32 ctx_flags);

static inline void pfm_arch_context_free(struct pfm_context *ctx)
{}

int pfm_arch_ctxswout_thread(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_ctxswin_thread(struct task_struct *task,
			     struct pfm_context *ctx);

void pfm_arch_unload_context(struct pfm_context *ctx);
int pfm_arch_load_context(struct pfm_context *ctx);
int pfm_arch_setfl_sane(struct pfm_context *ctx, u32 flags);

void pfm_arch_mask_monitoring(struct pfm_context *ctx,
			      struct pfm_event_set *set);
void pfm_arch_unmask_monitoring(struct pfm_context *ctx,
				struct pfm_event_set *set);

void pfm_arch_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set);
void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set);

void pfm_arch_stop(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx);

int  pfm_arch_init(void);
void pfm_arch_init_percpu(void);
char *pfm_arch_get_pmu_module_name(void);

int __pfm_use_dbregs(struct task_struct *task);
int  __pfm_release_dbregs(struct task_struct *task);
int pfm_ia64_mark_dbregs_used(struct pfm_context *ctx,
			      struct pfm_event_set *set);

void pfm_arch_show_session(struct seq_file *m);

static inline int pfm_arch_reserve_regs(u64 *unavail_pmcs, u64 *unavail_pmds)
{
	return 0;
}

static inline void pfm_arch_release_regs(void)
{}

static inline int pfm_arch_acquire_pmu(void)
{
	return 0;
}

static inline void pfm_arch_release_pmu(void)
{}

/* not necessary on IA-64 */
static inline void pfm_cacheflush(void *addr, unsigned int len)
{}

/*
 * miscellaneous architected definitions
 */
#define PFM_ITA_FCNTR	4 /* first counting monitor (PMC/PMD) */

/*
 * private event set flags  (set_priv_flags)
 */
#define PFM_ITA_SETFL_USE_DBR	0x1000000 /* set uses debug registers */


/*
 * Itanium-specific data structures
 */
struct pfm_ia64_context_flags {
	unsigned int use_dbr:1;	 /* use range restrictions (debug registers) */
	unsigned int insecure:1; /* insecure monitoring for non-self session */
	unsigned int reserved:30;/* for future use */
};

struct pfm_arch_context {
	struct pfm_ia64_context_flags flags;	/* arch specific ctx flags */
	u64			 ctx_saved_psr_up;/* storage for psr_up */
#ifdef CONFIG_IA64_PERFMON_COMPAT
	void			*ctx_smpl_vaddr; /* vaddr of user mapping */
#endif
};

#ifdef CONFIG_IA64_PERFMON_COMPAT
ssize_t pfm_arch_compat_read(struct pfm_context *ctx,
			     char __user *buf,
			     int non_block,
			     size_t size);
int pfm_ia64_compat_init(void);
int pfm_smpl_buf_alloc_compat(struct pfm_context *ctx, size_t rsize, int fd);
#else
static inline ssize_t pfm_arch_compat_read(struct pfm_context *ctx,
			     char __user *buf,
			     int non_block,
			     size_t size)
{
	return -EINVAL;
}

static inline int pfm_smpl_buf_alloc_compat(struct pfm_context *ctx,
					    size_t rsize, struct file *filp)
{
	return -EINVAL;
}
#endif

static inline void pfm_arch_arm_handle_work(struct task_struct *task)
{
	set_tsk_thread_flag(task, TIF_NOTIFY_RESUME);
}

static inline void pfm_arch_disarm_handle_work(struct task_struct *task)
{
	/*
	 * since 2.6.28, we do not need this function anymore because
	 * TIF_NOTIFY_RESUME, it automatically cleared by do_notify_resume_user()
	 * so worst case we have a spurious call to this function
	 */
}

static inline int pfm_arch_pmu_config_init(struct pfm_pmu_config *cfg)
{
	return 0;
}

extern struct pfm_ia64_pmu_info *pfm_ia64_pmu_info;

#define PFM_ARCH_CTX_SIZE	(sizeof(struct pfm_arch_context))

/*
 * IA-64 does not need extra alignment requirements for the sampling buffer
 */
#define PFM_ARCH_SMPL_ALIGN_SIZE	0


static inline void pfm_release_dbregs(struct task_struct *task)
{
	if (task->thread.flags & IA64_THREAD_DBG_VALID)
		__pfm_release_dbregs(task);
}

#define pfm_use_dbregs(_t)     __pfm_use_dbregs(_t)

struct pfm_arch_pmu_info {
	unsigned long mask_pmcs[PFM_PMC_BV]; /* modify on when masking */
};

DECLARE_PER_CPU(u32, pfm_syst_info);
#else /* !CONFIG_PERFMON */
/*
 * perfmon ia64-specific hooks
 */
#define pfm_release_dbregs(_t) 		do { } while (0)
#define pfm_use_dbregs(_t)     		(0)

#endif /* CONFIG_PERFMON */

#endif /* __KERNEL__ */
#endif /* _ASM_IA64_PERFMON_KERN_H_ */
