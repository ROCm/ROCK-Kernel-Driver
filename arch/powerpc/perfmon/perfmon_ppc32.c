/*
 * This file contains the PPC32 PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Philip Mucci, mucci@cs.utk.edu
 *
 * Based on code from:
 * Copyright (c) 2005 David Gibson, IBM Corporation.
 *
 * Based on perfmon_p6.c:
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
#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include <asm/reg.h>

MODULE_AUTHOR("Philip Mucci <mucci@cs.utk.edu>");
MODULE_DESCRIPTION("PPC32 PMU description table");
MODULE_LICENSE("GPL");

static struct pfm_pmu_config pfm_ppc32_pmu_conf;

static struct pfm_regmap_desc pfm_ppc32_pmc_desc[] = {
/* mmcr0 */ PMC_D(PFM_REG_I, "MMCR0", 0x0, 0, 0, SPRN_MMCR0),
/* mmcr1 */ PMC_D(PFM_REG_I, "MMCR1", 0x0, 0, 0, SPRN_MMCR1),
/* mmcr2 */ PMC_D(PFM_REG_I, "MMCR2", 0x0, 0, 0, SPRN_MMCR2),
};
#define PFM_PM_NUM_PMCS	ARRAY_SIZE(pfm_ppc32_pmc_desc)

static struct pfm_regmap_desc pfm_ppc32_pmd_desc[] = {
/* pmd0  */ PMD_D(PFM_REG_C, "PMC1", SPRN_PMC1),
/* pmd1  */ PMD_D(PFM_REG_C, "PMC2", SPRN_PMC2),
/* pmd2  */ PMD_D(PFM_REG_C, "PMC3", SPRN_PMC3),
/* pmd3  */ PMD_D(PFM_REG_C, "PMC4", SPRN_PMC4),
/* pmd4  */ PMD_D(PFM_REG_C, "PMC5", SPRN_PMC5),
/* pmd5  */ PMD_D(PFM_REG_C, "PMC6", SPRN_PMC6),
};
#define PFM_PM_NUM_PMDS	ARRAY_SIZE(pfm_ppc32_pmd_desc)

static void perfmon_perf_irq(struct pt_regs *regs)
{
	u32 mmcr0;

	/* BLATANTLY STOLEN FROM OPROFILE, then modified */

	/* set the PMM bit (see comment below) */
	mtmsr(mfmsr() | MSR_PMM);

	pfm_interrupt_handler(instruction_pointer(regs), regs);

	/* The freeze bit was set by the interrupt.
	 * Clear the freeze bit, and reenable the interrupt.
	 * The counters won't actually start until the rfi clears
	 * the PMM bit.
	 */

	/* Unfreezes the counters on this CPU, enables the interrupt,
	 * enables the counters to trigger the interrupt, and sets the
	 * counters to only count when the mark bit is not set.
	 */
	mmcr0 = mfspr(SPRN_MMCR0);

	mmcr0 &= ~(MMCR0_FC | MMCR0_FCM0);
	mmcr0 |= (MMCR0_FCECE | MMCR0_PMC1CE | MMCR0_PMCnCE | MMCR0_PMXE);

	mtspr(SPRN_MMCR0, mmcr0);
}

static int pfm_ppc32_probe_pmu(void)
{
	enum ppc32_pmu_type pm_type;
	int nmmcr = 0, npmds = 0, intsok = 0, i;
	unsigned int pvr;
	char *str;

	pvr = mfspr(SPRN_PVR);

	switch (PVR_VER(pvr)) {
	case 0x0004: /* 604 */
		str = "PPC604";
		pm_type = PFM_POWERPC_PMU_604;
		nmmcr = 1;
		npmds = 2;
		break;
	case 0x0009: /* 604e;  */
	case 0x000A: /* 604ev */
		str = "PPC604e";
		pm_type = PFM_POWERPC_PMU_604e;
		nmmcr = 2;
		npmds = 4;
		break;
	case 0x0008: /* 750/740 */
		str = "PPC750";
		pm_type = PFM_POWERPC_PMU_750;
		nmmcr = 2;
		npmds = 4;
		break;
	case 0x7000: /* 750FX */
	case 0x7001:
		str = "PPC750";
		pm_type = PFM_POWERPC_PMU_750;
		nmmcr = 2;
		npmds = 4;
		if ((pvr & 0xFF0F) >= 0x0203)
			intsok = 1;
		break;
	case 0x7002: /* 750GX */
		str = "PPC750";
		pm_type = PFM_POWERPC_PMU_750;
		nmmcr = 2;
		npmds = 4;
		intsok = 1;
	case 0x000C: /* 7400 */
		str = "PPC7400";
		pm_type = PFM_POWERPC_PMU_7400;
		nmmcr = 3;
		npmds = 4;
		break;
	case 0x800C: /* 7410 */
		str = "PPC7410";
		pm_type = PFM_POWERPC_PMU_7400;
		nmmcr = 3;
		npmds = 4;
		if ((pvr & 0xFFFF) >= 0x01103)
			intsok = 1;
		break;
	case 0x8000: /* 7451/7441 */
	case 0x8001: /* 7455/7445 */
	case 0x8002: /* 7457/7447 */
	case 0x8003: /* 7447A */
	case 0x8004: /* 7448 */
		str = "PPC7450";
		pm_type = PFM_POWERPC_PMU_7450;
		nmmcr = 3; npmds = 6;
		intsok = 1;
		break;
	default:
		PFM_INFO("Unknown PVR_VER(0x%x)\n", PVR_VER(pvr));
		return -1;
	}

	/*
	 * deconfigure unimplemented registers
	 */
	for (i = npmds; i < PFM_PM_NUM_PMDS; i++)
		pfm_ppc32_pmd_desc[i].type = PFM_REG_NA;

	for (i = nmmcr; i < PFM_PM_NUM_PMCS; i++)
		pfm_ppc32_pmc_desc[i].type = PFM_REG_NA;

	/*
	 * update PMU description structure
	 */
	pfm_ppc32_pmu_conf.pmu_name = str;
	pfm_ppc32_pmu_info.pmu_style = pm_type;
	pfm_ppc32_pmu_conf.num_pmc_entries = nmmcr;
	pfm_ppc32_pmu_conf.num_pmd_entries = npmds;

	if (intsok == 0)
		PFM_INFO("Interrupts unlikely to work\n");

	return reserve_pmc_hardware(perfmon_perf_irq);
}

