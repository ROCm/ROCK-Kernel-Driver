/*
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * Copyright (c) 2007 Advanced Micro Devices, Inc.
 * Contributed by Robert Richter <robert.richter@amd.com>
 *
 * This file contains X86 Processor Family specific definitions
 * for the perfmon interface. This covers P6, Pentium M, P4/Xeon
 * (32-bit and 64-bit, i.e., EM64T) and AMD X86-64.
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
#ifndef _ASM_X86_PERFMON_KERN_H_
#define _ASM_X86_PERFMON_KERN_H_

#ifdef CONFIG_PERFMON
#include <linux/unistd.h>
#ifdef CONFIG_4KSTACKS
#define PFM_ARCH_PMD_STK_ARG	2
#define PFM_ARCH_PMC_STK_ARG	2
#else
#define PFM_ARCH_PMD_STK_ARG	4 /* about 700 bytes of stack space */
#define PFM_ARCH_PMC_STK_ARG	4 /* about 200 bytes of stack space */
#endif

struct pfm_arch_pmu_info {
	u32 flags;		/* PMU feature flags */
	/*
	 * mandatory model-specific callbacks
	 */
	int  (*stop_save)(struct pfm_context *ctx, struct pfm_event_set *set);
	int  (*has_ovfls)(struct pfm_context *ctx);
	void (*quiesce)(void);

	/*
	 * optional model-specific callbacks
	 */
	void (*acquire_pmu_percpu)(void);
	void (*release_pmu_percpu)(void);
	int (*create_context)(struct pfm_context *ctx, u32 ctx_flags);
	void (*free_context)(struct pfm_context *ctx);
	int (*load_context)(struct pfm_context *ctx);
	void (*unload_context)(struct pfm_context *ctx);
	void (*write_pmc)(struct pfm_context *ctx, unsigned int cnum, u64 value);
	void (*write_pmd)(struct pfm_context *ctx, unsigned int cnum, u64 value);
	u64  (*read_pmd)(struct pfm_context *ctx, unsigned int cnum);
	u64  (*read_pmc)(struct pfm_context *ctx, unsigned int cnum);
	void (*nmi_copy_state)(struct pfm_context *ctx);
	void (*restore_pmcs)(struct pfm_context *ctx,
			     struct pfm_event_set *set);
	void (*restore_pmds)(struct pfm_context *ctx,
			     struct pfm_event_set *set);
};

/*
 * PMU feature flags
 */
#define PFM_X86_FL_USE_NMI	0x01	/* user asking for NMI */
#define PFM_X86_FL_NO_SHARING	0x02	/* no sharing with other subsystems */
#define PFM_X86_FL_SHARING	0x04	/* PMU is being shared */

struct pfm_x86_ctx_flags {
	unsigned int insecure:1;  /* rdpmc per-thread self-monitoring */
	unsigned int use_pebs:1;  /* PEBS used */
	unsigned int use_ds:1;    /* DS used */
	unsigned int reserved:29; /* for future use */
};

struct pfm_arch_context {
	u64 saved_real_iip;		/* instr pointer of last NMI intr */
	struct pfm_x86_ctx_flags flags;	/* flags */
	void *ds_area;			/* address of DS area (to go away) */
	void *data;			/* model-specific data */
};

/*
 * functions implemented as inline on x86
 */

/**
 * pfm_arch_write_pmc - write a single PMC register
 * @ctx: context to work on
 * @cnum: PMC index
 * @value: PMC 64-bit value
 *
 * in certain situations, ctx may be NULL
 */
static inline void pfm_arch_write_pmc(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	struct pfm_arch_pmu_info *pmu_info;

	pmu_info = pfm_pmu_info();

	/*
	 * we only write to the actual register when monitoring is
	 * active (pfm_start was issued)
	 */
	if (ctx && ctx->flags.started == 0)
		return;

	/*
	 * model-specific override, if any
	 */
	if (pmu_info->write_pmc) {
		pmu_info->write_pmc(ctx, cnum, value);
		return;
	}

	PFM_DBG_ovfl("pfm_arch_write_pmc(0x%lx, 0x%Lx)",
		     pfm_pmu_conf->pmc_desc[cnum].hw_addr,
		     (unsigned long long) value);

	wrmsrl(pfm_pmu_conf->pmc_desc[cnum].hw_addr, value);
}

