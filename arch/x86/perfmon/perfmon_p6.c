/*
 * This file contains the P6 family processor PMU register description tables
 *
 * This module supports original P6 processors
 * (Pentium II, Pentium Pro, Pentium III) and Pentium M.
 *
 * Copyright (c) 2005-2007 Hewlett-Packard Development Company, L.P.
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
#include <linux/kprobes.h>
#include <linux/perfmon_kern.h>
#include <linux/nmi.h>
#include <asm/msr.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("P6 PMU description table");
MODULE_LICENSE("GPL");

static int force_nmi;
MODULE_PARM_DESC(force_nmi, "bool: force use of NMI for PMU interrupt");
module_param(force_nmi, bool, 0600);

/*
 * - upper 32 bits are reserved
 * - INT: APIC enable bit is reserved (forced to 1)
 * - bit 21 is reserved
 * - bit 22 is reserved on PEREVNTSEL1
 *
 * RSVD: reserved bits are 1
 */
#define PFM_P6_PMC0_RSVD ((~((1ULL<<32)-1)) | (1ULL<<20) | (1ULL<<21))
#define PFM_P6_PMC1_RSVD ((~((1ULL<<32)-1)) | (1ULL<<20) | (3ULL<<21))

/*
 * force Local APIC interrupt on overflow
 * disable with NO_EMUL64
 */
#define PFM_P6_PMC_VAL  (1ULL<<20)
#define PFM_P6_NO64	(1ULL<<20)


static void __kprobes pfm_p6_quiesce(void);
static int pfm_p6_has_ovfls(struct pfm_context *ctx);
static int pfm_p6_stop_save(struct pfm_context *ctx,
			    struct pfm_event_set *set);

static u64 enable_mask[PFM_MAX_PMCS];
static u16 max_enable;

/*
 * PFM_X86_FL_NO_SHARING: because of the single enable bit on MSR_P6_EVNTSEL0
 * the PMU cannot be shared with NMI watchdog or Oprofile
 */
struct pfm_arch_pmu_info pfm_p6_pmu_info = {
	.stop_save = pfm_p6_stop_save,
	.has_ovfls = pfm_p6_has_ovfls,
	.quiesce = pfm_p6_quiesce,
	.flags = PFM_X86_FL_NO_SHARING,
};

static struct pfm_regmap_desc pfm_p6_pmc_desc[] = {
/* pmc0  */ PMC_D(PFM_REG_I64, "PERFEVTSEL0", PFM_P6_PMC_VAL, PFM_P6_PMC0_RSVD, PFM_P6_NO64, MSR_P6_EVNTSEL0),
/* pmc1  */ PMC_D(PFM_REG_I64, "PERFEVTSEL1", PFM_P6_PMC_VAL, PFM_P6_PMC1_RSVD, PFM_P6_NO64, MSR_P6_EVNTSEL1)
};
#define PFM_P6_NUM_PMCS	ARRAY_SIZE(pfm_p6_pmc_desc)

#define PFM_P6_D(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "PERFCTR"#n,			\
	  .hw_addr = MSR_P6_PERFCTR0+n,		\
	  .rsvd_msk = 0,			\
	  .dep_pmcs[0] = 1ULL << n		\
	}

static struct pfm_regmap_desc pfm_p6_pmd_desc[] = {
/* pmd0  */ PFM_P6_D(0),
/* pmd1  */ PFM_P6_D(1)
};
#define PFM_P6_NUM_PMDS ARRAY_SIZE(pfm_p6_pmd_desc)

static int pfm_p6_probe_pmu(void)
{
	int high, low;

	if (current_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		PFM_INFO("not an Intel processor");
		return -1;
	}

	/*
	 * check for P6 processor family
	 */
	if (current_cpu_data.x86 != 6) {
		PFM_INFO("unsupported family=%d", current_cpu_data.x86);
		return -1;
	}

	switch (current_cpu_data.x86_model) {
	case 1: /* Pentium Pro */
	case 3:
	case 5: /* Pentium II Deschutes */
	case 7 ... 11:
		break;
	case 13:
		/* for Pentium M, we need to check if PMU exist */
		rdmsr(MSR_IA32_MISC_ENABLE, low, high);
		if (low & (1U << 7))
			break;
	default:
		PFM_INFO("unsupported CPU model %d",
			 current_cpu_data.x86_model);
		return -1;

	}

	if (!cpu_has_apic) {
		PFM_INFO("no Local APIC, try rebooting with lapic");
		return -1;
	}
	__set_bit(0, cast_ulp(enable_mask));
	__set_bit(1, cast_ulp(enable_mask));
	max_enable = 1 + 1;
	/*
	 * force NMI interrupt?
	 */
	if (force_nmi)
		pfm_p6_pmu_info.flags |= PFM_X86_FL_USE_NMI;

	return 0;
}

