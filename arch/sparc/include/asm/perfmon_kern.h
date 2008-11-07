#ifndef _SPARC64_PERFMON_KERN_H_
#define _SPARC64_PERFMON_KERN_H_

#ifdef __KERNEL__

#ifdef CONFIG_PERFMON

#include <linux/irq.h>
#include <asm/system.h>

#define PFM_ARCH_PMD_STK_ARG	2
#define PFM_ARCH_PMC_STK_ARG	1

struct pfm_arch_pmu_info {
	u32 pmu_style;
};

static inline void pfm_arch_resend_irq(struct pfm_context *ctx)
{
}

static inline void pfm_arch_clear_pmd_ovfl_cond(struct pfm_context *ctx,
					        struct pfm_event_set *set)
{}

static inline void pfm_arch_serialize(void)
{
}

/*
 * SPARC does not save the PMDs during pfm_arch_intr_freeze_pmu(), thus
 * this routine needs to do it when switching sets on overflow
 */
static inline void pfm_arch_save_pmds_from_intr(struct pfm_context *ctx,
						struct pfm_event_set *set)
{
	pfm_save_pmds(ctx, set);
}

extern void pfm_arch_write_pmc(struct pfm_context *ctx,
			       unsigned int cnum, u64 value);
extern u64 pfm_arch_read_pmc(struct pfm_context *ctx, unsigned int cnum);

static inline void pfm_arch_write_pmd(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	u64 pic;

	value &= pfm_pmu_conf->ovfl_mask;

	read_pic(pic);

	switch (cnum) {
	case 0:
		pic = (pic & 0xffffffff00000000UL) |
			(value & 0xffffffffUL);
		break;
	case 1:
		pic = (pic & 0xffffffffUL) |
			(value << 32UL);
		break;
	default:
		BUG();
	}

	write_pic(pic);
}

static inline u64 pfm_arch_read_pmd(struct pfm_context *ctx,
				    unsigned int cnum)
{
	u64 pic;

	read_pic(pic);

	switch (cnum) {
	case 0:
		return pic & 0xffffffffUL;
	case 1:
		return pic >> 32UL;
	default:
		BUG();
		return 0;
	}
}

/*
 * For some CPUs, the upper bits of a counter must be set in order for the
 * overflow interrupt to happen. On overflow, the counter has wrapped around,
 * and the upper bits are cleared. This function may be used to set them back.
 */
static inline void pfm_arch_ovfl_reset_pmd(struct pfm_context *ctx,
					   unsigned int cnum)
{
	u64 val = pfm_arch_read_pmd(ctx, cnum);

	/* This masks out overflow bit 31 */
	pfm_arch_write_pmd(ctx, cnum, val);
}

/*
 * At certain points, perfmon needs to know if monitoring has been
 * explicitely started/stopped by user via pfm_start/pfm_stop. The
 * information is tracked in ctx.flags.started. However on certain
 * architectures, it may be possible to start/stop directly from
 * user level with a single assembly instruction bypassing
 * the kernel. This function must be used to determine by
 * an arch-specific mean if monitoring is actually started/stopped.
 */
static inline int pfm_arch_is_active(struct pfm_context *ctx)
{
	return ctx->flags.started;
}

static inline void pfm_arch_ctxswout_sys(struct task_struct *task,
					 struct pfm_context *ctx)
{
}

static inline void pfm_arch_ctxswin_sys(struct task_struct *task,
					struct pfm_context *ctx)
{
}

static inline void pfm_arch_ctxswin_thread(struct task_struct *task,
					   struct pfm_context *ctx)
{
}

int  pfm_arch_is_monitoring_active(struct pfm_context *ctx);
int  pfm_arch_ctxswout_thread(struct task_struct *task,
			      struct pfm_context *ctx);
void pfm_arch_stop(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set);
void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set);
char *pfm_arch_get_pmu_module_name(void);