/**
 * pfm_arch_write_pmd - write a single PMD register
 * @ctx: context to work on
 * @cnum: PMD index
 * @value: PMD 64-bit value
 */
static inline void pfm_arch_write_pmd(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	struct pfm_arch_pmu_info *pmu_info;

	pmu_info = pfm_pmu_info();

	/*
	 * to make sure the counter overflows, we set the
	 * upper bits. we also clear any other unimplemented
	 * bits as this may cause crash on some processors.
	 */
	if (pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_C64)
		value = (value | ~pfm_pmu_conf->ovfl_mask)
		      & ~pfm_pmu_conf->pmd_desc[cnum].rsvd_msk;

	PFM_DBG_ovfl("pfm_arch_write_pmd(0x%lx, 0x%Lx)",
		     pfm_pmu_conf->pmd_desc[cnum].hw_addr,
		     (unsigned long long) value);

	/*
	 * model-specific override, if any
	 */
	if (pmu_info->write_pmd) {
		pmu_info->write_pmd(ctx, cnum, value);
		return;
	}

	wrmsrl(pfm_pmu_conf->pmd_desc[cnum].hw_addr, value);
}

/**
 * pfm_arch_read_pmd - read a single PMD register
 * @ctx: context to work on
 * @cnum: PMD index
 *
 * return value is register 64-bit value
 */
static inline u64 pfm_arch_read_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	struct pfm_arch_pmu_info *pmu_info;
	u64 tmp;

	pmu_info = pfm_pmu_info();

	/*
	 * model-specific override, if any
	 */
	if (pmu_info->read_pmd)
		tmp = pmu_info->read_pmd(ctx, cnum);
	else
		rdmsrl(pfm_pmu_conf->pmd_desc[cnum].hw_addr, tmp);

	PFM_DBG_ovfl("pfm_arch_read_pmd(0x%lx) = 0x%Lx",
		     pfm_pmu_conf->pmd_desc[cnum].hw_addr,
		     (unsigned long long) tmp);
	return tmp;
}

/**
 * pfm_arch_read_pmc - read a single PMC register
 * @ctx: context to work on
 * @cnum: PMC index
 *
 * return value is register 64-bit value
 */
static inline u64 pfm_arch_read_pmc(struct pfm_context *ctx, unsigned int cnum)
{
	struct pfm_arch_pmu_info *pmu_info;
	u64 tmp;

	pmu_info = pfm_pmu_info();

	/*
	 * model-specific override, if any
	 */
	if (pmu_info->read_pmc)
		tmp = pmu_info->read_pmc(ctx, cnum);
	else
		rdmsrl(pfm_pmu_conf->pmc_desc[cnum].hw_addr, tmp);

	PFM_DBG_ovfl("pfm_arch_read_pmc(0x%lx) = 0x%016Lx",
		     pfm_pmu_conf->pmc_desc[cnum].hw_addr,
		     (unsigned long long) tmp);
	return tmp;
}

/**
 * pfm_arch_is_active - return non-zero is monitoring has been started
 * @ctx: context to check
 *
 * At certain points, perfmon needs to know if monitoring has been
 * explicitly started.
 *
 * On x86, there is not other way but to use pfm_start/pfm_stop
 * to activate monitoring, thus we can simply check flags.started
 */
static inline int pfm_arch_is_active(struct pfm_context *ctx)
{
	return ctx->flags.started;
}


/**
 * pfm_arch_unload_context - detach context from thread or CPU
 * @ctx: context to detach
 *
 * in system-wide ctx->task is NULL, otherwise it points to the
 * attached thread
 */
static inline void pfm_arch_unload_context(struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_arch_context *ctx_arch;

	ctx_arch = pfm_ctx_arch(ctx);
	pmu_info = pfm_pmu_info();

	if (ctx_arch->flags.insecure) {
		PFM_DBG("clear cr4.pce");
		clear_in_cr4(X86_CR4_PCE);
	}

	if (pmu_info->unload_context)
		pmu_info->unload_context(ctx);
}

/**
 * pfm_arch_load_context - attach context to thread or CPU
 * @ctx: context to attach
 */
