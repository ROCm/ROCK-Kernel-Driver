/*
 * This file contains the POWER6 PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (c) 2007, IBM Corporation
 *
 * Based on perfmon_power5.c, and written by Carl Love <carll@us.ibm.com>
 * and Kevin Corry <kevcorry@us.ibm.com>.  Some fixes and refinement by
 * Corey Ashford <cjashfor@us.ibm.com>
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
#include <linux/module.h>
#include <linux/perfmon_kern.h>

MODULE_AUTHOR("Corey Ashford <cjashfor@us.ibm.com>");
MODULE_DESCRIPTION("POWER6 PMU description table");
MODULE_LICENSE("GPL");

static struct pfm_regmap_desc pfm_power6_pmc_desc[] = {
/* mmcr0 */ PMC_D(PFM_REG_I, "MMCR0", MMCR0_FC, 0, 0, SPRN_MMCR0),
/* mmcr1 */ PMC_D(PFM_REG_I, "MMCR1", 0, 0, 0, SPRN_MMCR1),
/* mmcra */ PMC_D(PFM_REG_I, "MMCRA", 0, 0, 0, SPRN_MMCRA)
};
#define PFM_PM_NUM_PMCS	ARRAY_SIZE(pfm_power6_pmc_desc)
#define PFM_DELTA_TB    10000   /* Not a real registers */
#define PFM_DELTA_PURR  10001

/*
 * counters wrap to zero at transition from 2^32-1 to 2^32.  Note:
 * interrupt generated at transition from 2^31-1 to 2^31
 */
#define OVERFLOW_VALUE    0x100000000UL

/* The TB and PURR registers are read-only. Also, note that the TB register
 * actually consists of both the 32-bit SPRN_TBRU and SPRN_TBRL registers.
 * For Perfmon2's purposes, we'll treat it as a single 64-bit register.
 */
static struct pfm_regmap_desc pfm_power6_pmd_desc[] = {
	/* On POWER 6 PMC5 and PMC6 are not writable, they do not
	 * generate interrupts, and do not qualify their counts
	 * based on problem mode, supervisor mode or hypervisor mode.
	 * These two counters are implemented as virtual counters
	 * to make the appear to work like the other counters.  A
	 * kernel timer is used sample the real PMC5 and PMC6 and
	 * update the virtual counters.
	 */
/* tb    */ PMD_D((PFM_REG_I|PFM_REG_RO), "TB", SPRN_TBRL),
/* pmd1  */ PMD_D(PFM_REG_C, "PMC1", SPRN_PMC1),
/* pmd2  */ PMD_D(PFM_REG_C, "PMC2", SPRN_PMC2),
/* pmd3  */ PMD_D(PFM_REG_C, "PMC3", SPRN_PMC3),
/* pmd4  */ PMD_D(PFM_REG_C, "PMC4", SPRN_PMC4),
/* pmd5  */ PMD_D((PFM_REG_I|PFM_REG_V), "PMC5", SPRN_PMC5),
/* pmd6  */ PMD_D((PFM_REG_I|PFM_REG_V), "PMC6", SPRN_PMC6),
/* purr  */ PMD_D((PFM_REG_I|PFM_REG_RO), "PURR", SPRN_PURR),
/* delta purr */ PMD_D((PFM_REG_I|PFM_REG_V), "DELTA_TB", PFM_DELTA_TB),
/* delta tb   */ PMD_D((PFM_REG_I|PFM_REG_V), "DELTA_PURR", PFM_DELTA_PURR),
};

#define PFM_PM_NUM_PMDS	ARRAY_SIZE(pfm_power6_pmd_desc)

u32 pmc5_start_save[NR_CPUS];
u32 pmc6_start_save[NR_CPUS];

static struct timer_list pmc5_6_update[NR_CPUS];
u64 enable_cntrs_cnt;
u64 disable_cntrs_cnt;
u64 call_delta;
u64 pm5_6_interrupt;
u64 pm1_4_interrupt;
/* need ctx_arch for kernel timer.  Can't get it in context of the kernel
 * timer.
 */
struct pfm_arch_context *pmc5_6_ctx_arch[NR_CPUS];
long int update_time;

static void delta(int cpu_num, struct pfm_arch_context *ctx_arch)
{
	u32 tmp5, tmp6;

	call_delta++;

	tmp5 = (u32) mfspr(SPRN_PMC5);
	tmp6 = (u32) mfspr(SPRN_PMC6);

	/*
	 * The following difference calculation relies on 32-bit modular
	 * arithmetic for the deltas to come out correct (especially in the
	 * presence of a 32-bit counter wrap).
	 */
	ctx_arch->powergs_pmc5 += (u64)(tmp5 - pmc5_start_save[cpu_num]);
	ctx_arch->powergs_pmc6 += (u64)(tmp6 - pmc6_start_save[cpu_num]);

	pmc5_start_save[cpu_num] = tmp5;
	pmc6_start_save[cpu_num] = tmp6;

	return;
}


