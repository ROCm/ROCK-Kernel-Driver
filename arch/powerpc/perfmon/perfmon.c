/*
 * This file implements the powerpc specific
 * support for the perfmon2 interface
 *
 * Copyright (c) 2005 David Gibson, IBM Corporation.
 *
 * based on versions for other architectures:
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
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
#include <linux/interrupt.h>
#include <linux/perfmon_kern.h>

static void pfm_stop_active(struct task_struct *task,
			    struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *arch_info;

	arch_info = pfm_pmu_info();
	BUG_ON(!arch_info->disable_counters || !arch_info->get_ovfl_pmds);

	arch_info->disable_counters(ctx, set);

	if (set->npend_ovfls)
		return;

	arch_info->get_ovfl_pmds(ctx, set);
}

/*
 * Called from pfm_save_pmds(). Interrupts are masked.  Registers are
 * already saved away.
 */
void pfm_arch_clear_pmd_ovfl_cond(struct pfm_context *ctx,
				  struct pfm_event_set *set)
{
	int i, num;
	u64 *used_pmds, *intr_pmds;

	num = set->nused_pmds;
	used_pmds = set->used_pmds;
	intr_pmds = ctx->regs.intr_pmds;

	for (i = 0; num; i++)
		if (likely(test_bit(i, used_pmds))) {
			if (likely(test_bit(i, intr_pmds)))
				pfm_write_pmd(ctx, i, 0);
			num--;
		}
}

/*
 * Called from pfm_ctxsw(). Task is guaranteed to be current.
 * Context is locked. Interrupts are masked. Monitoring is active.
 * PMU access is guaranteed. PMC and PMD registers are live in PMU.
 *
 * for per-thread:
 * 	must stop monitoring for the task
 * Return:
 * 	non-zero : did not save PMDs (as part of stopping the PMU)
 * 	       0 : saved PMDs (no need to save them in caller)
 */
int pfm_arch_ctxswout_thread(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *arch_info;

	arch_info = pfm_pmu_info();
	/*
	 * disable lazy restore of the PMC/PMD registers.
	 */
	ctx->active_set->priv_flags |= PFM_SETFL_PRIV_MOD_BOTH;

	if (ctx->state == PFM_CTX_MASKED)
		return 1;

	pfm_stop_active(task, ctx, ctx->active_set);

	if (arch_info->ctxswout_thread)
		arch_info->ctxswout_thread(task, ctx, ctx->active_set);

	return pfm_arch_is_active(ctx);
}

/*
 * Called from pfm_ctxsw
 */
void pfm_arch_ctxswin_thread(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *arch_info;

	arch_info = pfm_pmu_info();
	if (ctx->state != PFM_CTX_MASKED && ctx->flags.started == 1) {
		BUG_ON(!arch_info->enable_counters);
		arch_info->enable_counters(ctx, ctx->active_set);
	}

	if (arch_info->ctxswin_thread)
		arch_info->ctxswin_thread(task, ctx, ctx->active_set);
}

/*
 * Called from pfm_stop() and idle notifier
 *
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
 * Enable active monitoring. Called from pfm_start() and
 * pfm_arch_unmask_monitoring().
 *
 * Interrupts are masked. Context is locked. Set is the active set.
 *
 * For per-thread:
 * 	Task is not necessarily current. If not current task, then task
 * 	is guaranteed stopped and off any cpu. No access to PMU if task
 *	is not current.
 *
 * For system-wide:
 * 	Task is always current
 */
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *arch_info;

	arch_info = pfm_pmu_info();
	if (task != current)
		return;

	BUG_ON(!arch_info->enable_counters);

	arch_info->enable_counters(ctx, ctx->active_set);
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
	struct pfm_arch_pmu_info *arch_info;
	u64 *used_pmds;
	u16 i, num;

	arch_info = pfm_pmu_info();

	/* The model-specific module can override the default
	 * restore-PMD method.
	 */
	if (arch_info->restore_pmds)
		return arch_info->restore_pmds(ctx, set);

	num = set->nused_pmds;
	used_pmds = set->used_pmds;

	for (i = 0; num; i++) {
		if (likely(test_bit(i, used_pmds))) {
			pfm_write_pmd(ctx, i, set->pmds[i].value);
			num--;
		}
	}
}

