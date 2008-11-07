/*
 * This file contains the Intel architectural perfmon v1, v2, v3
 * description tables.
 *
 * Architectural perfmon was introduced with Intel Core Solo/Duo
 * processors.
 *
 * Copyright (c) 2006-2007 Hewlett-Packard Development Company, L.P.
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
#include <asm/apic.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("Intel architectural perfmon v1");
MODULE_LICENSE("GPL");

static int force, force_nmi;
MODULE_PARM_DESC(force, "bool: force module to load succesfully");
MODULE_PARM_DESC(force_nmi, "bool: force use of NMI for PMU interrupt");
module_param(force, bool, 0600);
module_param(force_nmi, bool, 0600);

static u64 enable_mask[PFM_MAX_PMCS];
static u16 max_enable;

/*
 * - upper 32 bits are reserved
 * - INT: APIC enable bit is reserved (forced to 1)
 * - bit 21 is reserved
 *
 * RSVD: reserved bits are 1
 */
#define PFM_IA_PMC_RSVD	((~((1ULL<<32)-1)) \
			| (1ULL<<20) \
			| (1ULL<<21))

/*
 * force Local APIC interrupt on overflow
 * disable with NO_EMUL64
 */
#define PFM_IA_PMC_VAL	(1ULL<<20)
#define PFM_IA_NO64	(1ULL<<20)

/*
 * architectuture specifies that:
 * IA32_PMCx MSR        : starts at 0x0c1 & occupy a contiguous block of MSR
 * IA32_PERFEVTSELx MSR : starts at 0x186 & occupy a contiguous block of MSR
 * MSR_GEN_FIXED_CTR0   : starts at 0x309 & occupy a contiguous block of MSR
 */
#define MSR_GEN_SEL_BASE	MSR_P6_EVNTSEL0
#define MSR_GEN_PMC_BASE	MSR_P6_PERFCTR0
#define MSR_GEN_FIXED_PMC_BASE	MSR_CORE_PERF_FIXED_CTR0

/*
 * layout of EAX for CPUID.0xa leaf function
 */
struct pmu_eax {
	unsigned int version:8;		/* architectural perfmon version */
	unsigned int num_cnt:8; 	/* number of generic counters */
	unsigned int cnt_width:8;	/* width of generic counters */
	unsigned int ebx_length:8;	/* number of architected events */
};

/*
 * layout of EDX for CPUID.0xa leaf function when perfmon v2 is detected
 */
struct pmu_edx {
	unsigned int num_cnt:5;		/* number of fixed counters */
	unsigned int cnt_width:8;	/* width of fixed counters */
	unsigned int reserved:19;
};

static void pfm_intel_arch_restore_pmcs(struct pfm_context *ctx,
					struct pfm_event_set *set);
static int pfm_intel_arch_stop_save(struct pfm_context *ctx,
				    struct pfm_event_set *set);
static int pfm_intel_arch_has_ovfls(struct pfm_context *ctx);
static void __kprobes pfm_intel_arch_quiesce(void);

/*
 * physical addresses of MSR controlling the perfevtsel and counter registers
 */
struct pfm_arch_pmu_info pfm_intel_arch_pmu_info = {
	.stop_save = pfm_intel_arch_stop_save,
	.has_ovfls = pfm_intel_arch_has_ovfls,
	.quiesce = pfm_intel_arch_quiesce,
	.restore_pmcs = pfm_intel_arch_restore_pmcs
};

#define PFM_IA_C(n) {                   \
	.type = PFM_REG_I64,            \
	.desc = "PERFEVTSEL"#n,         \
	.dfl_val = PFM_IA_PMC_VAL,      \
	.rsvd_msk = PFM_IA_PMC_RSVD,    \
	.no_emul64_msk = PFM_IA_NO64,   \
	.hw_addr = MSR_GEN_SEL_BASE+(n) \
	}

#define PFM_IA_D(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "PMC"#n,			\
	  .hw_addr = MSR_P6_PERFCTR0+n,		\
	  .dep_pmcs[0] = 1ULL << n		\
	}

