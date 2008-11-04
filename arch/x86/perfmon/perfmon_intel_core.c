/*
 * This file contains the Intel Core PMU registers description tables.
 * Intel Core-based processors support architectural perfmon v2 + PEBS
 *
 * Copyright (c) 2006-2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/perfmon_kern.h>
#include <linux/nmi.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("Intel Core");
MODULE_LICENSE("GPL");

static int force_nmi;
MODULE_PARM_DESC(force_nmi, "bool: force use of NMI for PMU interrupt");
module_param(force_nmi, bool, 0600);

/*
 * - upper 32 bits are reserved
 * - INT: APIC enable bit is reserved (forced to 1)
 * - bit 21 is reserved
 *
 *   RSVD: reserved bits must be 1
 */
#define PFM_CORE_PMC_RSVD ((~((1ULL<<32)-1)) \
			| (1ULL<<20)   \
			| (1ULL<<21))

/*
 * Core counters are 40-bits
 */
#define PFM_CORE_CTR_RSVD	(~((1ULL<<40)-1))

/*
 * force Local APIC interrupt on overflow
 * disable with NO_EMUL64
 */
#define PFM_CORE_PMC_VAL	(1ULL<<20)
#define PFM_CORE_NO64		(1ULL<<20)

#define PFM_CORE_NA { .reg_type = PFM_REGT_NA}

#define PFM_CORE_CA(m, c, t) \
	{ \
	  .addrs[0] = m, \
	  .ctr = c, \
	  .reg_type = t \
	}

struct pfm_ds_area_intel_core {
	u64	bts_buf_base;
	u64	bts_index;
	u64	bts_abs_max;
	u64	bts_intr_thres;
	u64	pebs_buf_base;
	u64	pebs_index;
	u64	pebs_abs_max;
	u64	pebs_intr_thres;
	u64	pebs_cnt_reset;
};

static void pfm_core_restore_pmcs(struct pfm_context *ctx,
				  struct pfm_event_set *set);
static int pfm_core_has_ovfls(struct pfm_context *ctx);
static int pfm_core_stop_save(struct pfm_context *ctx,
			      struct pfm_event_set *set);
static void __kprobes pfm_core_quiesce(void);

static u64 enable_mask[PFM_MAX_PMCS];
static u16 max_enable;

struct pfm_arch_pmu_info pfm_core_pmu_info = {
	.stop_save = pfm_core_stop_save,
	.has_ovfls = pfm_core_has_ovfls,
	.quiesce = pfm_core_quiesce,
	.restore_pmcs = pfm_core_restore_pmcs
};

static struct pfm_regmap_desc pfm_core_pmc_desc[] = {
/* pmc0  */ {
	      .type = PFM_REG_I64,
	      .desc = "PERFEVTSEL0",
	      .dfl_val = PFM_CORE_PMC_VAL,
	      .rsvd_msk = PFM_CORE_PMC_RSVD,
	      .no_emul64_msk = PFM_CORE_NO64,
	      .hw_addr = MSR_P6_EVNTSEL0
	    },
/* pmc1  */ {
	      .type = PFM_REG_I64,
	      .desc = "PERFEVTSEL1",
	      .dfl_val = PFM_CORE_PMC_VAL,
	      .rsvd_msk = PFM_CORE_PMC_RSVD,
	      .no_emul64_msk = PFM_CORE_NO64,
	      .hw_addr = MSR_P6_EVNTSEL1
	    },
/* pmc2  */ PMX_NA, PMX_NA,
/* pmc4  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc8  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc12 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc16 */ { .type = PFM_REG_I,
	      .desc = "FIXED_CTRL",
	      .dfl_val = 0x888ULL,
	      .rsvd_msk = 0xfffffffffffffcccULL,
	      .no_emul64_msk = 0,
	      .hw_addr = MSR_CORE_PERF_FIXED_CTR_CTRL
	    },
/* pmc17  */ { .type = PFM_REG_W,
	      .desc = "PEBS_ENABLE",
	      .dfl_val = 0,
	      .rsvd_msk = 0xfffffffffffffffeULL,
	      .no_emul64_msk = 0,
	      .hw_addr = MSR_IA32_PEBS_ENABLE
	    }
};

#define PFM_CORE_D(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "PMC"#n,			\
	  .rsvd_msk =  PFM_CORE_CTR_RSVD,	\
	  .hw_addr = MSR_P6_PERFCTR0+n,		\
	  .dep_pmcs[0] = 1ULL << n		\
	}

