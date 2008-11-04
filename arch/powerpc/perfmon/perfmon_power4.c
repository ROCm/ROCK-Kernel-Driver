/*
 * This file contains the POWER4 PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (c) 2007, IBM Corporation.
 *
 * Based on a simple modification of perfmon_power5.c for POWER4 by
 * Corey Ashford <cjashfor@us.ibm.com>.
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
MODULE_DESCRIPTION("POWER4 PMU description table");
MODULE_LICENSE("GPL");

static struct pfm_regmap_desc pfm_power4_pmc_desc[] = {
/* mmcr0 */ PMC_D(PFM_REG_I, "MMCR0", MMCR0_FC, 0, 0, SPRN_MMCR0),
/* mmcr1 */ PMC_D(PFM_REG_I, "MMCR1", 0, 0, 0, SPRN_MMCR1),
/* mmcra */ PMC_D(PFM_REG_I, "MMCRA", 0, 0, 0, SPRN_MMCRA)
};
#define PFM_PM_NUM_PMCS	ARRAY_SIZE(pfm_power4_pmc_desc)

/* The TB and PURR registers are read-only. Also, note that the TB register
 * actually consists of both the 32-bit SPRN_TBRU and SPRN_TBRL registers.
 * For Perfmon2's purposes, we'll treat it as a single 64-bit register.
 */
static struct pfm_regmap_desc pfm_power4_pmd_desc[] = {
/* tb    */ PMD_D((PFM_REG_I|PFM_REG_RO), "TB", SPRN_TBRL),
/* pmd1  */ PMD_D(PFM_REG_C, "PMC1", SPRN_PMC1),
/* pmd2  */ PMD_D(PFM_REG_C, "PMC2", SPRN_PMC2),
/* pmd3  */ PMD_D(PFM_REG_C, "PMC3", SPRN_PMC3),
/* pmd4  */ PMD_D(PFM_REG_C, "PMC4", SPRN_PMC4),
/* pmd5  */ PMD_D(PFM_REG_C, "PMC5", SPRN_PMC5),
/* pmd6  */ PMD_D(PFM_REG_C, "PMC6", SPRN_PMC6),
/* pmd7  */ PMD_D(PFM_REG_C, "PMC7", SPRN_PMC7),
/* pmd8  */ PMD_D(PFM_REG_C, "PMC8", SPRN_PMC8)
};
#define PFM_PM_NUM_PMDS	ARRAY_SIZE(pfm_power4_pmd_desc)

static int pfm_power4_probe_pmu(void)
{
	unsigned long pvr = mfspr(SPRN_PVR);
	int ver = PVR_VER(pvr);

	if ((ver == PV_POWER4) || (ver == PV_POWER4p))
		return 0;

	return -1;
}

static void pfm_power4_write_pmc(unsigned int cnum, u64 value)
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

static void pfm_power4_write_pmd(unsigned int cnum, u64 value)
{
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
	case SPRN_PMC5:
		mtspr(SPRN_PMC5, value & ovfl_mask);
		break;
	case SPRN_PMC6:
		mtspr(SPRN_PMC6, value & ovfl_mask);
		break;
	case SPRN_PMC7:
		mtspr(SPRN_PMC7, value & ovfl_mask);
		break;
	case SPRN_PMC8:
		mtspr(SPRN_PMC8, value & ovfl_mask);
		break;
	case SPRN_TBRL:
	case SPRN_PURR:
		/* Ignore writes to read-only registers. */
		break;
	default:
		BUG();
	}
}

static u64 pfm_power4_read_pmd(unsigned int cnum)
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
	case SPRN_PMC5:
		return mfspr(SPRN_PMC5);
	case SPRN_PMC6:
		return mfspr(SPRN_PMC6);
	case SPRN_PMC7:
		return mfspr(SPRN_PMC7);
	case SPRN_PMC8:
		return mfspr(SPRN_PMC8);
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

/* forward decl */
static void pfm_power4_disable_counters(struct pfm_context *ctx,
					struct pfm_event_set *set);

/**
 * pfm_power4_enable_counters
 *
 **/
static void pfm_power4_enable_counters(struct pfm_context *ctx,
				       struct pfm_event_set *set)
{
	unsigned int i, max_pmc;

	/* Make sure the counters are disabled before touching the other
	   control registers */
	pfm_power4_disable_counters(ctx, set);

	max_pmc = ctx->regs.max_pmc;