/**
 * pfm_p6_has_ovfls - check for pending overflow condition
 * @ctx: context to work on
 *
 * detect if counters have overflowed.
 * return:
 * 	0 : no overflow
 * 	1 : at least one overflow
 */
static int __kprobes pfm_p6_has_ovfls(struct pfm_context *ctx)
{
	u64 *cnt_mask;
	u64 wmask, val;
	u16 i, num;

	cnt_mask = ctx->regs.cnt_pmds;
	num = ctx->regs.num_counters;
	wmask = 1ULL << pfm_pmu_conf->counter_width;

	/*
	 * we can leverage the fact that we know the mapping
	 * to hardcode the MSR address and avoid accessing
	 * more cachelines
	 *
	 * We need to check cnt_mask because not all registers
	 * may be available.
	 */
	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(cnt_mask))) {
			rdmsrl(MSR_P6_PERFCTR0+i, val);
			if (!(val & wmask))
				return 1;
			num--;
		}
	}
	return 0;
}

/**
 * pfm_p6_stop_save -- stop monitoring and save PMD values
 * @ctx: context to work on
 * @set: current event set
 *
 * return value:
 * 	0 - no need to save PMDs in caller
 * 	1 - need to save PMDs in caller
 */
static int pfm_p6_stop_save(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *pmu_info;
	u64 used_mask[PFM_PMC_BV];
	u64 *cnt_pmds;
	u64 val, wmask, ovfl_mask;
	u32 i, count;

	pmu_info = pfm_pmu_info();

	wmask = 1ULL << pfm_pmu_conf->counter_width;
	bitmap_and(cast_ulp(used_mask),
		   cast_ulp(set->used_pmcs),
		   cast_ulp(enable_mask),
		   max_enable);

	count = bitmap_weight(cast_ulp(used_mask), ctx->regs.max_pmc);

	/*
	 * stop monitoring
	 * Unfortunately, this is very expensive!
	 * wrmsrl() is serializing.
	 */
	for (i = 0; count; i++) {
		if (test_bit(i, cast_ulp(used_mask))) {
			wrmsrl(MSR_P6_EVNTSEL0+i, 0);
			count--;
		}
	}

	/*
	 * if we already having a pending overflow condition, we simply
	 * return to take care of this first.
	 */
	if (set->npend_ovfls)
		return 1;

	ovfl_mask = pfm_pmu_conf->ovfl_mask;
	cnt_pmds = ctx->regs.cnt_pmds;

	/*
	 * check for pending overflows and save PMDs (combo)
	 * we employ used_pmds because we also need to save
	 * and not just check for pending interrupts.
	 *
	 * Must check for counting PMDs because of virtual PMDs
	 */
	count = set->nused_pmds;
	for (i = 0; count; i++) {
		if (test_bit(i, cast_ulp(set->used_pmds))) {
			val = pfm_arch_read_pmd(ctx, i);
			if (likely(test_bit(i, cast_ulp(cnt_pmds)))) {
				if (!(val & wmask)) {
					__set_bit(i, cast_ulp(set->povfl_pmds));
					set->npend_ovfls++;
				}
				val = (set->pmds[i].value & ~ovfl_mask) | (val & ovfl_mask);
			}
			set->pmds[i].value = val;
			count--;
		}
	}
	/* 0 means: no need to save PMDs at upper level */
	return 0;
}

/**
 * pfm_p6_quiesce_pmu -- stop monitoring without grabbing any lock
 *
 * called from NMI interrupt handler to immediately stop monitoring
 * cannot grab any lock, including perfmon related locks
 */
static void __kprobes pfm_p6_quiesce(void)
{
	/*
	 * quiesce PMU by clearing available registers that have
	 * the start/stop capability
	 *
	 * P6 processors only have enable bit on PERFEVTSEL0
	 */
	if (test_bit(0, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_P6_EVNTSEL0, 0);
}

/*
 * Counters have 40 bits implemented. However they are designed such
 * that bits [32-39] are sign extensions of bit 31. As such the
 * effective width of a counter for P6-like PMU is 31 bits only.
 *
 * See IA-32 Intel Architecture Software developer manual Vol 3B
 */
static struct pfm_pmu_config pfm_p6_pmu_conf = {
	.pmu_name = "Intel P6 processor Family",
	.counter_width = 31,
	.pmd_desc = pfm_p6_pmd_desc,
	.pmc_desc = pfm_p6_pmc_desc,
	.num_pmc_entries = PFM_P6_NUM_PMCS,
	.num_pmd_entries = PFM_P6_NUM_PMDS,
	.probe_pmu = pfm_p6_probe_pmu,
	.version = "1.0",
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmu_info = &pfm_p6_pmu_info
};

static int __init pfm_p6_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_p6_pmu_conf);
}

static void __exit pfm_p6_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_p6_pmu_conf);
}

module_init(pfm_p6_pmu_init_module);
module_exit(pfm_p6_pmu_cleanup_module);