#define PFM_CORE_FD(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "FIXED_CTR"#n,		\
	  .rsvd_msk =  PFM_CORE_CTR_RSVD,	\
	  .hw_addr = MSR_CORE_PERF_FIXED_CTR0+n,\
	  .dep_pmcs[0] = 1ULL << 16		\
	}

static struct pfm_regmap_desc pfm_core_pmd_desc[] = {
/* pmd0  */ PFM_CORE_D(0),
/* pmd1  */ PFM_CORE_D(1),
/* pmd2  */ PMX_NA, PMX_NA,
/* pmd4  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmd8  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmd12 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmd16 */ PFM_CORE_FD(0),
/* pmd17 */ PFM_CORE_FD(1),
/* pmd18 */ PFM_CORE_FD(2)
};
#define PFM_CORE_NUM_PMCS	ARRAY_SIZE(pfm_core_pmc_desc)
#define PFM_CORE_NUM_PMDS	ARRAY_SIZE(pfm_core_pmd_desc)

static struct pfm_pmu_config pfm_core_pmu_conf;

static int pfm_core_probe_pmu(void)
{
	/*
	 * Check for Intel Core processor explicitely
	 * Checking for cpu_has_perfmon is not enough as this
	 * matches intel Core Duo/Core Solo but none supports
	 * PEBS.
	 *
	 * Intel Core = arch perfmon v2 + PEBS
	 */
	if (current_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		PFM_INFO("not an AMD processor");
		return -1;
	}

	if (current_cpu_data.x86 != 6)
		return -1;

	switch (current_cpu_data.x86_model) {
	case 15: /* Merom */
		break;
	case 23: /* Penryn */
		break;
	case 29: /* Dunnington */
		break;
	default:
		return -1;
	}

	if (!cpu_has_apic) {
		PFM_INFO("no Local APIC, unsupported");
		return -1;
	}

	PFM_INFO("nmi_watchdog=%d nmi_active=%d force_nmi=%d",
		nmi_watchdog, atomic_read(&nmi_active), force_nmi);

	/*
	 * Intel Core processors implement DS and PEBS, no need to check
	 */
	if (cpu_has_pebs)
		PFM_INFO("PEBS supported, enabled");

	/*
	 * initialize bitmask of register with enable capability, i.e.,
	 * startstop. This is used to restrict the number of registers to
	 * touch on start/stop
	 * max_enable: number of bits to scan in enable_mask = highest + 1
	 *
	 * may be adjusted in pfm_arch_pmu_acquire()
	 */
	__set_bit(0, cast_ulp(enable_mask));
	__set_bit(1, cast_ulp(enable_mask));
	__set_bit(16, cast_ulp(enable_mask));
	__set_bit(17, cast_ulp(enable_mask));
	max_enable = 17+1;

	if (force_nmi)
		pfm_core_pmu_info.flags |= PFM_X86_FL_USE_NMI;

	return 0;
}

static int pfm_core_pmc17_check(struct pfm_context *ctx,
			     struct pfm_event_set *set,
			     struct pfarg_pmc *req)
{
	struct pfm_arch_context *ctx_arch;
	ctx_arch = pfm_ctx_arch(ctx);

	/*
	 * if user activates PEBS_ENABLE, then we need to have a valid
	 * DS Area setup. This only happens when the PEBS sampling format is
	 * used in which case PFM_X86_USE_PEBS is set. We must reject all other
	 * requests.
	 *
	 * Otherwise we may pickup stale MSR_IA32_DS_AREA values. It appears
	 * that a value of 0 for this MSR does crash the system with
	 * PEBS_ENABLE=1.
	 */
	if (!ctx_arch->flags.use_pebs && req->reg_value) {
		PFM_DBG("pmc17 useable only with a PEBS sampling format");
		return -EINVAL;
	}
	return 0;
}

/*
 * detect is counters have overflowed.
 * return:
 * 	0 : no overflow
 * 	1 : at least one overflow
 *
 * used by Intel Core-based processors
 */
static int __kprobes pfm_core_has_ovfls(struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;
	u64 *cnt_mask;
	u64 wmask, val;
	u16 i, num;

	pmu_info = &pfm_core_pmu_info;
	cnt_mask = ctx->regs.cnt_pmds;
	num = ctx->regs.num_counters;
	wmask = 1ULL << pfm_pmu_conf->counter_width;

	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(cnt_mask))) {
			rdmsrl(pfm_core_pmd_desc[i].hw_addr, val);
			if (!(val & wmask))
				return 1;
			num--;
		}
	}
	return 0;
}

