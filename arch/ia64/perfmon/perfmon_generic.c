/*
 * This file contains the generic PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (c) 2002-2006 Hewlett-Packard Development Company, L.P.
 * contributed by Stephane Eranian <eranian@hpl.hp.com>
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
#include <asm/pal.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("Generic IA-64 PMU description tables");
MODULE_LICENSE("GPL");

#define RDEP(x)	(1UL << (x))

#define PFM_IA64GEN_MASK_PMCS	(RDEP(4)|RDEP(5)|RDEP(6)|RDEP(7))
#define PFM_IA64GEN_RSVD	(0xffffffffffff0080UL)
#define PFM_IA64GEN_NO64	(1UL<<5)

/* forward declaration */
static struct pfm_pmu_config pfm_ia64gen_pmu_conf;

static struct pfm_arch_pmu_info pfm_ia64gen_pmu_info = {
	.mask_pmcs = {PFM_IA64GEN_MASK_PMCS,},
};

static struct pfm_regmap_desc pfm_ia64gen_pmc_desc[] = {
/* pmc0  */ PMX_NA,
/* pmc1  */ PMX_NA,
/* pmc2  */ PMX_NA,
/* pmc3  */ PMX_NA,
/* pmc4  */ PMC_D(PFM_REG_W64, "PMC4", 0x0, PFM_IA64GEN_RSVD, PFM_IA64GEN_NO64, 4),
/* pmc5  */ PMC_D(PFM_REG_W64, "PMC5", 0x0, PFM_IA64GEN_RSVD, PFM_IA64GEN_NO64, 5),
/* pmc6  */ PMC_D(PFM_REG_W64, "PMC6", 0x0, PFM_IA64GEN_RSVD, PFM_IA64GEN_NO64, 6),
/* pmc7  */ PMC_D(PFM_REG_W64, "PMC7", 0x0, PFM_IA64GEN_RSVD, PFM_IA64GEN_NO64, 7)
};
#define PFM_IA64GEN_NUM_PMCS ARRAY_SIZE(pfm_ia64gen_pmc_desc)

static struct pfm_regmap_desc pfm_ia64gen_pmd_desc[] = {
/* pmd0  */ PMX_NA,
/* pmd1  */ PMX_NA,
/* pmd2  */ PMX_NA,
/* pmd3  */ PMX_NA,
/* pmd4  */ PMD_DP(PFM_REG_C, "PMD4", 4, 1ull << 4),
/* pmd5  */ PMD_DP(PFM_REG_C, "PMD5", 5, 1ull << 5),
/* pmd6  */ PMD_DP(PFM_REG_C, "PMD6", 6, 1ull << 6),
/* pmd7  */ PMD_DP(PFM_REG_C, "PMD7", 7, 1ull << 7)
};
#define PFM_IA64GEN_NUM_PMDS ARRAY_SIZE(pfm_ia64gen_pmd_desc)

static int pfm_ia64gen_pmc_check(struct pfm_context *ctx,
				 struct pfm_event_set *set,
				 struct pfarg_pmc *req)
{
#define PFM_IA64GEN_PMC_PM_POS6	(1UL<<6)
	u64 tmpval;
	int is_system;

	is_system = ctx->flags.system;
	tmpval = req->reg_value;

	switch (req->reg_num) {
	case  4:
	case  5:
	case  6:
	case  7:
		/* set pmc.oi for 64-bit emulation */
		tmpval |= 1UL << 5;

		if (is_system)
			tmpval |= PFM_IA64GEN_PMC_PM_POS6;
		else
			tmpval &= ~PFM_IA64GEN_PMC_PM_POS6;
		break;

	}
	req->reg_value = tmpval;

	return 0;
}

/*
 * matches anything
 */
static int pfm_ia64gen_probe_pmu(void)
{
	u64 pm_buffer[16];
	pal_perf_mon_info_u_t pm_info;

	/*
	 * call PAL_PERFMON_INFO to retrieve counter width which
	 * is implementation specific
	 */
	if (ia64_pal_perf_mon_info(pm_buffer, &pm_info))
		return -1;

	pfm_ia64gen_pmu_conf.counter_width = pm_info.pal_perf_mon_info_s.width;

	return 0;
}

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static struct pfm_pmu_config pfm_ia64gen_pmu_conf = {
	.pmu_name = "Generic IA-64",
	.counter_width = 0, /* computed from PAL_PERFMON_INFO */
	.pmd_desc = pfm_ia64gen_pmd_desc,
	.pmc_desc = pfm_ia64gen_pmc_desc,
	.probe_pmu = pfm_ia64gen_probe_pmu,
	.num_pmc_entries = PFM_IA64GEN_NUM_PMCS,
	.num_pmd_entries = PFM_IA64GEN_NUM_PMDS,
	.pmc_write_check = pfm_ia64gen_pmc_check,
	.version = "1.0",
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmu_info = &pfm_ia64gen_pmu_info
	/* no read/write checkers */
};

static int __init pfm_gen_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_ia64gen_pmu_conf);
}

static void __exit pfm_gen_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_ia64gen_pmu_conf);
}

module_init(pfm_gen_pmu_init_module);
module_exit(pfm_gen_pmu_cleanup_module);