static void pmc5_6_updater(unsigned long cpu_num)
{
	/* update the virtual pmd 5 and pmd 6 counters */

	delta(cpu_num, pmc5_6_ctx_arch[cpu_num]);
	mod_timer(&pmc5_6_update[cpu_num], jiffies + update_time);
}


static int pfm_power6_probe_pmu(void)
{
	unsigned long pvr = mfspr(SPRN_PVR);

	switch (PVR_VER(pvr)) {
	case PV_POWER6:
		return 0;
	case PV_POWER5p:
		/* If this is a POWER5+ and the revision is less than 0x300,
		   don't treat it as a POWER6. */
		return (PVR_REV(pvr) < 0x300) ? -1 : 0;
	default:
		return -1;
	}
}

static void pfm_power6_write_pmc(unsigned int cnum, u64 value)
{
	switch (pfm_pmu_conf->pmc_desc[cnum].hw_addr) {
	case SPRN_MMCR0:
		mtspr(SPRN_MMCR0, value);
		break;
	case SPRN_MMCR1:
		mtspr(SPRN_MMCR1, value);
		break;
	case SPRN_MMCRA:
		mtspr(SPRN_MMCRA, value);
		break;
	default:
		BUG();
	}
}

static void pfm_power6_write_pmd(unsigned int cnum, u64 value)
{
	/* On POWER 6 PMC5 and PMC6 are implemented as
	 * virtual counters.  See comment in pfm_power6_pmd_desc
	 * definition.
	 */
	u64 ovfl_mask = pfm_pmu_conf->ovfl_mask;

	switch (pfm_pmu_conf->pmd_desc[cnum].hw_addr) {
	case SPRN_PMC1:
		mtspr(SPRN_PMC1, value & ovfl_mask);
		break;
	case SPRN_PMC2:
		mtspr(SPRN_PMC2, value & ovfl_mask);
		break;
	case SPRN_PMC3:
		mtspr(SPRN_PMC3, value & ovfl_mask);
		break;
	case SPRN_PMC4:
		mtspr(SPRN_PMC4, value & ovfl_mask);
		break;
	case SPRN_TBRL:
	case SPRN_PURR:
		/* Ignore writes to read-only registers. */
		break;
	default:
		BUG();
	}
}

static u64 pfm_power6_sread(struct pfm_context *ctx, unsigned int cnum)
{
	struct pfm_arch_context *ctx_arch = pfm_ctx_arch(ctx);
	int cpu_num = smp_processor_id();

	/* On POWER 6 PMC5 and PMC6 are implemented as
	 * virtual counters.  See comment in pfm_power6_pmd_desc
	 * definition.
	 */

	switch (pfm_pmu_conf->pmd_desc[cnum].hw_addr) {
	case SPRN_PMC5:
		return ctx_arch->powergs_pmc5 + (u64)((u32)mfspr(SPRN_PMC5) - pmc5_start_save[cpu_num]);
		break;

	case SPRN_PMC6:
		return ctx_arch->powergs_pmc6 + (u64)((u32)mfspr(SPRN_PMC6) - pmc6_start_save[cpu_num]);
		break;

	case PFM_DELTA_TB:
		return ctx_arch->delta_tb
			+ (((u64)mfspr(SPRN_TBRU) << 32) | mfspr(SPRN_TBRL))
			- ctx_arch->delta_tb_start;
		break;

	case PFM_DELTA_PURR:
		return ctx_arch->delta_purr
			+ mfspr(SPRN_PURR)
			- ctx_arch->delta_purr_start;
		break;

	default:
		BUG();
	}
}

void pfm_power6_swrite(struct pfm_context *ctx, unsigned int cnum,
	u64 val)
{
	struct pfm_arch_context *ctx_arch = pfm_ctx_arch(ctx);
	int cpu_num = smp_processor_id();

	switch (pfm_pmu_conf->pmd_desc[cnum].hw_addr) {
	case SPRN_PMC5:
		pmc5_start_save[cpu_num] = mfspr(SPRN_PMC5);
		ctx_arch->powergs_pmc5 = val;
		break;

	case SPRN_PMC6:
		pmc6_start_save[cpu_num] = mfspr(SPRN_PMC6);
		ctx_arch->powergs_pmc6 = val;
		break;

	case PFM_DELTA_TB:
		ctx_arch->delta_tb_start =
			(((u64)mfspr(SPRN_TBRU) << 32) | mfspr(SPRN_TBRL));
		ctx_arch->delta_tb = val;
		break;

	case PFM_DELTA_PURR:
		ctx_arch->delta_purr_start = mfspr(SPRN_PURR);
		ctx_arch->delta_purr = val;
		break;

	default:
		BUG();
	}
}