static int pfm_core_stop_save(struct pfm_context *ctx,
			      struct pfm_event_set *set)
{
	struct pfm_arch_context *ctx_arch;
	struct pfm_ds_area_intel_core *ds = NULL;
	u64 used_mask[PFM_PMC_BV];
	u64 *cnt_mask;
	u64 val, wmask, ovfl_mask;
	u16 count, has_ovfl;
	u16 i, pebs_idx = ~0;

	ctx_arch = pfm_ctx_arch(ctx);

	wmask = 1ULL << pfm_pmu_conf->counter_width;

	/*
	 * used enable pmc bitmask
	 */
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
	cnt_mask = ctx->regs.cnt_pmds;

	if (ctx_arch->flags.use_pebs) {
		ds = ctx_arch->ds_area;
		pebs_idx = 0; /* PMC0/PMD0 */
		PFM_DBG("ds=%p pebs_idx=0x%llx thres=0x%llx",
			ds,
			(unsigned long long)ds->pebs_index,
			(unsigned long long)ds->pebs_intr_thres);
	}

	/*
	 * Check for pending overflows and save PMDs (combo)
	 * We employ used_pmds and not intr_pmds because we must
	 * also saved on PMD registers.
	 * Must check for counting PMDs because of virtual PMDs
	 *
	 * XXX: should use the ovf_status register instead, yet
	 *      we would have to check if NMI is used and fallback
	 *      to individual pmd inspection.
	 */
	count = set->nused_pmds;

	for (i = 0; count; i++) {
		if (test_bit(i, cast_ulp(set->used_pmds))) {
			val = pfm_arch_read_pmd(ctx, i);
			if (likely(test_bit(i, cast_ulp(cnt_mask)))) {
				if (i == pebs_idx)
					has_ovfl = (ds->pebs_index >=
						    ds->pebs_intr_thres);
				else
					has_ovfl = !(val & wmask);
				if (has_ovfl) {
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
 * pfm_core_quiesce - stop monitoring without grabbing any lock
 *
 * called from NMI interrupt handler to immediately stop monitoring
 * cannot grab any lock, including perfmon related locks
 */
static void __kprobes pfm_core_quiesce(void)
{
	/*
	 * quiesce PMU by clearing available registers that have
	 * the start/stop capability
	 */
	if (test_bit(0, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_P6_EVNTSEL0, 0);
	if (test_bit(1, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_P6_EVNTSEL1, 0);
	if (test_bit(16, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, 0);
	if (test_bit(17, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_IA32_PEBS_ENABLE, 0);
}
/**
 * pfm_core_restore_pmcs - reload PMC registers
 * @ctx: context to restore from
 * @set: current event set
 *
 * optimized version of pfm_arch_restore_pmcs(). On Core, we can
 * afford to only restore the pmcs registers we use, because they are
 * all independent from each other.
 */
static void pfm_core_restore_pmcs(struct pfm_context *ctx,
				  struct pfm_event_set *set)
{
	struct pfm_arch_context *ctx_arch;
	u64 *mask;
	u16 i, num;

	ctx_arch = pfm_ctx_arch(ctx);

	/*
	 * must restore DS pointer before restoring PMCs
	 * as this can potentially reactivate monitoring
	 */
	if (ctx_arch->flags.use_ds)
		wrmsrl(MSR_IA32_DS_AREA, (unsigned long)ctx_arch->ds_area);

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
 * Counters may have model-specific width which can be probed using
 * the CPUID.0xa leaf. Yet, the documentation says: "
 * In the initial implementation, only the read bit width is reported
 * by CPUID, write operations are limited to the low 32 bits.
 * Bits [w-32] are sign extensions of bit 31. As such the effective width
 * of a counter is 31 bits only.
 */
static struct pfm_pmu_config pfm_core_pmu_conf = {
	.pmu_name = "Intel Core",
	.pmd_desc = pfm_core_pmd_desc,
	.counter_width = 31,
	.num_pmc_entries = PFM_CORE_NUM_PMCS,
	.num_pmd_entries = PFM_CORE_NUM_PMDS,
	.pmc_desc = pfm_core_pmc_desc,
	.probe_pmu = pfm_core_probe_pmu,
	.version = "1.2",
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmu_info = &pfm_core_pmu_info,
	.pmc_write_check = pfm_core_pmc17_check
};

static int __init pfm_core_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_core_pmu_conf);
}

static void __exit pfm_core_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_core_pmu_conf);
}

module_init(pfm_core_pmu_init_module);
module_exit(pfm_core_pmu_cleanup_module);