#define PFM_IA_FD(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "FIXED_CTR"#n,		\
	  .hw_addr = MSR_CORE_PERF_FIXED_CTR0+n,\
	  .dep_pmcs[0] = 1ULL << 16		\
	}

static struct pfm_regmap_desc pfm_intel_arch_pmc_desc[] = {
/* pmc0  */ PFM_IA_C(0),  PFM_IA_C(1),   PFM_IA_C(2),  PFM_IA_C(3),
/* pmc4  */ PFM_IA_C(4),  PFM_IA_C(5),   PFM_IA_C(6),  PFM_IA_C(7),
/* pmc8  */ PFM_IA_C(8),  PFM_IA_C(9),  PFM_IA_C(10), PFM_IA_C(11),
/* pmc12 */ PFM_IA_C(12), PFM_IA_C(13), PFM_IA_C(14), PFM_IA_C(15),

/* pmc16 */ { .type = PFM_REG_I,
	      .desc = "FIXED_CTRL",
	      .dfl_val = 0x8888888888888888ULL, /* force PMI */
	      .rsvd_msk = 0, /* set dynamically */
	      .no_emul64_msk = 0,
	      .hw_addr = MSR_CORE_PERF_FIXED_CTR_CTRL
	    },
};
#define PFM_IA_MAX_PMCS	ARRAY_SIZE(pfm_intel_arch_pmc_desc)

static struct pfm_regmap_desc pfm_intel_arch_pmd_desc[] = {
/* pmd0  */  PFM_IA_D(0),  PFM_IA_D(1),  PFM_IA_D(2),  PFM_IA_D(3),
/* pmd4  */  PFM_IA_D(4),  PFM_IA_D(5),  PFM_IA_D(6),  PFM_IA_D(7),
/* pmd8  */  PFM_IA_D(8),  PFM_IA_D(9), PFM_IA_D(10), PFM_IA_D(11),
/* pmd12 */ PFM_IA_D(12), PFM_IA_D(13), PFM_IA_D(14), PFM_IA_D(15),

/* pmd16 */ PFM_IA_FD(0), PFM_IA_FD(1), PFM_IA_FD(2), PFM_IA_FD(3),
/* pmd20 */ PFM_IA_FD(4), PFM_IA_FD(5), PFM_IA_FD(6), PFM_IA_FD(7),
/* pmd24 */ PFM_IA_FD(8), PFM_IA_FD(9), PFM_IA_FD(10), PFM_IA_FD(11),
/* pmd28 */ PFM_IA_FD(16), PFM_IA_FD(17), PFM_IA_FD(18), PFM_IA_FD(19)
};
#define PFM_IA_MAX_PMDS	ARRAY_SIZE(pfm_intel_arch_pmd_desc)

#define PFM_IA_MAX_CNT		16 /* # generic counters in mapping table */
#define PFM_IA_MAX_FCNT		16 /* # of fixed counters in mapping table */
#define PFM_IA_FCNT_BASE	16 /* base index of fixed counters PMD */

static struct pfm_pmu_config pfm_intel_arch_pmu_conf;

static void pfm_intel_arch_check_errata(void)
{
	/*
	 * Core Duo errata AE49 (no fix). Both counters share a single
	 * enable bit in PERFEVTSEL0
	 */
	if (current_cpu_data.x86 == 6 && current_cpu_data.x86_model == 14)
		pfm_intel_arch_pmu_info.flags |= PFM_X86_FL_NO_SHARING;
}

static inline void set_enable_mask(unsigned int i)
{
	__set_bit(i, cast_ulp(enable_mask));

	/* max_enable = highest + 1 */
	if ((i+1) > max_enable)
		max_enable = i+ 1;
}

