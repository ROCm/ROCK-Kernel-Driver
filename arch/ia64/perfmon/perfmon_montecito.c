/*
 * This file contains the McKinley PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
 * Contributed Stephane Eranian <eranian@hpl.hp.com>
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
#include <linux/smp.h>
#include <linux/perfmon_kern.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("Dual-Core Itanium 2 (Montecito) PMU description table");
MODULE_LICENSE("GPL");

#define RDEP(x)	(1UL << (x))

#define PFM_MONT_MASK_PMCS (RDEP(4)|RDEP(5)|RDEP(6)|RDEP(7)|\
			    RDEP(8)|RDEP(9)|RDEP(10)|RDEP(11)|\
			    RDEP(12)|RDEP(13)|RDEP(14)|RDEP(15)|\
			    RDEP(37)|RDEP(39)|RDEP(40)|RDEP(42))

#define PFM_MONT_NO64	(1UL<<5)

static struct pfm_arch_pmu_info pfm_mont_pmu_info = {
	.mask_pmcs = {PFM_MONT_MASK_PMCS,},
};

#define PFM_MONT_RSVD 0xffffffff838000a0UL
/*
 *
 * For debug registers, writing xBR(y) means we use also xBR(y+1). Hence using
 * PMC256+y means we use PMC256+y+1.  Yet, we do not have dependency information
 * but this is fine because they are handled separately in the IA-64 specific
 * code.
 *
 * For PMC4-PMC15, PMC40: we force pmc.ism=2 (IA-64 mode only)
 */