static inline int pfm_arch_load_context(struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_arch_context *ctx_arch;
	int ret = 0;

	ctx_arch = pfm_ctx_arch(ctx);
	pmu_info = pfm_pmu_info();

	/*
	 * RDPMC authorized in system-wide and
	 * per-thread self-monitoring.
	 *
	 * RDPMC only gives access to counts.
	 *
	 * The context-switch routine code does not restore
	 * all the PMD registers (optimization), thus there
	 * is a possible leak of counts there in per-thread
	 * mode.
	 */
	if (ctx->task == current || ctx->flags.system) {
		PFM_DBG("set cr4.pce");
		set_in_cr4(X86_CR4_PCE);
		ctx_arch->flags.insecure = 1;
	}

	if (pmu_info->load_context)
		ret = pmu_info->load_context(ctx);

	return ret;
}

void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set);
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_stop(struct task_struct *task, struct pfm_context *ctx);

/**
 * pfm_arch_unmask_monitoring - unmask monitoring
 * @ctx: context to mask
 * @set: current event set
 *
 * masking is slightly different from stopping in that, it does not undo
 * the pfm_start() issued by user. This is used in conjunction with
 * sampling. Masking means stop monitoring, but do not authorize user
 * to issue pfm_start/stop during that time. Unmasking is achieved via
 * pfm_restart() and also may also depend on the sampling format used.
 *
 * on x86 masking/unmasking use the start/stop mechanism, except
 * that flags.started is not modified.
 */
static inline void pfm_arch_unmask_monitoring(struct pfm_context *ctx,
					      struct pfm_event_set *set)
{
	pfm_arch_start(current, ctx);
}

/**
 * pfm_arch_intr_freeze_pmu - stop monitoring when handling PMU interrupt
 * @ctx: current context
 * @set: current event set
 *
 * called from __pfm_interrupt_handler().
 * ctx is not NULL. ctx is locked. interrupts are masked
 *
 * The following actions must take place:
 *  - stop all monitoring to ensure handler has consistent view.
 *  - collect overflowed PMDs bitmask into povfls_pmds and
 *    npend_ovfls. If no interrupt detected then npend_ovfls
 *    must be set to zero.
 */
static inline void pfm_arch_intr_freeze_pmu(struct pfm_context *ctx,
					    struct pfm_event_set *set)
{
	/*
	 * on X86, freezing is equivalent to stopping
	 */
	pfm_arch_stop(current, ctx);

	/*
	 * we mark monitoring as stopped to avoid
	 * certain side effects especially in
	 * pfm_switch_sets_from_intr() and
	 * pfm_arch_restore_pmcs()
	 */
	ctx->flags.started = 0;
}

/**
 * pfm_arch_intr_unfreeze_pmu - conditionally reactive monitoring
 * @ctx: current context
 *
 * current context may be not when dealing when spurious interrupts
 *
 * Must re-activate monitoring if context is not MASKED.
 * interrupts are masked.
 */
static inline void pfm_arch_intr_unfreeze_pmu(struct pfm_context *ctx)
{
	if (ctx == NULL)
		return;

	PFM_DBG_ovfl("state=%d", ctx->state);

	/*
	 * restore flags.started which is cleared in
	 * pfm_arch_intr_freeze_pmu()
	 */
	ctx->flags.started = 1;

	if (ctx->state == PFM_CTX_MASKED)
		return;

	pfm_arch_restore_pmcs(ctx, ctx->active_set);
}

/**
 * pfm_arch_setfl_sane - check arch/model specific event set flags
 * @ctx: context to work on
 * @flags: event set flags as passed by user
 *
 * called from pfm_setfl_sane(). Context is locked. Interrupts are masked.
 *
 * Return:
 *      0 when flags are valid
 *      1 on error
 */
static inline int pfm_arch_setfl_sane(struct pfm_context *ctx, u32 flags)
{
	return 0;
}

/**
 * pfm_arch_ovfl_reset_pmd - reset pmd on overflow
 * @ctx: current context
 * @cnum: PMD index
 *
 * On some CPUs, the upper bits of a counter must be set in order for the
 * overflow interrupt to happen. On overflow, the counter has wrapped around,
 * and the upper bits are cleared. This function may be used to set them back.
 *
 * For x86, the current version loses whatever is remaining in the counter,
 * which is usually has a small count. In order not to loose this count,
 * we do a read-modify-write to set the upper bits while preserving the
 * low-order bits. This is slow but works.
 */