	/* Write MMCR0 last, and a fairly easy way to do this is to write
	   the registers in the reverse order */
	for (i = max_pmc; i != 0; i--)
		if (test_bit(i - 1, set->used_pmcs))
			pfm_power4_write_pmc(i - 1, set->pmcs[i - 1]);
}

/**
 * pfm_power4_disable_counters
 *
 **/
static void pfm_power4_disable_counters(struct pfm_context *ctx,
					struct pfm_event_set *set)
{
	/* Set the Freeze Counters bit */
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) | MMCR0_FC);
	asm volatile ("sync");
}

/**
 * pfm_power4_get_ovfl_pmds
 *
 * Determine which counters in this set have overflowed and fill in the
 * set->povfl_pmds mask and set->npend_ovfls count.
 **/
static void pfm_power4_get_ovfl_pmds(struct pfm_context *ctx,
				     struct pfm_event_set *set)
{
	unsigned int i;
	unsigned int max_pmd = ctx->regs.max_intr_pmd;
	u64 *used_pmds = set->used_pmds;
	u64 *cntr_pmds = ctx->regs.cnt_pmds;
	u64 width_mask = 1 << pfm_pmu_conf->counter_width;
	u64 new_val, mask[PFM_PMD_BV];

	bitmap_and(cast_ulp(mask), cast_ulp(cntr_pmds),
		   cast_ulp(used_pmds), max_pmd);

	for (i = 0; i < max_pmd; i++) {
		if (test_bit(i, mask)) {
			new_val = pfm_power4_read_pmd(i);
			if (new_val & width_mask) {
				set_bit(i, set->povfl_pmds);
				set->npend_ovfls++;
			}
		}
	}
}

static void pfm_power4_irq_handler(struct pt_regs *regs,
				   struct pfm_context *ctx)
{
	u32 mmcr0;

	/* Disable the counters (set the freeze bit) to not polute
	 * the counts.
	 */
	mmcr0 = mfspr(SPRN_MMCR0);
	mtspr(SPRN_MMCR0, (mmcr0 | MMCR0_FC));

	/* Set the PMM bit (see comment below). */
	mtmsrd(mfmsr() | MSR_PMM);

	pfm_interrupt_handler(instruction_pointer(regs), regs);

	mmcr0 = mfspr(SPRN_MMCR0);

	/*
	 * Reset the perfmon trigger if
	 * not in masking mode.
	 */
	if (ctx->state != PFM_CTX_MASKED)
		mmcr0 |= MMCR0_PMXE;

	/*
	 * We must clear the PMAO bit on some (GQ) chips. Just do it
	 * all the time.
	 */
	mmcr0 &= ~MMCR0_PMAO;

	/*
	 * Now clear the freeze bit, counting will not start until we
	 * rfid from this exception, because only at that point will
	 * the PMM bit be cleared.
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);
}

static void pfm_power4_resend_irq(struct pfm_context *ctx)
{
	/*
	 * Assert the PMAO bit to cause a PMU interrupt.  Make sure we
	 * trigger the edge detection circuitry for PMAO
	 */
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) & ~MMCR0_PMAO);
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) | MMCR0_PMAO);
}

struct pfm_arch_pmu_info pfm_power4_pmu_info = {
	.pmu_style        = PFM_POWERPC_PMU_POWER4,
	.write_pmc        = pfm_power4_write_pmc,
	.write_pmd        = pfm_power4_write_pmd,
	.read_pmd         = pfm_power4_read_pmd,
	.irq_handler      = pfm_power4_irq_handler,
	.get_ovfl_pmds    = pfm_power4_get_ovfl_pmds,
	.enable_counters  = pfm_power4_enable_counters,
	.disable_counters = pfm_power4_disable_counters,
	.resend_irq       = pfm_power4_resend_irq
};

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static struct pfm_pmu_config pfm_power4_pmu_conf = {
	.pmu_name = "POWER4",
	.counter_width = 31,
	.pmd_desc = pfm_power4_pmd_desc,
	.pmc_desc = pfm_power4_pmc_desc,
	.num_pmc_entries = PFM_PM_NUM_PMCS,
	.num_pmd_entries = PFM_PM_NUM_PMDS,
	.probe_pmu  = pfm_power4_probe_pmu,
	.pmu_info = &pfm_power4_pmu_info,
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE
};

static int __init pfm_power4_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_power4_pmu_conf);
}

static void __exit pfm_power4_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_power4_pmu_conf);
}

module_init(pfm_power4_pmu_init_module);
module_exit(pfm_power4_pmu_cleanup_module);
