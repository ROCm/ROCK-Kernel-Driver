/*
 * Copyright (c) 2005 Philip Mucci.
 *
 * Based on other versions:
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file contains mips64 specific definitions for the perfmon
 * interface.
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
#ifndef _ASM_MIPS64_PERFMON_KERN_H_
#define _ASM_MIPS64_PERFMON_KERN_H_

#ifdef __KERNEL__

#ifdef CONFIG_PERFMON
#include <linux/unistd.h>
#include <asm/cacheflush.h>

#define PFM_ARCH_PMD_STK_ARG	2
#define PFM_ARCH_PMC_STK_ARG	2

struct pfm_arch_pmu_info {
	u32 pmu_style;
};

#define MIPS64_CONFIG_PMC_MASK (1 << 4)
#define MIPS64_PMC_INT_ENABLE_MASK (1 << 4)
#define MIPS64_PMC_CNT_ENABLE_MASK (0xf)
#define MIPS64_PMC_EVT_MASK (0x7 << 6)
#define MIPS64_PMC_CTR_MASK (1 << 31)
#define MIPS64_PMD_INTERRUPT (1 << 31)

/* Coprocessor register 25 contains the PMU interface. */
/* Sel 0 is control for counter 0 */
/* Sel 1 is count for counter 0. */
/* Sel 2 is control for counter 1. */
/* Sel 3 is count for counter 1. */

/*

31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4  3 2 1 0
M  0--------------------------------------------------------------0 Event-- IE U S K EXL

M 31 If this bit is one, another pair of Performance Control
and Counter registers is implemented at a MTC0

Event 8:5 Counter event enabled for this counter. Possible events
are listed in Table 6-30. R/W Undefined

IE 4 Counter Interrupt Enable. This bit masks bit 31 of the
associated count register from the interrupt exception
request output. R/W 0

U 3 Count in User Mode. When this bit is set, the specified
event is counted in User Mode. R/W Undefined

S 2 Count in Supervisor Mode. When this bit is set, the
specified event is counted in Supervisor Mode. R/W Undefined

K 1 Count in Kernel Mode. When this bit is set, count the
event in Kernel Mode when EXL and ERL both are 0. R/W Undefined

EXL 0 Count when EXL. When this bit is set, count the event
when EXL = 1 and ERL = 0. R/W Undefined
*/

static inline void pfm_arch_resend_irq(struct pfm_context *ctx)
{}

static inline void pfm_arch_clear_pmd_ovfl_cond(struct pfm_context *ctx,
					        struct pfm_event_set *set)
{}

static inline void pfm_arch_serialize(void)
{}


/*
 * MIPS does not save the PMDs during pfm_arch_intr_freeze_pmu(), thus
 * this routine needs to do it when switching sets on overflow
 */
static inline void pfm_arch_save_pmds_from_intr(struct pfm_context *ctx,
						struct pfm_event_set *set)
{
	pfm_save_pmds(ctx, set);
}

static inline void pfm_arch_write_pmc(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	/*
	 * we only write to the actual register when monitoring is
	 * active (pfm_start was issued)
	 */
	if (ctx && (ctx->flags.started == 0))
		return;

	switch (pfm_pmu_conf->pmc_desc[cnum].hw_addr) {
	case 0:
		write_c0_perfctrl0(value);
		break;
	case 1:
		write_c0_perfctrl1(value);
		break;
	case 2:
		write_c0_perfctrl2(value);
		break;
	case 3:
		write_c0_perfctrl3(value);
		break;
	default:
		BUG();
	}
}

static inline void pfm_arch_write_pmd(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	value &= pfm_pmu_conf->ovfl_mask;

	switch (pfm_pmu_conf->pmd_desc[cnum].hw_addr) {
	case 0:
		write_c0_perfcntr0(value);
		break;
	case 1:
		write_c0_perfcntr1(value);
		break;
	case 2:
		write_c0_perfcntr2(value);
		break;
	case 3:
		write_c0_perfcntr3(value);
		break;
	default:
		BUG();
	}
}

