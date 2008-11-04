/*
 * This file contains the McKinley PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (c) 2002-2006 Hewlett-Packard Development Company, L.P.
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

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("Itanium 2 (McKinley) PMU description tables");
MODULE_LICENSE("GPL");

#define RDEP(x)	(1UL << (x))

#define PFM_MCK_MASK_PMCS (RDEP(4)|RDEP(5)|RDEP(6)|RDEP(7)|RDEP(10)|RDEP(11)|\
			   RDEP(12))

#define PFM_MCK_NO64	(1UL<<5)

static struct pfm_arch_pmu_info pfm_mck_pmu_info = {
	.mask_pmcs = {PFM_MCK_MASK_PMCS,},
};

/* reserved bits are 1 in the mask */
#define PFM_ITA2_RSVD 0xfffffffffc8000a0UL

/*
 * For debug registers, writing xBR(y) means we use also xBR(y+1). Hence using
 * PMC256+y means we use PMC256+y+1.  Yet, we do not have dependency information
 * but this is fine because they are handled separately in the IA-64 specific
 * code.
 */
static struct pfm_regmap_desc pfm_mck_pmc_desc[] = {
/* pmc0  */ PMX_NA,
/* pmc1  */ PMX_NA,
/* pmc2  */ PMX_NA,
/* pmc3  */ PMX_NA,
/* pmc4  */ PMC_D(PFM_REG_W64, "PMC4" , 0x800020UL, 0xfffffffffc8000a0, PFM_MCK_NO64, 4),
/* pmc5  */ PMC_D(PFM_REG_W64, "PMC5" , 0x20UL, PFM_ITA2_RSVD, PFM_MCK_NO64, 5),
/* pmc6  */ PMC_D(PFM_REG_W64, "PMC6" , 0x20UL, PFM_ITA2_RSVD, PFM_MCK_NO64, 6),
/* pmc7  */ PMC_D(PFM_REG_W64, "PMC7" , 0x20UL, PFM_ITA2_RSVD, PFM_MCK_NO64, 7),
/* pmc8  */ PMC_D(PFM_REG_W  , "PMC8" , 0xffffffff3fffffffUL, 0xc0000004UL, 0, 8),
/* pmc9  */ PMC_D(PFM_REG_W  , "PMC9" , 0xffffffff3ffffffcUL, 0xc0000004UL, 0, 9),
/* pmc10 */ PMC_D(PFM_REG_W  , "PMC10", 0x0, 0xffffffffffff0000UL, 0, 10),
/* pmc11 */ PMC_D(PFM_REG_W  , "PMC11", 0x0, 0xfffffffffcf0fe30UL, 0, 11),
/* pmc12 */ PMC_D(PFM_REG_W  , "PMC12", 0x0, 0xffffffffffff0000UL, 0, 12),
/* pmc13 */ PMC_D(PFM_REG_W  , "PMC13", 0x2078fefefefeUL, 0xfffe1fffe7e7e7e7UL, 0, 13),
/* pmc14 */ PMC_D(PFM_REG_W  , "PMC14", 0x0db60db60db60db6UL, 0xffffffffffffdb6dUL, 0, 14),
/* pmc15 */ PMC_D(PFM_REG_W  , "PMC15", 0xfffffff0UL, 0xfffffffffffffff0UL, 0, 15),
/* pmc16 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc24 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc32 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc40 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc48 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc56 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc64 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc72 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc80 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc88 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc96 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc104 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc112 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc120 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc128 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc136 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc144 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc152 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc160 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc168 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc176 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc184 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc192 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc200 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc208 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc216 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc224 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc232 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc240 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc248 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc256 */ PMC_D(PFM_REG_W  , "IBR0", 0x0, 0, 0, 0),
/* pmc257 */ PMC_D(PFM_REG_W  , "IBR1", 0x0, 0x8000000000000000UL, 0, 1),
/* pmc258 */ PMC_D(PFM_REG_W  , "IBR2", 0x0, 0, 0, 2),
/* pmc259 */ PMC_D(PFM_REG_W  , "IBR3", 0x0, 0x8000000000000000UL, 0, 3),
/* pmc260 */ PMC_D(PFM_REG_W  , "IBR4", 0x0, 0, 0, 4),
/* pmc261 */ PMC_D(PFM_REG_W  , "IBR5", 0x0, 0x8000000000000000UL, 0, 5),
/* pmc262 */ PMC_D(PFM_REG_W  , "IBR6", 0x0, 0, 0, 6),
/* pmc263 */ PMC_D(PFM_REG_W  , "IBR7", 0x0, 0x8000000000000000UL, 0, 7),
/* pmc264 */ PMC_D(PFM_REG_W  , "DBR0", 0x0, 0, 0, 0),
/* pmc265 */ PMC_D(PFM_REG_W  , "DBR1", 0x0, 0xc000000000000000UL, 0, 1),
/* pmc266 */ PMC_D(PFM_REG_W  , "DBR2", 0x0, 0, 0, 2),
/* pmc267 */ PMC_D(PFM_REG_W  , "DBR3", 0x0, 0xc000000000000000UL, 0, 3),
/* pmc268 */ PMC_D(PFM_REG_W  , "DBR4", 0x0, 0, 0, 4),
/* pmc269 */ PMC_D(PFM_REG_W  , "DBR5", 0x0, 0xc000000000000000UL, 0, 5),
/* pmc270 */ PMC_D(PFM_REG_W  , "DBR6", 0x0, 0, 0, 6),
/* pmc271 */ PMC_D(PFM_REG_W  , "DBR7", 0x0, 0xc000000000000000UL, 0, 7)
};
#define PFM_MCK_NUM_PMCS ARRAY_SIZE(pfm_mck_pmc_desc)