static void pfm_intel_arch_setup_generic(unsigned int version,
					 unsigned int width,
					 unsigned int count)
{
	u64 rsvd;
	unsigned int i;

	/*
	 * first we handle the generic counters:
	 *
	 * - ensure HW does not have more registers than hardcoded in the tables
	 * - adjust rsvd_msk to actual counter width
	 * - initialize enable_mask (list of PMC with start/stop capability)
	 * - mark unused hardcoded generic counters as unimplemented
	 */

	/*
	 * min of number of Hw counters and hardcoded in the tables
	 */
	if (count >= PFM_IA_MAX_CNT) {
		printk(KERN_INFO "perfmon: Limiting number of generic counters"
				 " to %u, HW supports %u",
				 PFM_IA_MAX_CNT, count);
		count = PFM_IA_MAX_CNT;
	}

	/*
	 * adjust rsvd_msk for generic counters based on actual width
	 * initialize enable_mask (1 per pmd)
	 */
	rsvd = ~((1ULL << width)-1);
	for (i = 0; i < count; i++) {
		pfm_intel_arch_pmd_desc[i].rsvd_msk = rsvd;
		set_enable_mask(i);
	}

	/*
	 * handle version 3 new anythread bit (21)
	 */
	if (version == 3) {
		for (i = 0; i < count; i++)
			pfm_intel_arch_pmc_desc[i].rsvd_msk &= ~(1ULL << 21);
	}


	/*
	 * mark unused generic counters as not available
	 */
	for (i = count ; i < PFM_IA_MAX_CNT; i++) {
		pfm_intel_arch_pmd_desc[i].type = PFM_REG_NA;
		pfm_intel_arch_pmc_desc[i].type = PFM_REG_NA;
	}
}

static void pfm_intel_arch_setup_fixed(unsigned int version,
				       unsigned int width,
				       unsigned int count)
{
	u64 rsvd, dfl;
	unsigned int i;

	/*
	 * handle the fixed counters (if any):
	 *
	 * - ensure HW does not have more registers than hardcoded in the tables
	 * - adjust rsvd_msk to actual counter width
	 * - initialize enable_mask (list of PMC with start/stop capability)
	 * - mark unused hardcoded generic counters as unimplemented
	 */
	if (count >= PFM_IA_MAX_FCNT) {
		printk(KERN_INFO "perfmon: Limiting number of fixed counters"
				 " to %u, HW supports %u",
				 PFM_IA_MAX_FCNT, count);
		count = PFM_IA_MAX_FCNT;
	}
	/*
	 * adjust rsvd_msk for fixed counters based on actual width
	 */
	rsvd = ~((1ULL << width)-1);
	for (i = 0; i < count; i++)
		pfm_intel_arch_pmd_desc[PFM_IA_FCNT_BASE+i].rsvd_msk = rsvd;

	/*
	 * handle version new anythread bit (bit 2)
	 */
	if (version == 3)
		rsvd = 1ULL << 3;
	else
		rsvd = 3ULL << 2;

	pfm_intel_arch_pmc_desc[16].rsvd_msk = 0;
	for (i = 0; i < count; i++)
		pfm_intel_arch_pmc_desc[16].rsvd_msk |= rsvd << (i<<2);

	/*
	 * mark unused fixed counters as unimplemented
	 *
	 * update the rsvd_msk, dfl_val in FIXED_CTRL:
	 * 	- rsvd_msk: set all 4 bits
	 *	- dfl_val : clear all 4 bits
	 */
	dfl = pfm_intel_arch_pmc_desc[16].dfl_val;
	rsvd = pfm_intel_arch_pmc_desc[16].rsvd_msk;

	for (i = count ; i < PFM_IA_MAX_FCNT; i++) {
		pfm_intel_arch_pmd_desc[PFM_IA_FCNT_BASE+i].type = PFM_REG_NA;
		rsvd |= 0xfULL << (i<<2);
		dfl &= ~(0xfULL << (i<<2));
	}

	/*
	 * FIXED_CTR_CTRL unavailable when no fixed counters are defined
	 */
	if (!count) {
		pfm_intel_arch_pmc_desc[16].type = PFM_REG_NA;
	} else {
		/* update rsvd_mask and dfl_val */
		pfm_intel_arch_pmc_desc[16].rsvd_msk = rsvd;
		pfm_intel_arch_pmc_desc[16].dfl_val = dfl;
		set_enable_mask(16);
	}
}

