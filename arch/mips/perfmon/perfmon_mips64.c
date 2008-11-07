/*
 * This file contains the MIPS64 and decendent PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (c) 2005 Philip Mucci
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

MODULE_AUTHOR("Philip Mucci <mucci@cs.utk.edu>");
MODULE_DESCRIPTION("MIPS64 PMU description tables");
MODULE_LICENSE("GPL");

/*
 * reserved:
 * 	- bit 63-9
 * RSVD: reserved bits must be 1
 */
#define PFM_MIPS64_PMC_RSVD 0xfffffffffffff810ULL
#define PFM_MIPS64_PMC_VAL  (1ULL<<4)

extern int null_perf_irq(struct pt_regs *regs);
extern int (*perf_irq)(struct pt_regs *regs);
extern int perfmon_perf_irq(struct pt_regs *regs);

static struct pfm_arch_pmu_info pfm_mips64_pmu_info;

static struct pfm_regmap_desc pfm_mips64_pmc_desc[] = {
/* pmc0 */  PMC_D(PFM_REG_I64, "CP0_25_0", PFM_MIPS64_PMC_VAL, PFM_MIPS64_PMC_RSVD, 0, 0),
/* pmc1 */  PMC_D(PFM_REG_I64, "CP0_25_1", PFM_MIPS64_PMC_VAL, PFM_MIPS64_PMC_RSVD, 0, 1),
/* pmc2 */  PMC_D(PFM_REG_I64, "CP0_25_2", PFM_MIPS64_PMC_VAL, PFM_MIPS64_PMC_RSVD, 0, 2),
/* pmc3 */  PMC_D(PFM_REG_I64, "CP0_25_3", PFM_MIPS64_PMC_VAL, PFM_MIPS64_PMC_RSVD, 0, 3)
};
#define PFM_MIPS64_NUM_PMCS ARRAY_SIZE(pfm_mips64_pmc_desc)

static struct pfm_regmap_desc pfm_mips64_pmd_desc[] = {
/* pmd0 */ PMD_D(PFM_REG_C, "CP0_25_0", 0),
/* pmd1 */ PMD_D(PFM_REG_C, "CP0_25_1", 1),
/* pmd2 */ PMD_D(PFM_REG_C, "CP0_25_2", 2),
/* pmd3 */ PMD_D(PFM_REG_C, "CP0_25_3", 3)
};
#define PFM_MIPS64_NUM_PMDS ARRAY_SIZE(pfm_mips64_pmd_desc)

static int pfm_mips64_probe_pmu(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	switch (c->cputype) {
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
		return 0;
		break;
	default:
		PFM_INFO("Unknown cputype 0x%x", c->cputype);
	}
	return -1;
}

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static struct pfm_pmu_config pfm_mips64_pmu_conf = {
	.pmu_name = "MIPS", /* placeholder */
	.counter_width = 31,
	.pmd_desc = pfm_mips64_pmd_desc,
	.pmc_desc = pfm_mips64_pmc_desc,
	.num_pmc_entries = PFM_MIPS64_NUM_PMCS,
	.num_pmd_entries = PFM_MIPS64_NUM_PMDS,
	.probe_pmu = pfm_mips64_probe_pmu,
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmu_info = &pfm_mips64_pmu_info
};

static inline int n_counters(void)
{
	if (!(read_c0_config1() & MIPS64_CONFIG_PMC_MASK))
		return 0;
	if (!(read_c0_perfctrl0() & MIPS64_PMC_CTR_MASK))
		return 1;
	if (!(read_c0_perfctrl1() & MIPS64_PMC_CTR_MASK))
		return 2;
	if (!(read_c0_perfctrl2() & MIPS64_PMC_CTR_MASK))
		return 3;
	return 4;
}

static int __init pfm_mips64_pmu_init_module(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	int i, ret, num;
	u64 temp_mask;

	switch (c->cputype) {
	case CPU_5KC:
		pfm_mips64_pmu_conf.pmu_name = "MIPS5KC";
		break;
	case CPU_R12000:
		pfm_mips64_pmu_conf.pmu_name = "MIPSR12000";
		break;
	case CPU_20KC:
		pfm_mips64_pmu_conf.pmu_name = "MIPS20KC";
		break;
	case CPU_24K:
		pfm_mips64_pmu_conf.pmu_name = "MIPS24K";
		break;
	case CPU_25KF:
		pfm_mips64_pmu_conf.pmu_name = "MIPS25KF";
		break;
	case CPU_SB1:
		pfm_mips64_pmu_conf.pmu_name = "SB1";
		break;
	case CPU_SB1A:
	pfm_mips64_pmu_conf.pmu_name = "SB1A";
		break;
#ifndef CONFIG_SMP
	case CPU_34K:
		pfm_mips64_pmu_conf.pmu_name = "MIPS34K";
		break;
#if defined(CPU_74K)
	case CPU_74K:
		pfm_mips64_pmu_conf.pmu_name = "MIPS74K";
		break;
#endif
#endif
	default:
		PFM_INFO("Unknown cputype 0x%x", c->cputype);
		return -1;
	}

	/* The R14k and older performance counters have to          */
	/* be hard-coded, as there is no support for auto-detection */
	if ((c->cputype == CPU_R12000) || (c->cputype == CPU_R14000))
		num = 4;
	else if (c->cputype == CPU_R10000)
		num = 2;
	else
		num = n_counters();

	if (num == 0) {
		PFM_INFO("cputype 0x%x has no counters", c->cputype);
		return -1;
	}
	/* mark remaining counters unavailable */
	for (i = num; i < PFM_MIPS64_NUM_PMCS; i++)
		pfm_mips64_pmc_desc[i].type = PFM_REG_NA;

	for (i = num; i < PFM_MIPS64_NUM_PMDS; i++)
		pfm_mips64_pmd_desc[i].type = PFM_REG_NA;

	/* set the PMC_RSVD mask */
	switch (c->cputype) {
	case CPU_5KC:
	case CPU_R10000:
	case CPU_20KC:
	   /* 4-bits for event */
	   temp_mask = 0xfffffffffffffe10ULL;
	   break;
	case CPU_R12000:
	case CPU_R14000:
	   /* 5-bits for event */
	   temp_mask = 0xfffffffffffffc10ULL;
	   break;
	default:
	   /* 6-bits for event */
	   temp_mask = 0xfffffffffffff810ULL;
	}
	for (i = 0; i < PFM_MIPS64_NUM_PMCS; i++)
		pfm_mips64_pmc_desc[i].rsvd_msk = temp_mask;

	pfm_mips64_pmu_conf.num_pmc_entries = num;
	pfm_mips64_pmu_conf.num_pmd_entries = num;

	pfm_mips64_pmu_info.pmu_style = c->cputype;

	ret = pfm_pmu_register(&pfm_mips64_pmu_conf);
	if (ret == 0)
		perf_irq = perfmon_perf_irq;
	return ret;
}

static void __exit pfm_mips64_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_mips64_pmu_conf);
	perf_irq = null_perf_irq;
}

module_init(pfm_mips64_pmu_init_module);
module_exit(pfm_mips64_pmu_cleanup_module);
