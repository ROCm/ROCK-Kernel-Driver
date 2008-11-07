/*
 * This file implements the MIPS64 specific
 * support for the perfmon2 interface
 *
 * Copyright (c) 2005 Philip J. Mucci
 *
 * based on versions for other architectures:
 * Copyright (c) 2005 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@htrpl.hp.com>
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
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/perfmon_kern.h>

/*
 * collect pending overflowed PMDs. Called from pfm_ctxsw()
 * and from PMU interrupt handler. Must fill in set->povfl_pmds[]
 * and set->npend_ovfls. Interrupts are masked
 */
static void __pfm_get_ovfl_pmds(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 new_val, wmask;
	u64 *used_mask, *intr_pmds;
	u64 mask[PFM_PMD_BV];
	unsigned int i, max;

	max = ctx->regs.max_intr_pmd;
	intr_pmds = ctx->regs.intr_pmds;
	used_mask = set->used_pmds;

	wmask = 1ULL << pfm_pmu_conf->counter_width;

	bitmap_and(cast_ulp(mask),
		   cast_ulp(intr_pmds),
		   cast_ulp(used_mask),
		   max);

	/*
	 * check all PMD that can generate interrupts
	 * (that includes counters)
	 */
	for (i = 0; i < max; i++) {
		if (test_bit(i, mask)) {
			new_val = pfm_arch_read_pmd(ctx, i);

			PFM_DBG_ovfl("pmd%u new_val=0x%llx bit=%d\n",
				     i, (unsigned long long)new_val,
				     (new_val&wmask) ? 1 : 0);

			if (new_val & wmask) {
				__set_bit(i, set->povfl_pmds);
				set->npend_ovfls++;
			}
		}
	}
}

static void pfm_stop_active(struct task_struct *task, struct pfm_context *ctx,
			    struct pfm_event_set *set)
{
	unsigned int i, max;

	max = ctx->regs.max_pmc;

	/*
	 * clear enable bits, assume all pmcs are enable pmcs
	 */
	for (i = 0; i < max; i++) {
		if (test_bit(i, set->used_pmcs))
			pfm_arch_write_pmc(ctx, i, 0);
	}

	if (set->npend_ovfls)
		return;

	__pfm_get_ovfl_pmds(ctx, set);
}

/*
 * Called from pfm_ctxsw(). Task is guaranteed to be current.
 * Context is locked. Interrupts are masked. Monitoring is active.
 * PMU access is guaranteed. PMC and PMD registers are live in PMU.
 *
 * for per-thread:
 * 	must stop monitoring for the task
 *
 * Return:
 * 	non-zero : did not save PMDs (as part of stopping the PMU)
 * 	       0 : saved PMDs (no need to save them in caller)
 */
int pfm_arch_ctxswout_thread(struct task_struct *task, struct pfm_context *ctx)
{
	/*
	 * disable lazy restore of PMC registers.
	 */
	ctx->active_set->priv_flags |= PFM_SETFL_PRIV_MOD_PMCS;

	/*
	 * if masked, monitoring is stopped, thus there is no
	 * need to stop the PMU again and there is no need to
	 * check for pending overflows. This is not just an
	 * optimization, this is also for correctness as you
	 * may end up detecting overflows twice.
	 */
	if (ctx->state == PFM_CTX_MASKED)
		return 1;

	pfm_stop_active(task, ctx, ctx->active_set);

	return 1;
}

/*
 * Called from pfm_stop() and pfm_ctxsw()
 * Interrupts are masked. Context is locked. Set is the active set.
 *
 * For per-thread:
 *   task is not necessarily current. If not current task, then
 *   task is guaranteed stopped and off any cpu. Access to PMU
 *   is not guaranteed. Interrupts are masked. Context is locked.
 *   Set is the active set.
 *
 * For system-wide:
 * 	task is current
 *
 * must disable active monitoring. ctx cannot be NULL
 */
void pfm_arch_stop(struct task_struct *task, struct pfm_context *ctx)
{
	/*
	 * no need to go through stop_save()
	 * if we are already stopped
	 */
	if (!ctx->flags.started || ctx->state == PFM_CTX_MASKED)
		return;

	/*
	 * stop live registers and collect pending overflow
	 */
	if (task == current)
		pfm_stop_active(task, ctx, ctx->active_set);
}