static inline u64 pfm_arch_read_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	switch (pfm_pmu_conf->pmd_desc[cnum].hw_addr) {
	case 0:
		return read_c0_perfcntr0();
		break;
	case 1:
		return read_c0_perfcntr1();
		break;
	case 2:
		return read_c0_perfcntr2();
		break;
	case 3:
		return read_c0_perfcntr3();
		break;
	default:
		BUG();
		return 0;
	}
}

static inline u64 pfm_arch_read_pmc(struct pfm_context *ctx, unsigned int cnum)
{
	switch (pfm_pmu_conf->pmc_desc[cnum].hw_addr) {
	case 0:
		return read_c0_perfctrl0();
		break;
	case 1:
		return read_c0_perfctrl1();
		break;
	case 2:
		return read_c0_perfctrl2();
		break;
	case 3:
		return read_c0_perfctrl3();
		break;
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
  u64 val;
  val = pfm_arch_read_pmd(ctx, cnum);
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
{}

static inline void pfm_arch_ctxswin_sys(struct task_struct *task,
					struct pfm_context *ctx)
{}

static inline void pfm_arch_ctxswin_thread(struct task_struct *task,
					   struct pfm_context *ctx)
{}
int  pfm_arch_ctxswout_thread(struct task_struct *task,
			      struct pfm_context *ctx);

int  pfm_arch_is_monitoring_active(struct pfm_context *ctx);
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
 * On MIPS, the PMU is frozen via arch_stop, masking would be implemented
 * via arch-stop as well. Given that the PMU is already stopped when
 * entering the interrupt handler, we do not need to stop it again, so
 * this function is a nop.
 */
static inline void pfm_arch_mask_monitoring(struct pfm_context *ctx,
					    struct pfm_event_set *set)
{}

/*
 * on MIPS masking/unmasking uses the start/stop mechanism, so we simply
 * need to start here.
 */
static inline void pfm_arch_unmask_monitoring(struct pfm_context *ctx,
					      struct pfm_event_set *set)
{
	pfm_arch_start(current, ctx);
}

static inline int pfm_arch_context_create(struct pfm_context *ctx,
					  u32 ctx_flags)
{
	return 0;
}

static inline void pfm_arch_context_free(struct pfm_context *ctx)
{}





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
static inline int
pfm_arch_setfl_sane(struct pfm_context *ctx, u32 flags)
{
	return 0;
}

static inline int pfm_arch_init(void)
{
	return 0;
}

static inline void pfm_arch_init_percpu(void)
{}

static inline int pfm_arch_load_context(struct pfm_context *ctx)
{
	return 0;
}

static inline void pfm_arch_unload_context(struct pfm_context *ctx)
{}

static inline int pfm_arch_pmu_acquire(u64 *unavail_pmcs, u64 *unavail_pmds)
{
	return 0;
}

static inline void pfm_arch_pmu_release(void)
{}

#ifdef CONFIG_PERFMON_FLUSH
/*
 * due to cache aliasing problem on MIPS, it is necessary to flush
 * pages out of the cache when they are modified.
 */
static inline void pfm_cacheflush(void *addr, unsigned int len)
{
	unsigned long start, end;

	start = (unsigned long)addr & PAGE_MASK;
	end = ((unsigned long)addr + len + PAGE_SIZE - 1) & PAGE_MASK;

	while (start < end) {
		flush_data_cache_page(start);
		start += PAGE_SIZE;
	}
}
#else
static inline void pfm_cacheflush(void *addr, unsigned int len)
{}
#endif

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
	if (test_thread_flag(TIF_32BIT_ADDR)) {
		if (test_thread_flag(TIF_32BIT_REGS))
			return __NR_O32_Linux+330;
		return __NR_N32_Linux+293;
	}
	return __NR_64_Linux+289;
}

struct pfm_arch_context {
	/* empty */
};

#define PFM_ARCH_CTX_SIZE	sizeof(struct pfm_arch_context)
/*
 * MIPS may need extra alignment requirements for the sampling buffer
 */
#ifdef CONFIG_PERFMON_SMPL_ALIGN
#define PFM_ARCH_SMPL_ALIGN_SIZE	0x4000
#else
#define PFM_ARCH_SMPL_ALIGN_SIZE	0
#endif

#endif /* CONFIG_PERFMON */

#endif /* __KERNEL__ */
#endif /* _ASM_MIPS64_PERFMON_KERN_H_ */