static int pfm_intel_arch_probe_pmu(void)
{
	union {
		unsigned int val;
		struct pmu_eax eax;
		struct pmu_edx edx;
	} eax, edx;
	unsigned int ebx, ecx;
	unsigned int width = 0;

	edx.val = 0;

	if (!(cpu_has_arch_perfmon || force)) {
		PFM_INFO("no support for Intel architectural PMU");
		return -1;
	}

	if (!cpu_has_apic) {
		PFM_INFO("no Local APIC, try rebooting with lapic option");
		return -1;
	}

	/* cpuid() call protected by cpu_has_arch_perfmon */
	cpuid(0xa, &eax.val, &ebx, &ecx, &edx.val);

	/*
	 * reject processors supported by perfmon_intel_core
	 *
	 * We need to do this explicitely to avoid depending
	 * on the link order in case, the modules are compiled as
	 * builtin.
	 *
	 * non Intel processors are rejected by cpu_has_arch_perfmon
	 */
	if (current_cpu_data.x86 == 6 && !force) {
		switch (current_cpu_data.x86_model) {
		case 15: /* Merom: use perfmon_intel_core  */
		case 23: /* Penryn: use perfmon_intel_core */
			return -1;
		default:
			break;
		}
	}

	/*
	 * some 6/15 models have buggy BIOS
	 */
	if (eax.eax.version == 0
	    && current_cpu_data.x86 == 6 && current_cpu_data.x86_model == 15) {
		PFM_INFO("buggy v2 BIOS, adjusting for 2 generic counters");
		eax.eax.version = 2;
		eax.eax.num_cnt = 2;
		eax.eax.cnt_width = 40;
	}

	/*
	 * Intel Atom processors have a buggy firmware which does not report
	 * the correct number of fixed counters
	 */
	if (eax.eax.version == 3 && edx.edx.num_cnt < 3
	    && current_cpu_data.x86 == 6 && current_cpu_data.x86_model == 28) {
		PFM_INFO("buggy v3 BIOS, adjusting for 3 fixed counters");
		edx.edx.num_cnt = 3;
	}

	/*
	 * some v2 BIOSes are incomplete
	 */
	if (eax.eax.version == 2 && !edx.edx.num_cnt) {
		PFM_INFO("buggy v2 BIOS, adjusting for 3 fixed counters");
		edx.edx.num_cnt = 3;
		edx.edx.cnt_width = 40;
	}

	/*
	 * no fixed counters on earlier versions
	 */
	if (eax.eax.version < 2) {
		edx.val = 0;
	} else {
		/*
		 * use the min value of both widths until we support
		 * variable width counters
		 */
		width = eax.eax.cnt_width < edx.edx.cnt_width ?
			eax.eax.cnt_width : edx.edx.cnt_width;
	}

	PFM_INFO("detected architecural perfmon v%d", eax.eax.version);
	PFM_INFO("num_gen=%d width=%d num_fixed=%d width=%d",
		  eax.eax.num_cnt,
		  eax.eax.cnt_width,
		  edx.edx.num_cnt,
		  edx.edx.cnt_width);


	pfm_intel_arch_setup_generic(eax.eax.version,
				     width,
				     eax.eax.num_cnt);

	pfm_intel_arch_setup_fixed(eax.eax.version,
				   width,
				   edx.edx.num_cnt);

	if (force_nmi)
		pfm_intel_arch_pmu_info.flags |= PFM_X86_FL_USE_NMI;

	pfm_intel_arch_check_errata();

	return 0;
}

/**
 * pfm_intel_arch_has_ovfls - check for pending overflow condition
 * @ctx: context to work on
 *
 * detect if counters have overflowed.
 * return:
 * 	0 : no overflow
 * 	1 : at least one overflow
 */
static int __kprobes pfm_intel_arch_has_ovfls(struct pfm_context *ctx)
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
			rdmsrl(pfm_intel_arch_pmd_desc[i].hw_addr, val);
			if (!(val & wmask))
				return 1;
			num--;
		}
	}
	return 0;
}

