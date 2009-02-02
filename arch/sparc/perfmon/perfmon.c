/* perfmon.c: sparc64 perfmon support
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/perfmon_kern.h>

#include <asm/system.h>
#include <asm/spitfire.h>
#include <asm/hypervisor.h>

struct pcr_ops {
	void (*write)(u64);
	u64 (*read)(void);
};

static void direct_write_pcr(u64 val)
{
	write_pcr(val);
}

static u64 direct_read_pcr(void)
{
	u64 pcr;

	read_pcr(pcr);

	return pcr;
}

static struct pcr_ops direct_pcr_ops = {
	.write	= direct_write_pcr,
	.read	= direct_read_pcr,
};

/* Using the hypervisor call is needed so that we can set the
 * hypervisor trace bit correctly, which is hyperprivileged.
 */
static void n2_write_pcr(u64 val)
{
	unsigned long ret;

	ret = sun4v_niagara2_setperf(HV_N2_PERF_SPARC_CTL, val);
	if (val != HV_EOK)
		write_pcr(val);
}

static u64 n2_read_pcr(void)
{
	u64 pcr;

	read_pcr(pcr);

	return pcr;
}

static struct pcr_ops n2_pcr_ops = {
	.write	= n2_write_pcr,
	.read	= n2_read_pcr,
};

static struct pcr_ops *pcr_ops;

void pfm_arch_write_pmc(struct pfm_context *ctx,
			unsigned int cnum, u64 value)
{
	/*
	 * we only write to the actual register when monitoring is
	 * active (pfm_start was issued)
	 */
	if (ctx && ctx->flags.started == 0)
		return;

	pcr_ops->write(value);
}

u64 pfm_arch_read_pmc(struct pfm_context *ctx, unsigned int cnum)
{
	return pcr_ops->read();
}

/*
 * collect pending overflowed PMDs. Called from pfm_ctxsw()
 * and from PMU interrupt handler. Must fill in set->povfl_pmds[]
 * and set->npend_ovfls. Interrupts are masked
 */