static u64 pfm_power6_read_pmd(unsigned int cnum)
{
	switch (pfm_pmu_conf->pmd_desc[cnum].hw_addr) {
	case SPRN_PMC1:
		return mfspr(SPRN_PMC1);
	case SPRN_PMC2:
		return mfspr(SPRN_PMC2);
	case SPRN_PMC3:
		return mfspr(SPRN_PMC3);
	case SPRN_PMC4:
		return mfspr(SPRN_PMC4);
	case SPRN_TBRL:
		return ((u64)mfspr(SPRN_TBRU) << 32) | mfspr(SPRN_TBRL);
	case SPRN_PURR:
		if (cpu_has_feature(CPU_FTR_PURR))
			return mfspr(SPRN_PURR);
		else
			return 0;
	default:
		BUG();
	}
}


/**
 * pfm_power6_enable_counters
 *
 **/
static void pfm_power6_enable_counters(struct pfm_context *ctx,
				       struct pfm_event_set *set)
{

	unsigned int i, max_pmc;
	int cpu_num = smp_processor_id();
	struct pfm_arch_context *ctx_arch;

	enable_cntrs_cnt++;

	/* need the ctx passed down to the routine */
	ctx_arch = pfm_ctx_arch(ctx);
	max_pmc = ctx->regs.max_pmc;

	/* Write MMCR0 last, and a fairly easy way to do this is to write
	   the registers in the reverse order */
	for (i = max_pmc; i != 0; i--)
		if (test_bit(i - 1, set->used_pmcs))
			pfm_power6_write_pmc(i - 1, set->pmcs[i - 1]);

	/* save current free running HW event count */
	pmc5_start_save[cpu_num] = mfspr(SPRN_PMC5);
	pmc6_start_save[cpu_num] = mfspr(SPRN_PMC6);

	ctx_arch->delta_purr_start = mfspr(SPRN_PURR);

	if (cpu_has_feature(CPU_FTR_PURR))
		ctx_arch->delta_tb_start =
			((u64)mfspr(SPRN_TBRU) << 32) | mfspr(SPRN_TBRL);
	else
		ctx_arch->delta_tb_start = 0;

	/* Start kernel timer for this cpu to periodically update
	 * the virtual counters.
	 */
	init_timer(&pmc5_6_update[cpu_num]);
	pmc5_6_update[cpu_num].function = pmc5_6_updater;
	pmc5_6_update[cpu_num].data = (unsigned long) cpu_num;
	pmc5_6_update[cpu_num].expires = jiffies + update_time;
	/* context for this timer, timer will be removed if context
	 * is switched because the counters will be stopped first.
	 * NEEDS WORK, I think this is all ok, a little concerned about a
	 * race between the kernel timer going off right as the counters
	 * are being stopped and the context switching.  Need to think
	 * about this.
	 */
	pmc5_6_ctx_arch[cpu_num] = ctx_arch;
	add_timer(&pmc5_6_update[cpu_num]);
}

/**
 * pfm_power6_disable_counters
 *
 **/
static void pfm_power6_disable_counters(struct pfm_context *ctx,
					struct pfm_event_set *set)
{
	struct pfm_arch_context *ctx_arch;
	int cpu_num = smp_processor_id();

	disable_cntrs_cnt++;

	/* Set the Freeze Counters bit */
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) | MMCR0_FC);
	asm volatile ("sync");

	/* delete kernel update timer */
	del_timer_sync(&pmc5_6_update[cpu_num]);

	/* Update the virtual pmd 5 and 6 counters from the free running
	 * HW counters
	 */
	ctx_arch = pfm_ctx_arch(ctx);
	delta(cpu_num, ctx_arch);

	ctx_arch->delta_tb +=
		(((u64)mfspr(SPRN_TBRU) << 32) | mfspr(SPRN_TBRL))
		- ctx_arch->delta_tb_start;

	ctx_arch->delta_purr += mfspr(SPRN_PURR)
		- ctx_arch->delta_purr_start;
}

/**
 * pfm_power6_get_ovfl_pmds
 *
 * Determine which counters in this set have overflowed and fill in the
 * set->povfl_pmds mask and set->npend_ovfls count.
 **/
static void pfm_power6_get_ovfl_pmds(struct pfm_context *ctx,
				     struct pfm_event_set *set)
{
	unsigned int i;
	unsigned int first_intr_pmd = ctx->regs.first_intr_pmd;
	unsigned int max_intr_pmd = ctx->regs.max_intr_pmd;
	u64 *used_pmds = set->used_pmds;
	u64 *cntr_pmds = ctx->regs.cnt_pmds;
	u64 width_mask = 1 << pfm_pmu_conf->counter_width;
	u64 new_val, mask[PFM_PMD_BV];