/*
 * function called from pfm_switch_sets(), pfm_context_load_thread(),
 * pfm_context_load_sys(), pfm_ctxsw(), pfm_switch_sets()
 * context is locked. Interrupts are masked. set cannot be NULL.
 * Access to the PMU is guaranteed.
 *
 * function must restore all PMC registers from set, if needed.
 */
void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *arch_info;
	u64 *impl_pmcs;
	unsigned int i, max_pmc, reg;

	arch_info = pfm_pmu_info();
	/* The model-specific module can override the default
	 * restore-PMC method.
	 */
	if (arch_info->restore_pmcs)
		return arch_info->restore_pmcs(ctx, set);

	/* The "common" powerpc model's enable the counters simply by writing
	 * all the control registers. Therefore, if we're masked or stopped we
	 * don't need to bother restoring the PMCs now.
	 */
	if (ctx->state == PFM_CTX_MASKED || ctx->flags.started == 0)
		return;

	max_pmc = ctx->regs.max_pmc;
	impl_pmcs = ctx->regs.pmcs;

	/*
	 * Restore all pmcs in reverse order to ensure the counters aren't
	 * enabled before their event selectors are set correctly.
	 */
	reg = max_pmc - 1;
	for (i = 0; i < max_pmc; i++) {
		if (test_bit(reg, impl_pmcs))
			pfm_arch_write_pmc(ctx, reg, set->pmcs[reg]);
		reg--;
	}
}

char *pfm_arch_get_pmu_module_name(void)
{
	unsigned int pvr = mfspr(SPRN_PVR);

	switch (PVR_VER(pvr)) {
	case 0x0004: /* 604 */
	case 0x0009: /* 604e;  */
	case 0x000A: /* 604ev */
	case 0x0008: /* 750/740 */
	case 0x7000: /* 750FX */
	case 0x7001:
	case 0x7002: /* 750GX */
	case 0x000C: /* 7400 */
	case 0x800C: /* 7410 */
	case 0x8000: /* 7451/7441 */
	case 0x8001: /* 7455/7445 */
	case 0x8002: /* 7457/7447 */
	case 0x8003: /* 7447A */
	case 0x8004: /* 7448 */
		return("perfmon_ppc32");
	case PV_POWER4:
	case PV_POWER4p:
		return "perfmon_power4";
	case PV_POWER5:
		return "perfmon_power5";
	case PV_POWER5p:
		if (PVR_REV(pvr) < 0x300)
			/* PMU behaves like POWER5 */
			return "perfmon_power5";
		else
			/* PMU behaves like POWER6 */
			return "perfmon_power6";
	case PV_POWER6:
		return "perfmon_power6";
	case PV_970:
	case PV_970FX:
	case PV_970MP:
		return "perfmon_ppc970";
	case PV_BE:
		return "perfmon_cell";
	}
	return NULL;
}

void pfm_arch_init_percpu(void)
{
#ifdef CONFIG_PPC64
	extern void ppc64_enable_pmcs(void);
	ppc64_enable_pmcs();
#endif
}

/**
 * powerpc_irq_handler
 *
 * Get the perfmon context that belongs to the current CPU, and call the
 * model-specific interrupt handler.
 **/
void powerpc_irq_handler(struct pt_regs *regs)
{
	struct pfm_arch_pmu_info *arch_info;
	struct pfm_context *ctx;

	if (! regs->softe) {
		/*
		 * We got a PMU interrupt while interrupts were soft
		 * disabled.  Disable hardware interrupts by clearing
		 * MSR_EE and also clear PMAO because we will need to set
		 * that again later when interrupts are re-enabled and
		 * raw_local_irq_restore() sees that the pmu_except_pending
		 * flag is set.
		 */
		regs->msr &= ~MSR_EE;
		get_paca()->pmu_except_pending = 1;
		mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) & ~MMCR0_PMAO);
		return;
	}

	arch_info = pfm_pmu_info();
	if (arch_info->irq_handler) {
		ctx = __get_cpu_var(pmu_ctx);
		if (likely(ctx))
			arch_info->irq_handler(regs, ctx);
	}
}