static void __pfm_get_ovfl_pmds(struct pfm_context *ctx, struct pfm_event_set *set)
{
	unsigned int max = ctx->regs.max_intr_pmd;
	u64 wmask = 1ULL << pfm_pmu_conf->counter_width;
	u64 *intr_pmds = ctx->regs.intr_pmds;
	u64 *used_mask = set->used_pmds;
	u64 mask[PFM_PMD_BV];
	unsigned int i;

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
			u64 new_val = pfm_arch_read_pmd(ctx, i);

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
	unsigned int i, max = ctx->regs.max_pmc;

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

	pfm_stop_active(task, ctx, ctx->active_set);

	return 1;
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
	unsigned int max_pmc = ctx->regs.max_pmc;
	unsigned int i;

	if (task != current)
		return;

	set = ctx->active_set;
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
	unsigned int max_pmd = ctx->regs.max_pmd;
	u64 ovfl_mask = pfm_pmu_conf->ovfl_mask;
	u64 *impl_pmds = ctx->regs.pmds;
	unsigned int i;

	/*
	 * must restore all pmds to avoid leaking
	 * information to user.
	 */
	for (i = 0; i < max_pmd; i++) {
		u64 val;

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
	unsigned int max_pmc = ctx->regs.max_pmc;
	u64 *impl_pmcs = ctx->regs.pmcs;
	unsigned int i;

	/* If we're masked or stopped we don't need to bother restoring
	 * the PMCs now.
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
	return NULL;
}

void perfmon_interrupt(struct pt_regs *regs)
{
	pfm_interrupt_handler(instruction_pointer(regs), regs);
}

static struct pfm_regmap_desc pfm_sparc64_pmc_desc[] = {
	PMC_D(PFM_REG_I, "PCR", 0, 0, 0, 0),
};

static struct pfm_regmap_desc pfm_sparc64_pmd_desc[] = {
	PMD_D(PFM_REG_C, "PIC0", 0),
	PMD_D(PFM_REG_C, "PIC1", 0),
};

static int pfm_sparc64_probe(void)
{
	return 0;
}

static struct pfm_pmu_config pmu_sparc64_pmu_conf = {
	.counter_width	= 31,
	.pmd_desc	= pfm_sparc64_pmd_desc,
	.num_pmd_entries = 2,
	.pmc_desc	= pfm_sparc64_pmc_desc,
	.num_pmc_entries = 1,
	.probe_pmu	= pfm_sparc64_probe,
	.flags		= PFM_PMU_BUILTIN_FLAG,
	.owner		= THIS_MODULE,
};

static unsigned long perf_hsvc_group;
static unsigned long perf_hsvc_major;
static unsigned long perf_hsvc_minor;

static int __init register_perf_hsvc(void)
{
	if (tlb_type == hypervisor) {
		switch (sun4v_chip_type) {
		case SUN4V_CHIP_NIAGARA1:
			perf_hsvc_group = HV_GRP_N2_CPU;
			break;

		case SUN4V_CHIP_NIAGARA2:
			perf_hsvc_group = HV_GRP_N2_CPU;
			break;

		default:
			return -ENODEV;
		}


		perf_hsvc_major = 1;
		perf_hsvc_minor = 0;
		if (sun4v_hvapi_register(perf_hsvc_group,
					 perf_hsvc_major,
					 &perf_hsvc_minor)) {
			printk("perfmon: Could not register N2 hvapi.\n");
			return -ENODEV;
		}
	}
	return 0;
}

static void unregister_perf_hsvc(void)
{
	if (tlb_type != hypervisor)
		return;
	sun4v_hvapi_unregister(perf_hsvc_group);
}

static int __init pfm_sparc64_pmu_init(void)
{
	u64 mask;
	int err;

	err = register_perf_hsvc();
	if (err)
		return err;

	if (tlb_type == hypervisor &&
	    sun4v_chip_type == SUN4V_CHIP_NIAGARA2)
		pcr_ops = &n2_pcr_ops;
	else
		pcr_ops = &direct_pcr_ops;

	if (!strcmp(sparc_pmu_type, "ultra12"))
		mask = (0xf << 11) | (0xf << 4) | 0x7;
	else if (!strcmp(sparc_pmu_type, "ultra3") ||
		 !strcmp(sparc_pmu_type, "ultra3i") ||
		 !strcmp(sparc_pmu_type, "ultra3+") ||
		 !strcmp(sparc_pmu_type, "ultra4+"))
		mask = (0x3f << 11) | (0x3f << 4) | 0x7;
	else if (!strcmp(sparc_pmu_type, "niagara2"))
		mask = ((1UL << 63) | (1UL << 62) |
			(1UL << 31) | (0xfUL << 27) | (0xffUL << 19) |
			(1UL << 18) | (0xfUL << 14) | (0xff << 6) |
			(0x3UL << 4) | 0x7UL);
	else if (!strcmp(sparc_pmu_type, "niagara"))
		mask = ((1UL << 9) | (1UL << 8) |
			(0x7UL << 4) | 0x7UL);
	else {
		err = -ENODEV;
		goto out_err;
	}

	pmu_sparc64_pmu_conf.pmu_name = sparc_pmu_type;
	pfm_sparc64_pmc_desc[0].rsvd_msk = ~mask;

	return pfm_pmu_register(&pmu_sparc64_pmu_conf);

out_err:
	unregister_perf_hsvc();
	return err;
}

static void __exit pfm_sparc64_pmu_exit(void)
{
	unregister_perf_hsvc();
	return pfm_pmu_unregister(&pmu_sparc64_pmu_conf);
}

module_init(pfm_sparc64_pmu_init);
module_exit(pfm_sparc64_pmu_exit);