static inline void pfm_arch_ovfl_reset_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	u64 val;
	val = pfm_arch_read_pmd(ctx, cnum);
	pfm_arch_write_pmd(ctx, cnum, val);
}

/**
 * pfm_arch_context_create - create context
 * @ctx: newly created context
 * @flags: context flags as passed by user
 *
 * called from __pfm_create_context()
 */
static inline int pfm_arch_context_create(struct pfm_context *ctx, u32 ctx_flags)
{
	struct pfm_arch_pmu_info *pmu_info;

	pmu_info = pfm_pmu_info();

	if (pmu_info->create_context)
		return pmu_info->create_context(ctx, ctx_flags);

	return 0;
}

/**
 * pfm_arch_context_free - free context
 * @ctx: context to free
 */
static inline void pfm_arch_context_free(struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;

	pmu_info = pfm_pmu_info();

	if (pmu_info->free_context)
		pmu_info->free_context(ctx);
}

/*
 * pfm_arch_clear_pmd_ovfl_cond - alter the pmds in such a way that they
 * will not cause cause interrupts when unused.
 *
 * This is a nop on x86
 */
static inline void pfm_arch_clear_pmd_ovfl_cond(struct pfm_context *ctx,
					        struct pfm_event_set *set)
{}

/*
 * functions implemented in arch/x86/perfmon/perfmon.c
 */
int  pfm_arch_init(void);
void pfm_arch_resend_irq(struct pfm_context *ctx);

int  pfm_arch_ctxswout_thread(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_ctxswin_thread(struct task_struct *task, struct pfm_context *ctx);

void pfm_arch_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set);
int  pfm_arch_pmu_config_init(struct pfm_pmu_config *cfg);
void pfm_arch_pmu_config_remove(void);
char *pfm_arch_get_pmu_module_name(void);
int pfm_arch_pmu_acquire(u64 *unavail_pmcs, u64 *unavail_pmds);
void pfm_arch_pmu_release(void);

/*
 * pfm_arch_serialize - make PMU modifications visible to subsequent instructions
 *
 * This is a nop on x86
 */
static inline void pfm_arch_serialize(void)
{}

/*
 * on x86, the PMDs are already saved by pfm_arch_freeze_pmu()
 * when entering the PMU interrupt handler, thus, we do not need
 * to save them again in pfm_switch_sets_from_intr()
 */
static inline void pfm_arch_save_pmds_from_intr(struct pfm_context *ctx,
						struct pfm_event_set *set)
{}


static inline void pfm_arch_ctxswout_sys(struct task_struct *task,
					 struct pfm_context *ctx)
{}

static inline void pfm_arch_ctxswin_sys(struct task_struct *task,
					struct pfm_context *ctx)
{}

static inline void pfm_arch_init_percpu(void)
{}

static inline void pfm_cacheflush(void *addr, unsigned int len)
{}

/*
 * this function is called from the PMU interrupt handler ONLY.
 * On x86, the PMU is frozen via arch_stop, masking would be implemented
 * via arch-stop as well. Given that the PMU is already stopped when
 * entering the interrupt handler, we do not need to stop it again, so
 * this function is a nop.
 */
static inline void pfm_arch_mask_monitoring(struct pfm_context *ctx,
					    struct pfm_event_set *set)
{}


static inline void pfm_arch_arm_handle_work(struct task_struct *task)
{}

static inline void pfm_arch_disarm_handle_work(struct task_struct *task)
{}

static inline int pfm_arch_get_base_syscall(void)
{
#ifdef __x86_64__
	/* 32-bit syscall definition coming from ia32_unistd.h */
	if (test_thread_flag(TIF_IA32))
		return __NR_ia32_pfm_create_context;
#endif
	return __NR_pfm_create_context;
}

#define PFM_ARCH_CTX_SIZE	(sizeof(struct pfm_arch_context))
/*
 * x86 does not need extra alignment requirements for the sampling buffer
 */
#define PFM_ARCH_SMPL_ALIGN_SIZE	0

asmlinkage void  pmu_interrupt(void);

#endif /* CONFIG_PEFMON */

#endif /* _ASM_X86_PERFMON_KERN_H_ */