static struct pfm_regmap_desc pfm_mont_pmc_desc[] = {
/* pmc0  */ PMX_NA,
/* pmc1  */ PMX_NA,
/* pmc2  */ PMX_NA,
/* pmc3  */ PMX_NA,
/* pmc4  */ PMC_D(PFM_REG_W64, "PMC4" , 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 4),
/* pmc5  */ PMC_D(PFM_REG_W64, "PMC5" , 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 5),
/* pmc6  */ PMC_D(PFM_REG_W64, "PMC6" , 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 6),
/* pmc7  */ PMC_D(PFM_REG_W64, "PMC7" , 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 7),
/* pmc8  */ PMC_D(PFM_REG_W64, "PMC8" , 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 8),
/* pmc9  */ PMC_D(PFM_REG_W64, "PMC9" , 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 9),
/* pmc10 */ PMC_D(PFM_REG_W64, "PMC10", 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 10),
/* pmc11 */ PMC_D(PFM_REG_W64, "PMC11", 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 11),
/* pmc12 */ PMC_D(PFM_REG_W64, "PMC12", 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 12),
/* pmc13 */ PMC_D(PFM_REG_W64, "PMC13", 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 13),
/* pmc14 */ PMC_D(PFM_REG_W64, "PMC14", 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 14),
/* pmc15 */ PMC_D(PFM_REG_W64, "PMC15", 0x2000020UL, PFM_MONT_RSVD, PFM_MONT_NO64, 15),
/* pmc16 */ PMX_NA,
/* pmc17 */ PMX_NA,
/* pmc18 */ PMX_NA,
/* pmc19 */ PMX_NA,
/* pmc20 */ PMX_NA,
/* pmc21 */ PMX_NA,
/* pmc22 */ PMX_NA,
/* pmc23 */ PMX_NA,
/* pmc24 */ PMX_NA,
/* pmc25 */ PMX_NA,
/* pmc26 */ PMX_NA,
/* pmc27 */ PMX_NA,
/* pmc28 */ PMX_NA,
/* pmc29 */ PMX_NA,
/* pmc30 */ PMX_NA,
/* pmc31 */ PMX_NA,
/* pmc32 */ PMC_D(PFM_REG_W , "PMC32", 0x30f01ffffffffffUL, 0xfcf0fe0000000000UL, 0, 32),
/* pmc33 */ PMC_D(PFM_REG_W , "PMC33", 0x0, 0xfffffe0000000000UL, 0, 33),
/* pmc34 */ PMC_D(PFM_REG_W , "PMC34", 0xf01ffffffffffUL, 0xfff0fe0000000000UL, 0, 34),
/* pmc35 */ PMC_D(PFM_REG_W , "PMC35", 0x0,  0x1ffffffffffUL, 0, 35),
/* pmc36 */ PMC_D(PFM_REG_W , "PMC36", 0xfffffff0UL, 0xfffffffffffffff0UL, 0, 36),
/* pmc37 */ PMC_D(PFM_REG_W , "PMC37", 0x0, 0xffffffffffffc000UL, 0, 37),
/* pmc38 */ PMC_D(PFM_REG_W , "PMC38", 0xdb6UL, 0xffffffffffffdb6dUL, 0, 38),
/* pmc39 */ PMC_D(PFM_REG_W , "PMC39", 0x0, 0xffffffffffff0030UL, 0, 39),
/* pmc40 */ PMC_D(PFM_REG_W , "PMC40", 0x2000000UL, 0xfffffffffff0fe30UL, 0, 40),
/* pmc41 */ PMC_D(PFM_REG_W , "PMC41", 0x00002078fefefefeUL, 0xfffe1fffe7e7e7e7UL, 0, 41),
/* pmc42 */ PMC_D(PFM_REG_W , "PMC42", 0x0, 0xfff800b0UL, 0, 42),
/* pmc43 */  PMX_NA, PMX_NA, PMX_NA, PMX_NA, PMX_NA,
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
/* pmc256 */ PMC_D(PFM_REG_W, "IBR0", 0x0, 0, 0, 0),
/* pmc257 */ PMC_D(PFM_REG_W, "IBR1", 0x0, 0x8000000000000000UL, 0, 1),
/* pmc258 */ PMC_D(PFM_REG_W, "IBR2", 0x0, 0, 0, 2),
/* pmc259 */ PMC_D(PFM_REG_W, "IBR3", 0x0, 0x8000000000000000UL, 0, 3),
/* pmc260 */ PMC_D(PFM_REG_W, "IBR4", 0x0, 0, 0, 4),
/* pmc261 */ PMC_D(PFM_REG_W, "IBR5", 0x0, 0x8000000000000000UL, 0, 5),
/* pmc262 */ PMC_D(PFM_REG_W, "IBR6", 0x0, 0, 0, 6),
/* pmc263 */ PMC_D(PFM_REG_W, "IBR7", 0x0, 0x8000000000000000UL, 0, 7),
/* pmc264 */ PMC_D(PFM_REG_W, "DBR0", 0x0, 0, 0, 0),
/* pmc265 */ PMC_D(PFM_REG_W, "DBR1", 0x0, 0xc000000000000000UL, 0, 1),
/* pmc266 */ PMC_D(PFM_REG_W, "DBR2", 0x0, 0, 0, 2),
/* pmc267 */ PMC_D(PFM_REG_W, "DBR3", 0x0, 0xc000000000000000UL, 0, 3),
/* pmc268 */ PMC_D(PFM_REG_W, "DBR4", 0x0, 0, 0, 4),
/* pmc269 */ PMC_D(PFM_REG_W, "DBR5", 0x0, 0xc000000000000000UL, 0, 5),
/* pmc270 */ PMC_D(PFM_REG_W, "DBR6", 0x0, 0, 0, 6),
/* pmc271 */ PMC_D(PFM_REG_W, "DBR7", 0x0, 0xc000000000000000UL, 0, 7)
};
#define PFM_MONT_NUM_PMCS ARRAY_SIZE(pfm_mont_pmc_desc)