static void pfm_ppc32_write_pmc(unsigned int cnum, u64 value)
{
	switch (pfm_pmu_conf->pmc_desc[cnum].hw_addr) {
	case SPRN_MMCR0:
		mtspr(SPRN_MMCR0, value);
		break;
	case SPRN_MMCR1:
		mtspr(SPRN_MMCR1, value);
		break;
	case SPRN_MMCR2:
		mtspr(SPRN_MMCR2, value);
		break;
	default:
		BUG();
	}
}

static void pfm_ppc32_write_pmd(unsigned int cnum, u64 value)
{
	switch (pfm_pmu_conf->pmd_desc[cnum].hw_addr) {
	case SPRN_PMC1:
		mtspr(SPRN_PMC1, value);
		break;
	case SPRN_PMC2:
		mtspr(SPRN_PMC2, value);
		break;
	case SPRN_PMC3:
		mtspr(SPRN_PMC3, value);
		break;
	case SPRN_PMC4:
		mtspr(SPRN_PMC4, value);
		break;
	case SPRN_PMC5:
		mtspr(SPRN_PMC5, value);
		break;
	case SPRN_PMC6:
		mtspr(SPRN_PMC6, value);
		break;
	default:
		BUG();
	}
}

static u64 pfm_ppc32_read_pmd(unsigned int cnum)
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
	default:
		BUG();
	}
}

/**
 * pfm_ppc32_enable_counters
 *
 * Just need to load the current values into the control registers.
 **/
static void pfm_ppc32_enable_counters(struct pfm_context *ctx,
				      struct pfm_event_set *set)
{
	unsigned int i, max_pmc;

	max_pmc = pfm_pmu_conf->regs.max_pmc;

	for (i = 0; i < max_pmc; i++)
		if (test_bit(i, set->used_pmcs))
			pfm_ppc32_write_pmc(i, set->pmcs[i]);
}

/**
 * pfm_ppc32_disable_counters
 *
 * Just need to zero all the control registers.
 **/
static void pfm_ppc32_disable_counters(struct pfm_context *ctx,
				       struct pfm_event_set *set)
{
	unsigned int i, max;

	max = pfm_pmu_conf->regs.max_pmc;

	for (i = 0; i < max; i++)
		if (test_bit(i, set->used_pmcs))
			pfm_ppc32_write_pmc(ctx, 0);
}

/**
 * pfm_ppc32_get_ovfl_pmds
 *
 * Determine which counters in this set have overflowed and fill in the
 * set->povfl_pmds mask and set->npend_ovfls count.
 **/
static void pfm_ppc32_get_ovfl_pmds(struct pfm_context *ctx,
				    struct pfm_event_set *set)
{
	unsigned int i;
	unsigned int max_pmd = pfm_pmu_conf->regs.max_cnt_pmd;
	u64 *used_pmds = set->used_pmds;
	u64 *cntr_pmds = pfm_pmu_conf->regs.cnt_pmds;
	u64 width_mask = 1 << pfm_pmu_conf->counter_width;
	u64 new_val, mask[PFM_PMD_BV];

	bitmap_and(cast_ulp(mask), cast_ulp(cntr_pmds),
		   cast_ulp(used_pmds), max_pmd);

	for (i = 0; i < max_pmd; i++) {
		if (test_bit(i, mask)) {
			new_val = pfm_ppc32_read_pmd(i);
			if (new_val & width_mask) {
				set_bit(i, set->povfl_pmds);
				set->npend_ovfls++;
			}
		}
	}
}

struct pfm_arch_pmu_info pfm_ppc32_pmu_info = {
	.pmu_style        = PFM_POWERPC_PMU_NONE,
	.write_pmc        = pfm_ppc32_write_pmc,
	.write_pmd        = pfm_ppc32_write_pmd,
	.read_pmd         = pfm_ppc32_read_pmd,
	.get_ovfl_pmds    = pfm_ppc32_get_ovfl_pmds,
	.enable_counters  = pfm_ppc32_enable_counters,
	.disable_counters = pfm_ppc32_disable_counters,
};

static struct pfm_pmu_config pfm_ppc32_pmu_conf = {
	.counter_width = 31,
	.pmd_desc = pfm_ppc32_pmd_desc,
	.pmc_desc = pfm_ppc32_pmc_desc,
	.probe_pmu  = pfm_ppc32_probe_pmu,
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.version = "0.1",
	.arch_info = &pfm_ppc32_pmu_info,
};

static int __init pfm_ppc32_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_ppc32_pmu_conf);
}

static void __exit pfm_ppc32_pmu_cleanup_module(void)
{
	release_pmc_hardware();
	pfm_pmu_unregister(&pfm_ppc32_pmu_conf);
}

module_init(pfm_ppc32_pmu_init_module);
module_exit(pfm_ppc32_pmu_cleanup_module);