	bitmap_and(cast_ulp(mask), cast_ulp(cntr_pmds), cast_ulp(used_pmds), max_intr_pmd);

	/* max_intr_pmd is actually the last interrupting pmd register + 1 */
	for (i = first_intr_pmd; i < max_intr_pmd; i++) {
		if (test_bit(i, mask)) {
			new_val = pfm_power6_read_pmd(i);
			if (new_val & width_mask) {
				set_bit(i, set->povfl_pmds);
				set->npend_ovfls++;
			}
		}
	}
}

static void pfm_power6_irq_handler(struct pt_regs *regs,
				   struct pfm_context *ctx)
{
	u32 mmcr0;
	u64 mmcra;

	/* Disable the counters (set the freeze bit) to not polute
	 * the counts.
	 */
	mmcr0 = mfspr(SPRN_MMCR0);
	mtspr(SPRN_MMCR0, (mmcr0 | MMCR0_FC));
	mmcra = mfspr(SPRN_MMCRA);

	/* Set the PMM bit (see comment below). */
	mtmsrd(mfmsr() | MSR_PMM);

	pm1_4_interrupt++;

	pfm_interrupt_handler(instruction_pointer(regs), regs);

	mmcr0 = mfspr(SPRN_MMCR0);

	/*
	 * Reset the perfmon trigger if
	 * not in masking mode.
	 */
	if (ctx->state != PFM_CTX_MASKED)
		mmcr0 |= MMCR0_PMXE;

	/*
	 * Clear the PMU Alert Occurred bit
	 */
	mmcr0 &= ~MMCR0_PMAO;

	/* Clear the appropriate bits in the MMCRA. */
	mmcra &= ~(POWER6_MMCRA_THRM | POWER6_MMCRA_OTHER);
	mtspr(SPRN_MMCRA, mmcra);

	/*
	 * Now clear the freeze bit, counting will not start until we
	 * rfid from this exception, because only at that point will
	 * the PMM bit be cleared.
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);
}

static void pfm_power6_resend_irq(struct pfm_context *ctx)
{
	/*
	 * Assert the PMAO bit to cause a PMU interrupt.  Make sure we
	 * trigger the edge detection circuitry for PMAO
	 */
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) & ~MMCR0_PMAO);
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) | MMCR0_PMAO);
}

struct pfm_arch_pmu_info pfm_power6_pmu_info = {
	.pmu_style        = PFM_POWERPC_PMU_POWER6,
	.write_pmc        = pfm_power6_write_pmc,
	.write_pmd        = pfm_power6_write_pmd,
	.read_pmd         = pfm_power6_read_pmd,
	.irq_handler      = pfm_power6_irq_handler,
	.get_ovfl_pmds    = pfm_power6_get_ovfl_pmds,
	.enable_counters  = pfm_power6_enable_counters,
	.disable_counters = pfm_power6_disable_counters,
	.resend_irq       = pfm_power6_resend_irq
};

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static struct pfm_pmu_config pfm_power6_pmu_conf = {
	.pmu_name = "POWER6",
	.counter_width = 31,
	.pmd_desc = pfm_power6_pmd_desc,
	.pmc_desc = pfm_power6_pmc_desc,
	.num_pmc_entries = PFM_PM_NUM_PMCS,
	.num_pmd_entries = PFM_PM_NUM_PMDS,
	.probe_pmu  = pfm_power6_probe_pmu,
	.pmu_info = &pfm_power6_pmu_info,
	.pmd_sread = pfm_power6_sread,
	.pmd_swrite = pfm_power6_swrite,
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE
};

static int __init pfm_power6_pmu_init_module(void)
{
	int ret;
	disable_cntrs_cnt = 0;
	enable_cntrs_cnt = 0;
	call_delta = 0;
	pm5_6_interrupt = 0;
	pm1_4_interrupt = 0;

	/* calculate the time for updating counters 5 and 6 */

	/*
	 * MAX_EVENT_RATE assumes a max instruction issue rate of 2
	 * instructions per clock cycle.  Experience shows that this factor
	 * of 2 is more than adequate.
	 */

# define MAX_EVENT_RATE (ppc_proc_freq * 2)

	/*
	 * Calculate the time, in jiffies, it takes for event counter 5 or
	 * 6 to completely wrap when counting at the max event rate, and
	 * then figure on sampling at twice that rate.
	 */
	update_time = (((unsigned long)HZ * OVERFLOW_VALUE)
		       / ((unsigned long)MAX_EVENT_RATE)) / 2;

	ret =  pfm_pmu_register(&pfm_power6_pmu_conf);
	return ret;
}

static void __exit pfm_power6_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_power6_pmu_conf);
}

module_init(pfm_power6_pmu_init_module);
module_exit(pfm_power6_pmu_cleanup_module);