/*
 * called from pfm_start() or pfm_ctxsw() when idle task and
 * EXCL_IDLE is on.
 *
 * Interrupts are masked. Context is locked. Set is the active set.
 *
 * For per-trhead:
 * 	Task is not necessarily current. If not current task, then task
 * 	is guaranteed stopped and off any cpu. Access to PMU is not guaranteed.
 *
 * For system-wide:
 * 	task is always current
 *
 * must enable active monitoring.
 */
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_event_set *set;
	unsigned int i, max_pmc;

	if (task != current)
		return;

	set = ctx->active_set;
	max_pmc = ctx->regs.max_pmc;

	for (i = 0; i < max_pmc; i++) {
		if (test_bit(i, set->used_pmcs))
			pfm_arch_write_pmc(ctx, i, set->pmcs[i]);
	}
}

/*
 * function called from pfm_switch_sets(), pfm_context_load_thread(),
 * pfm_context_load_sys(), pfm_ctxsw(), pfm_switch_sets()
 * context is locked. Interrupts are masked. set cannot be NULL.
 * Access to the PMU is guaranteed.
 *
 * function must restore all PMD registers from set.
 */
void pfm_arch_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 ovfl_mask, val;
	u64 *impl_pmds;
	unsigned int i;
	unsigned int max_pmd;

	max_pmd = ctx->regs.max_pmd;
	ovfl_mask = pfm_pmu_conf->ovfl_mask;
	impl_pmds = ctx->regs.pmds;

	/*
	 * must restore all pmds to avoid leaking
	 * information to user.
	 */
	for (i = 0; i < max_pmd; i++) {

		if (test_bit(i, impl_pmds) == 0)
			continue;

		val = set->pmds[i].value;

		/*
		 * set upper bits for counter to ensure
		 * overflow will trigger
		 */
		val &= ovfl_mask;

		pfm_arch_write_pmd(ctx, i, val);
	}
}

/*
 * function called from pfm_switch_sets(), pfm_context_load_thread(),
 * pfm_context_load_sys(), pfm_ctxsw().
 * Context is locked. Interrupts are masked. set cannot be NULL.
 * Access to the PMU is guaranteed.
 *
 * function must restore all PMC registers from set, if needed.
 */
void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set)
{
	u64 *impl_pmcs;
	unsigned int i, max_pmc;

	max_pmc = ctx->regs.max_pmc;
	impl_pmcs = ctx->regs.pmcs;

	/*
	 * - by default no PMCS measures anything
	 * - on ctxswout, all used PMCs are disabled (cccr enable bit cleared)
	 * hence when masked we do not need to restore anything
	 */
	if (ctx->state == PFM_CTX_MASKED || ctx->flags.started == 0)
		return;

	/*
	 * restore all pmcs
	 */
	for (i = 0; i < max_pmc; i++)
		if (test_bit(i, impl_pmcs))
			pfm_arch_write_pmc(ctx, i, set->pmcs[i]);
}

char *pfm_arch_get_pmu_module_name(void)
{
	switch (cpu_data->cputype) {
#ifndef CONFIG_SMP
	case CPU_34K:
#if defined(CPU_74K)
	case CPU_74K:
#endif
#endif
	case CPU_SB1:
	case CPU_SB1A:
	case CPU_R12000:
	case CPU_25KF:
	case CPU_24K:
	case CPU_20KC:
	case CPU_5KC:
		return "perfmon_mips64";
	default:
		return NULL;
	}
	return NULL;
}

int perfmon_perf_irq(void)
{
	/* BLATANTLY STOLEN FROM OPROFILE, then modified */
	struct pt_regs *regs;
	unsigned int counters = pfm_pmu_conf->regs_all.max_pmc;
	unsigned int control;
	unsigned int counter;

	regs = get_irq_regs();
	switch (counters) {
#define HANDLE_COUNTER(n)						\
	case n + 1:							\
		control = read_c0_perfctrl ## n();			\
		counter = read_c0_perfcntr ## n();			\
		if ((control & MIPS64_PMC_INT_ENABLE_MASK) &&		\
		    (counter & MIPS64_PMD_INTERRUPT)) {			\
			pfm_interrupt_handler(instruction_pointer(regs),\
					      regs);			\
			return(1);					\
		}
		HANDLE_COUNTER(3)
		HANDLE_COUNTER(2)
		HANDLE_COUNTER(1)
		HANDLE_COUNTER(0)
	}

	return 0;
}
EXPORT_SYMBOL(perfmon_perf_irq);