static struct pfm_regmap_desc pfm_mont_pmd_desc[] = {
/* pmd0  */ PMX_NA,
/* pmd1  */ PMX_NA,
/* pmd2  */ PMX_NA,
/* pmd3  */ PMX_NA,
/* pmd4  */ PMD_DP(PFM_REG_C, "PMD4", 4, 1ull << 4),
/* pmd5  */ PMD_DP(PFM_REG_C, "PMD5", 5, 1ull << 5),
/* pmd6  */ PMD_DP(PFM_REG_C, "PMD6", 6, 1ull << 6),
/* pmd7  */ PMD_DP(PFM_REG_C, "PMD7", 7, 1ull << 7),
/* pmd8  */ PMD_DP(PFM_REG_C, "PMD8", 8, 1ull << 8),
/* pmd9  */ PMD_DP(PFM_REG_C, "PMD9", 9, 1ull << 9),
/* pmd10 */ PMD_DP(PFM_REG_C, "PMD10", 10, 1ull << 10),
/* pmd11 */ PMD_DP(PFM_REG_C, "PMD11", 11, 1ull << 11),
/* pmd12 */ PMD_DP(PFM_REG_C, "PMD12", 12, 1ull << 12),
/* pmd13 */ PMD_DP(PFM_REG_C, "PMD13", 13, 1ull << 13),
/* pmd14 */ PMD_DP(PFM_REG_C, "PMD14", 14, 1ull << 14),
/* pmd15 */ PMD_DP(PFM_REG_C, "PMD15", 15, 1ull << 15),
/* pmd16 */ PMX_NA,
/* pmd17 */ PMX_NA,
/* pmd18 */ PMX_NA,
/* pmd19 */ PMX_NA,
/* pmd20 */ PMX_NA,
/* pmd21 */ PMX_NA,
/* pmd22 */ PMX_NA,
/* pmd23 */ PMX_NA,
/* pmd24 */ PMX_NA,
/* pmd25 */ PMX_NA,
/* pmd26 */ PMX_NA,
/* pmd27 */ PMX_NA,
/* pmd28 */ PMX_NA,
/* pmd29 */ PMX_NA,
/* pmd30 */ PMX_NA,
/* pmd31 */ PMX_NA,
/* pmd32 */ PMD_DP(PFM_REG_I, "PMD32", 32, 1ull << 40),
/* pmd33 */ PMD_DP(PFM_REG_I, "PMD33", 33, 1ull << 40),
/* pmd34 */ PMD_DP(PFM_REG_I, "PMD34", 34, 1ull << 37),
/* pmd35 */ PMD_DP(PFM_REG_I, "PMD35", 35, 1ull << 37),
/* pmd36 */ PMD_DP(PFM_REG_I, "PMD36", 36, 1ull << 40),
/* pmd37 */ PMX_NA,
/* pmd38 */ PMD_DP(PFM_REG_I, "PMD38", 38, (1ull<<39)|(1ull<<42)),
/* pmd39 */ PMD_DP(PFM_REG_I, "PMD39", 39, (1ull<<39)|(1ull<<42)),
/* pmd40 */ PMX_NA,
/* pmd41 */ PMX_NA,
/* pmd42 */ PMX_NA,
/* pmd43 */ PMX_NA,
/* pmd44 */ PMX_NA,
/* pmd45 */ PMX_NA,
/* pmd46 */ PMX_NA,
/* pmd47 */ PMX_NA,
/* pmd48 */ PMD_DP(PFM_REG_I, "PMD48", 48, (1ull<<39)|(1ull<<42)),
/* pmd49 */ PMD_DP(PFM_REG_I, "PMD49", 49, (1ull<<39)|(1ull<<42)),
/* pmd50 */ PMD_DP(PFM_REG_I, "PMD50", 50, (1ull<<39)|(1ull<<42)),
/* pmd51 */ PMD_DP(PFM_REG_I, "PMD51", 51, (1ull<<39)|(1ull<<42)),
/* pmd52 */ PMD_DP(PFM_REG_I, "PMD52", 52, (1ull<<39)|(1ull<<42)),
/* pmd53 */ PMD_DP(PFM_REG_I, "PMD53", 53, (1ull<<39)|(1ull<<42)),
/* pmd54 */ PMD_DP(PFM_REG_I, "PMD54", 54, (1ull<<39)|(1ull<<42)),
/* pmd55 */ PMD_DP(PFM_REG_I, "PMD55", 55, (1ull<<39)|(1ull<<42)),
/* pmd56 */ PMD_DP(PFM_REG_I, "PMD56", 56, (1ull<<39)|(1ull<<42)),
/* pmd57 */ PMD_DP(PFM_REG_I, "PMD57", 57, (1ull<<39)|(1ull<<42)),
/* pmd58 */ PMD_DP(PFM_REG_I, "PMD58", 58, (1ull<<39)|(1ull<<42)),
/* pmd59 */ PMD_DP(PFM_REG_I, "PMD59", 59, (1ull<<39)|(1ull<<42)),
/* pmd60 */ PMD_DP(PFM_REG_I, "PMD60", 60, (1ull<<39)|(1ull<<42)),
/* pmd61 */ PMD_DP(PFM_REG_I, "PMD61", 61, (1ull<<39)|(1ull<<42)),
/* pmd62 */ PMD_DP(PFM_REG_I, "PMD62", 62, (1ull<<39)|(1ull<<42)),
/* pmd63 */ PMD_DP(PFM_REG_I, "PMD63", 63, (1ull<<39)|(1ull<<42))
};
#define PFM_MONT_NUM_PMDS ARRAY_SIZE(pfm_mont_pmd_desc)