static inline void pfm_arch_intr_freeze_pmu(struct pfm_context *ctx,
					    struct pfm_event_set *set)
{
	pfm_arch_stop(current, ctx);
	/*
	 * we mark monitoring as stopped to avoid
	 * certain side effects especially in
	 * pfm_switch_sets_from_intr() on
	 * pfm_arch_restore_pmcs()
	 */
	ctx->flags.started = 0;
}

/*
 * unfreeze PMU from pfm_do_interrupt_handler()
 * ctx may be NULL for spurious
 */
static inline void pfm_arch_intr_unfreeze_pmu(struct pfm_context *ctx)
{
	if (!ctx)
		return;

	PFM_DBG_ovfl("state=%d", ctx->state);

	ctx->flags.started = 1;

	if (ctx->state == PFM_CTX_MASKED)
		return;

	pfm_arch_restore_pmcs(ctx, ctx->active_set);
}

/*
 * this function is called from the PMU interrupt handler ONLY.
 * On SPARC, the PMU is frozen via arch_stop, masking would be implemented
 * via arch-stop as well. Given that the PMU is already stopped when
 * entering the interrupt handler, we do not need to stop it again, so
 * this function is a nop.
 */
static inline void pfm_arch_mask_monitoring(struct pfm_context *ctx,
					    struct pfm_event_set *set)
{
}

/*
 * on MIPS masking/unmasking uses the start/stop mechanism, so we simply
 * need to start here.
 */
static inline void pfm_arch_unmask_monitoring(struct pfm_context *ctx,
					      struct pfm_event_set *set)
{
	pfm_arch_start(current, ctx);
}

static inline void pfm_arch_pmu_config_remove(void)
{
}

static inline int pfm_arch_context_create(struct pfm_context *ctx,
					  u32 ctx_flags)
{
	return 0;
}

static inline void pfm_arch_context_free(struct pfm_context *ctx)
{
}

/*
 * function called from pfm_setfl_sane(). Context is locked
 * and interrupts are masked.
 * The value of flags is the value of ctx_flags as passed by
 * user.
 *
 * function must check arch-specific set flags.
 * Return:
 * 	1 when flags are valid
 *      0 on error
 */
static inline int pfm_arch_setfl_sane(struct pfm_context *ctx, u32 flags)
{
	return 0;
}

static inline int pfm_arch_init(void)
{
	return 0;
}

static inline void pfm_arch_init_percpu(void)
{
}

static inline int pfm_arch_load_context(struct pfm_context *ctx)
{
	return 0;
}

static inline void pfm_arch_unload_context(struct pfm_context *ctx)
{}

extern void perfmon_interrupt(struct pt_regs *);

static inline int pfm_arch_pmu_acquire(u64 *unavail_pmcs, u64 *unavail_pmds)
{
	return register_perfctr_intr(perfmon_interrupt);
}

static inline void pfm_arch_pmu_release(void)
{
	release_perfctr_intr(perfmon_interrupt);
}

static inline void pfm_arch_arm_handle_work(struct task_struct *task)
{}

static inline void pfm_arch_disarm_handle_work(struct task_struct *task)
{}

static inline int pfm_arch_pmu_config_init(struct pfm_pmu_config *cfg)
{
	return 0;
}

static inline int pfm_arch_get_base_syscall(void)
{
	return __NR_pfm_create_context;
}

struct pfm_arch_context {
	/* empty */
};

#define PFM_ARCH_CTX_SIZE	sizeof(struct pfm_arch_context)
/*
 * SPARC needs extra alignment for the sampling buffer
 */
#define PFM_ARCH_SMPL_ALIGN_SIZE	(16 * 1024)

static inline void pfm_cacheflush(void *addr, unsigned int len)
{
}

#endif /* CONFIG_PERFMON */

#endif /* __KERNEL__ */

#endif /* _SPARC64_PERFMON_KERN_H_ */