static int pfm_intel_arch_stop_save(struct pfm_context *ctx,
				    struct pfm_event_set *set)
{
	u64 used_mask[PFM_PMC_BV];
	u64 *cnt_pmds;
	u64 val, wmask, ovfl_mask;
	u32 i, count;

	wmask = 1ULL << pfm_pmu_conf->counter_width;

	bitmap_and(cast_ulp(used_mask),
		   cast_ulp(set->used_pmcs),
		   cast_ulp(enable_mask),
		   max_enable);

	count = bitmap_weight(cast_ulp(used_mask), max_enable);

	/*
	 * stop monitoring
	 * Unfortunately, this is very expensive!
	 * wrmsrl() is serializing.
	 */
	for (i = 0; count; i++) {
		if (test_bit(i, cast_ulp(used_mask))) {
			wrmsrl(pfm_pmu_conf->pmc_desc[i].hw_addr, 0);
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
				val = (set->pmds[i].value & ~ovfl_mask)
					| (val & ovfl_mask);
			}
			set->pmds[i].value = val;
			count--;
		}
	}
	/* 0 means: no need to save PMDs at upper level */
	return 0;
}

/**
 * pfm_intel_arch_quiesce - stop monitoring without grabbing any lock
 *
 * called from NMI interrupt handler to immediately stop monitoring
 * cannot grab any lock, including perfmon related locks
 */
static void __kprobes pfm_intel_arch_quiesce(void)
{
	u16 i;

	/*
	 * PMC16 is the fixed control control register so it has a
	 * distinct MSR address
	 *
	 * We do not use the hw_addr field in the table to avoid touching
	 * too many cachelines
	 */
	for (i = 0; i < pfm_pmu_conf->regs_all.max_pmc; i++) {
		if (test_bit(i, cast_ulp(pfm_pmu_conf->regs_all.pmcs))) {
			if (i == 16)
				wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, 0);
			else
				wrmsrl(MSR_P6_EVNTSEL0+i, 0);
		}
	}
}

/**
 * pfm_intel_arch_restore_pmcs - reload PMC registers
 * @ctx: context to restore from
 * @set: current event set
 *
 * optimized version of pfm_arch_restore_pmcs(). On architectural perfmon,
 * we can afford to only restore the pmcs registers we use, because they
 * are all independent from each other.
 */
static void pfm_intel_arch_restore_pmcs(struct pfm_context *ctx,
					struct pfm_event_set *set)
{
	u64 *mask;
	u16 i, num;

	mask = set->used_pmcs;
	num = set->nused_pmcs;
	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(mask))) {
			wrmsrl(pfm_pmu_conf->pmc_desc[i].hw_addr, set->pmcs[i]);
			num--;
		}
	}
}
/*
 * Counters may have model-specific width. Yet the documentation says
 * that only the lower 32 bits can be written to due to the specification
 * of wrmsr. bits [32-(w-1)] are sign extensions of bit 31. Bits [w-63] must
 * not be set (see rsvd_msk for PMDs). As such the effective width of a
 * counter is 31 bits only regardless of what CPUID.0xa returns.
 *
 * See IA-32 Intel Architecture Software developer manual Vol 3B chapter 18
 */
static struct pfm_pmu_config pfm_intel_arch_pmu_conf = {
	.pmu_name = "Intel architectural",
	.pmd_desc = pfm_intel_arch_pmd_desc,
	.counter_width   = 31,
	.num_pmc_entries = PFM_IA_MAX_PMCS,
	.num_pmd_entries = PFM_IA_MAX_PMDS,
	.pmc_desc = pfm_intel_arch_pmc_desc,
	.probe_pmu = pfm_intel_arch_probe_pmu,
	.version = "1.0",
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmu_info = &pfm_intel_arch_pmu_info
};

static int __init pfm_intel_arch_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_intel_arch_pmu_conf);
}

static void __exit pfm_intel_arch_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_intel_arch_pmu_conf);
}

module_init(pfm_intel_arch_pmu_init_module);
module_exit(pfm_intel_arch_pmu_cleanup_module);