static struct pfm_regmap_desc pfm_mck_pmd_desc[] = {
/* pmd0  */ PMD_DP(PFM_REG_I, "PMD0", 0, 1ull << 10),
/* pmd1  */ PMD_DP(PFM_REG_I, "PMD1", 1, 1ull << 10),
/* pmd2  */ PMD_DP(PFM_REG_I, "PMD2", 2, 1ull << 11),
/* pmd3  */ PMD_DP(PFM_REG_I, "PMD3", 3, 1ull << 11),
/* pmd4  */ PMD_DP(PFM_REG_C, "PMD4", 4, 1ull << 4),
/* pmd5  */ PMD_DP(PFM_REG_C, "PMD5", 5, 1ull << 5),
/* pmd6  */ PMD_DP(PFM_REG_C, "PMD6", 6, 1ull << 6),
/* pmd7  */ PMD_DP(PFM_REG_C, "PMD7", 7, 1ull << 7),
/* pmd8  */ PMD_DP(PFM_REG_I, "PMD8", 8, 1ull << 12),
/* pmd9  */ PMD_DP(PFM_REG_I, "PMD9", 9, 1ull << 12),
/* pmd10 */ PMD_DP(PFM_REG_I, "PMD10", 10, 1ull << 12),
/* pmd11 */ PMD_DP(PFM_REG_I, "PMD11", 11, 1ull << 12),
/* pmd12 */ PMD_DP(PFM_REG_I, "PMD12", 12, 1ull << 12),
/* pmd13 */ PMD_DP(PFM_REG_I, "PMD13", 13, 1ull << 12),
/* pmd14 */ PMD_DP(PFM_REG_I, "PMD14", 14, 1ull << 12),
/* pmd15 */ PMD_DP(PFM_REG_I, "PMD15", 15, 1ull << 12),
/* pmd16 */ PMD_DP(PFM_REG_I, "PMD16", 16, 1ull << 12),
/* pmd17 */ PMD_DP(PFM_REG_I, "PMD17", 17, 1ull << 11)
};
#define PFM_MCK_NUM_PMDS ARRAY_SIZE(pfm_mck_pmd_desc)

static int pfm_mck_pmc_check(struct pfm_context *ctx,
			     struct pfm_event_set *set,
			     struct pfarg_pmc *req)
{
	struct pfm_arch_context *ctx_arch;
	u64 val8 = 0, val14 = 0, val13 = 0;
	u64 tmpval;
	u16 cnum;
	int ret = 0, check_case1 = 0;
	int is_system;