static int pfm_mont_has_ht;

static int pfm_mont_pmc_check(struct pfm_context *ctx,
			      struct pfm_event_set *set,
			      struct pfarg_pmc *req)
{
	struct pfm_arch_context *ctx_arch;
	u64 val32 = 0, val38 = 0, val41 = 0;
	u64 tmpval;
	u16 cnum;
	int ret = 0, check_case1 = 0;
	int is_system;

	tmpval = req->reg_value;
	cnum = req->reg_num;
	ctx_arch = pfm_ctx_arch(ctx);
	is_system = ctx->flags.system;

#define PFM_MONT_PMC_PM_POS6	(1UL<<6)
#define PFM_MONT_PMC_PM_POS4	(1UL<<4)

	switch (cnum) {
	case  4:
	case  5:
	case  6:
	case  7:
	case  8:
	case  9:
		if (is_system)
			tmpval |= PFM_MONT_PMC_PM_POS6;
		else
			tmpval &= ~PFM_MONT_PMC_PM_POS6;
		break;
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
		if ((req->reg_flags & PFM_REGFL_NO_EMUL64) == 0) {
			if (pfm_mont_has_ht) {
				PFM_INFO("perfmon: Errata 121 PMD10/PMD15 cannot be used to overflow"
					 "when threads on on");
				return -EINVAL;
			}
		}
		if (is_system)
			tmpval |= PFM_MONT_PMC_PM_POS6;
		else
			tmpval &= ~PFM_MONT_PMC_PM_POS6;
		break;
	case 39:
	case 40:
	case 42:
		if (pfm_mont_has_ht && ((req->reg_value >> 8) & 0x7) == 4) {
			PFM_INFO("perfmon: Errata 120: IP-EAR not available when threads are on");
			return -EINVAL;
		}
		if (is_system)
			tmpval |= PFM_MONT_PMC_PM_POS6;
		else
			tmpval &= ~PFM_MONT_PMC_PM_POS6;
		break;

	case  32:
		val32 = tmpval;
		val38 = set->pmcs[38];
		val41 = set->pmcs[41];
		check_case1 = 1;
		break;

	case  37:
		if (is_system)
			tmpval |= PFM_MONT_PMC_PM_POS4;
		else
			tmpval &= ~PFM_MONT_PMC_PM_POS4;
		break;

	case  38:
		val38 = tmpval;
		val32 = set->pmcs[32];
		val41 = set->pmcs[41];
		check_case1 = 1;
		break;
	case  41:
		val41 = tmpval;
		val32 = set->pmcs[32];
		val38 = set->pmcs[38];
		check_case1 = 1;
		break;
	}