	tmpval = req->reg_value;
	cnum = req->reg_num;
	ctx_arch = pfm_ctx_arch(ctx);
	is_system = ctx->flags.system;

#define PFM_MCK_PMC_PM_POS6	(1UL<<6)
#define PFM_MCK_PMC_PM_POS4	(1UL<<4)

	switch (cnum) {
	case  4:
	case  5:
	case  6:
	case  7:
	case 11:
	case 12:
		if (is_system)
			tmpval |= PFM_MCK_PMC_PM_POS6;
		else
			tmpval &= ~PFM_MCK_PMC_PM_POS6;
		break;

	case  8:
		val8 = tmpval;
		val13 = set->pmcs[13];
		val14 = set->pmcs[14];
		check_case1 = 1;
		break;

	case 10:
		if (is_system)
			tmpval |= PFM_MCK_PMC_PM_POS4;
		else
			tmpval &= ~PFM_MCK_PMC_PM_POS4;
		break;

	case 13:
		val8 = set->pmcs[8];
		val13 = tmpval;
		val14 = set->pmcs[14];
		check_case1 = 1;
		break;

	case 14:
		val8 = set->pmcs[8];
		val13 = set->pmcs[13];
		val14 = tmpval;
		check_case1 = 1;
		break;
	}

	/*
	 * check illegal configuration which can produce inconsistencies
	 * in tagging i-side events in L1D and L2 caches
	 */
	if (check_case1) {
		ret = (((val13 >> 45) & 0xf) == 0 && ((val8 & 0x1) == 0))
			&& ((((val14>>1) & 0x3) == 0x2 || ((val14>>1) & 0x3) == 0x0)
			|| (((val14>>4) & 0x3) == 0x2 || ((val14>>4) & 0x3) == 0x0));

		if (ret) {
			PFM_DBG("perfmon: invalid config pmc8=0x%lx "
				"pmc13=0x%lx pmc14=0x%lx",
				val8, val13, val14);
			return -EINVAL;
		}
	}

	/*
	 * check if configuration implicitely activates the use of
	 * the debug registers. If true, then we ensure that this is
	 * possible and that we do not pick up stale value in the HW
	 * registers.
	 *
	 * We postpone the checks of pmc13 and pmc14 to avoid side effects
	 * in case of errors
	 */

	/*
	 * pmc13 is "active" if:
	 * 	one of the pmc13.cfg_dbrpXX field is different from 0x3
	 * AND
	 * 	at the corresponding pmc13.ena_dbrpXX is set.
	 */
	if (cnum == 13 && (tmpval & 0x1e00000000000UL)
	    && (tmpval & 0x18181818UL) != 0x18181818UL
		&& ctx_arch->flags.use_dbr == 0) {
		PFM_DBG("pmc13=0x%lx active", tmpval);
		ret = pfm_ia64_mark_dbregs_used(ctx, set);
		if (ret)
			return ret;
	}

	/*
	 *  if any pmc14.ibrpX bit is enabled we must clear the ibrs
	 */
	if (cnum == 14 && ((tmpval & 0x2222UL) != 0x2222UL)
		&& ctx_arch->flags.use_dbr == 0) {
		PFM_DBG("pmc14=0x%lx active", tmpval);
		ret = pfm_ia64_mark_dbregs_used(ctx, set);
		if (ret)
			return ret;
	}

	req->reg_value = tmpval;

	return 0;
}

static int pfm_mck_probe_pmu(void)
{
	return local_cpu_data->family == 0x1f ? 0 : -1;
}

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static struct pfm_pmu_config pfm_mck_pmu_conf = {
	.pmu_name = "Itanium 2",
	.counter_width = 47,
	.pmd_desc = pfm_mck_pmd_desc,
	.pmc_desc = pfm_mck_pmc_desc,
	.pmc_write_check = pfm_mck_pmc_check,
	.num_pmc_entries = PFM_MCK_NUM_PMCS,
	.num_pmd_entries = PFM_MCK_NUM_PMDS,
	.probe_pmu = pfm_mck_probe_pmu,
	.version = "1.0",
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmu_info = &pfm_mck_pmu_info,
};

static int __init pfm_mck_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_mck_pmu_conf);
}

static void __exit pfm_mck_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_mck_pmu_conf);
}

module_init(pfm_mck_pmu_init_module);
module_exit(pfm_mck_pmu_cleanup_module);