	if (check_case1) {
		ret = (((val41 >> 45) & 0xf) == 0 && ((val32>>57) & 0x1) == 0)
		     && ((((val38>>1) & 0x3) == 0x2 || ((val38>>1) & 0x3) == 0)
		     || (((val38>>4) & 0x3) == 0x2 || ((val38>>4) & 0x3) == 0));
		if (ret) {
			PFM_DBG("perfmon: invalid config pmc38=0x%lx "
				"pmc41=0x%lx pmc32=0x%lx",
				val38, val41, val32);
			return -EINVAL;
		}
	}

	/*
	 * check if configuration implicitely activates the use of the
	 * debug registers. If true, then we ensure that this is possible
	 * and that we do not pick up stale value in the HW registers.
	 */

	/*
	 *
	 * pmc41 is "active" if:
	 * 	one of the pmc41.cfgdtagXX field is different from 0x3
	 * AND
	 * 	the corsesponding pmc41.en_dbrpXX is set.
	 * AND
	 *	ctx_fl_use_dbr (dbr not yet used)
	 */
	if (cnum == 41
	    && (tmpval & 0x1e00000000000)
		&& (tmpval & 0x18181818) != 0x18181818
		&& ctx_arch->flags.use_dbr == 0) {
		PFM_DBG("pmc41=0x%lx active, clearing dbr", tmpval);
		ret = pfm_ia64_mark_dbregs_used(ctx, set);
		if (ret)
			return ret;
	}
	/*
	 * we must clear the (instruction) debug registers if:
	 * 	pmc38.ig_ibrpX is 0 (enabled)
	 * and
	 * 	fl_use_dbr == 0 (dbr not yet used)
	 */
	if (cnum == 38 && ((tmpval & 0x492) != 0x492)
		&& ctx_arch->flags.use_dbr == 0) {
		PFM_DBG("pmc38=0x%lx active pmc38, clearing ibr", tmpval);
		ret = pfm_ia64_mark_dbregs_used(ctx, set);
		if (ret)
			return ret;

	}
	req->reg_value = tmpval;
	return 0;
}

static void pfm_handle_errata(void)
{
	pfm_mont_has_ht = 1;

	PFM_INFO("activating workaround for errata 120 "
		 "(Disable IP-EAR when threads are on)");

	PFM_INFO("activating workaround for Errata 121 "
		 "(PMC10-PMC15 cannot be used to overflow"
		 " when threads are on");
}
static int pfm_mont_probe_pmu(void)
{
	if (local_cpu_data->family != 0x20)
		return -1;

	/*
	 * the 2 errata must be activated when
	 * threads are/can be enabled
	 */
	if (is_multithreading_enabled())
		pfm_handle_errata();

	return 0;
}

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static struct pfm_pmu_config pfm_mont_pmu_conf = {
	.pmu_name = "Montecito",
	.counter_width = 47,
	.pmd_desc = pfm_mont_pmd_desc,
	.pmc_desc = pfm_mont_pmc_desc,
	.num_pmc_entries = PFM_MONT_NUM_PMCS,
	.num_pmd_entries = PFM_MONT_NUM_PMDS,
	.pmc_write_check = pfm_mont_pmc_check,
	.probe_pmu = pfm_mont_probe_pmu,
	.version = "1.0",
	.pmu_info = &pfm_mont_pmu_info,
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE
};

static int __init pfm_mont_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_mont_pmu_conf);
}

static void __exit pfm_mont_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_mont_pmu_conf);
}

module_init(pfm_mont_pmu_init_module);
module_exit(pfm_mont_pmu_cleanup_module);
