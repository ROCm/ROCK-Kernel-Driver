/*
 * perfmon support for Intel Atom (architectural perfmon v3 + PEBS)
 *
 * Copyright (c) 2008 Google,Inc
 * Contributed by Stephane Eranian <eranian@gmail.com>
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
#include <asm/msr.h>

MODULE_AUTHOR("Stephane Eranian <eranian@gmail.com>");
MODULE_DESCRIPTION("Intel Atom");
MODULE_LICENSE("GPL");

static int force, force_nmi;
MODULE_PARM_DESC(force, "bool: force module to load succesfully");
MODULE_PARM_DESC(force_nmi, "bool: force use of NMI for PMU interrupt");
module_param(force, bool, 0600);
module_param(force_nmi, bool, 0600);

/*
 * - upper 32 bits are reserved
 * - INT: APIC enable bit is reserved (forced to 1)
 *
 * RSVD: reserved bits are 1
 */
#define PFM_ATOM_PMC_RSVD	((~((1ULL<<32)-1)) | (1ULL<<20))

/*
 * force Local APIC interrupt on overflow
 * disable with NO_EMUL64
 */
#define PFM_ATOM_PMC_VAL	(1ULL<<20)
#define PFM_ATOM_NO64	(1ULL<<20)

/*
 * Atom counters are 40-bits. 40-bits can be read but ony 31 can be written
 * to due to a limitation of wrmsr. Bits [[63-32] are sign extensions of bit 31.
 * Bits [63-40] must not be set
 *
 * See IA-32 Intel Architecture Software developer manual Vol 3B chapter 18
 */
#define PFM_ATOM_PMD_WIDTH	31
#define PFM_ATOM_PMD_RSVD	~((1ULL << 40)-1)

static void pfm_intel_atom_acquire_pmu_percpu(void);
static void pfm_intel_atom_release_pmu_percpu(void);
static void pfm_intel_atom_restore_pmcs(struct pfm_context *ctx,
					struct pfm_event_set *set);
static int pfm_intel_atom_stop_save(struct pfm_context *ctx,
				    struct pfm_event_set *set);
static int pfm_intel_atom_has_ovfls(struct pfm_context *ctx);
static void __kprobes pfm_intel_atom_quiesce(void);

struct pfm_arch_pmu_info pfm_intel_atom_pmu_info = {
	.stop_save = pfm_intel_atom_stop_save,
	.has_ovfls = pfm_intel_atom_has_ovfls,
	.quiesce = pfm_intel_atom_quiesce,
	.restore_pmcs = pfm_intel_atom_restore_pmcs,
	.acquire_pmu_percpu = pfm_intel_atom_acquire_pmu_percpu,
	.release_pmu_percpu = pfm_intel_atom_release_pmu_percpu

};

#define PFM_ATOM_C(n) {                  \
	.type = PFM_REG_I64,             \
	.desc = "PERFEVTSEL"#n,          \
	.dfl_val = PFM_ATOM_PMC_VAL,       \
	.rsvd_msk = PFM_ATOM_PMC_RSVD,     \
	.no_emul64_msk = PFM_ATOM_NO64,    \
	.hw_addr = MSR_P6_EVNTSEL0 + (n) \
	}


static struct pfm_regmap_desc pfm_intel_atom_pmc_desc[] = {
/* pmc0  */ PFM_ATOM_C(0),
/* pmc1  */ PFM_ATOM_C(1),
/* pmc2  */ PMX_NA, PMX_NA,
/* pmc4  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc8  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc12 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmc16 */ { .type = PFM_REG_I,
	      .desc = "FIXED_CTRL",
	      .dfl_val = 0x0000000000000888ULL,  /* force PMI */
	      .rsvd_msk = 0xfffffffffffffcccULL, /* 3 fixed counters defined */
	      .no_emul64_msk = 0,
	      .hw_addr = MSR_CORE_PERF_FIXED_CTR_CTRL
	    },
/* pmc17  */{ .type = PFM_REG_W,
	      .desc = "PEBS_ENABLE",
	      .dfl_val = 0,
	      .rsvd_msk = 0xfffffffffffffffeULL,
	      .no_emul64_msk = 0,
	      .hw_addr = MSR_IA32_PEBS_ENABLE
	    }
};
#define PFM_ATOM_MAX_PMCS ARRAY_SIZE(pfm_intel_atom_pmc_desc)

#define PFM_ATOM_D(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "PMC"#n,			\
	  .rsvd_msk = PFM_ATOM_PMD_RSVD,	\
	  .hw_addr = MSR_P6_PERFCTR0+n,		\
	  .dep_pmcs[0] = 1ULL << n		\
	}

#define PFM_ATOM_FD(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "FIXED_CTR"#n,		\
	  .rsvd_msk = PFM_ATOM_PMD_RSVD,	\
	  .hw_addr = MSR_CORE_PERF_FIXED_CTR0+n,\
	  .dep_pmcs[0] = 1ULL << 16		\
	}

static struct pfm_regmap_desc pfm_intel_atom_pmd_desc[] = {
/* pmd0  */ PFM_ATOM_D(0),
/* pmd1  */ PFM_ATOM_D(1),
/* pmd2  */ PMX_NA,
/* pmd3  */ PMX_NA,
/* pmd4  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmd8  */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmd12 */ PMX_NA, PMX_NA, PMX_NA, PMX_NA,
/* pmd16 */ PFM_ATOM_FD(0),
/* pmd17 */ PFM_ATOM_FD(1),
/* pmd18 */ PFM_ATOM_FD(2)
};
#define PFM_ATOM_MAX_PMDS ARRAY_SIZE(pfm_intel_atom_pmd_desc)

static struct pfm_pmu_config pfm_intel_atom_pmu_conf;

static int pfm_intel_atom_probe_pmu(void)
{
	if (force)
		goto doit;

	if (current_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return -1;

	if (current_cpu_data.x86 != 6)
		return -1;

	if (current_cpu_data.x86_model != 28)
		return -1;
doit:
	/*
	 * having APIC is mandatory, so disregard force option
	 */
	if (!cpu_has_apic) {
		PFM_INFO("no Local APIC, try rebooting with lapic option");
		return -1;
	}

	PFM_INFO("detected Intel Atom PMU");

	if (force_nmi)
		pfm_intel_atom_pmu_info.flags |= PFM_X86_FL_USE_NMI;

	return 0;
}

/**
 * pfm_intel_atom_has_ovfls - check for pending overflow condition
 * @ctx: context to work on
 *
 * detect if counters have overflowed.
 * return:
 * 	0 : no overflow
 * 	1 : at least one overflow
 */
static int __kprobes pfm_intel_atom_has_ovfls(struct pfm_context *ctx)
{
	struct pfm_regmap_desc *d;
	u64 ovf;

	d = pfm_pmu_conf->pmd_desc;
	/*
 	 * read global overflow status register
 	 * if sharing PMU, then not all bit are ours so must
 	 * check only the ones we actually use
 	 */
	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, ovf);

	/*
 	 * for pmd0, we also check PEBS overflow on bit 62
 	 */
	if ((d[0].type & PFM_REG_I) && (ovf & ((1ull << 62) | 1ull)))
		return 1;

	if ((d[1].type & PFM_REG_I) && (ovf & 2ull))
		return 1;

	if ((d[16].type & PFM_REG_I) && (ovf & (1ull << 32)))
		return 1;

	if ((d[17].type & PFM_REG_I) && (ovf & (2ull << 32)))
		return 1;

	if ((d[18].type & PFM_REG_I) && (ovf & (4ull << 32)))
		return 1;

	return 0;
}

/**
 * pfm_intel_atom_stop_save - stop monitoring, collect pending overflow, save pmds
 * @ctx: context to work on
 * @set: active set
 *
 * return:
 * 	1: caller needs to save pmds
 * 	0: caller does not need to save pmds, they have been saved by this call
 */
static int pfm_intel_atom_stop_save(struct pfm_context *ctx,
				    struct pfm_event_set *set)
{
#define PFM_ATOM_WMASK	(1ULL << 31)
#define PFM_ATOM_OMASK	((1ULL << 31)-1)
	u64 clear_ovf = 0;
	u64 ovf, ovf2, val;

	/*
	 * read global overflow status register
	 * if sharing PMU, then not all bit are ours so must
	 * check only the ones we actually use.
	 *
	 * XXX: Atom seems to have a bug with the stickyness of
	 *      GLOBAL_STATUS. If we read GLOBAL_STATUS after we
	 *      clear the generic counters, then their bits in
	 *      GLOBAL_STATUS are cleared. This should not be the
	 *      case accoding to architected PMU. To workaround
	 *      the problem, we read GLOBAL_STATUS BEFORE we stop
	 *      all monitoring.
	 */
	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, ovf);

	/*
	 * stop monitoring
	 */
	if (test_bit(0, cast_ulp(set->used_pmcs)))
		wrmsrl(MSR_P6_EVNTSEL0, 0);

	if (test_bit(1, cast_ulp(set->used_pmcs)))
		wrmsrl(MSR_P6_EVNTSEL1, 0);

	if (test_bit(16, cast_ulp(set->used_pmcs)))
		wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, 0);

	if (test_bit(17, cast_ulp(set->used_pmcs)))
		wrmsrl(MSR_IA32_PEBS_ENABLE, 0);

	/*
	 * XXX: related to bug mentioned above
	 *
	 * read GLOBAL_STATUS again to avoid race condition
	 * with overflows happening after first read and
	 * before stop. That avoids missing overflows on
	 * the fixed counters and PEBS
	 */
	rdmsrl(MSR_CORE_PERF_GLOBAL_STATUS, ovf2);
	ovf |= ovf2;

	/*
	 * if we already have a pending overflow condition, we simply
	 * return to take care of it first.
	 */
	if (set->npend_ovfls)
		return 1;

	/*
 	 * check PMD 0,1,16,17,18 for overflow and save their value
 	 */
	if (test_bit(0, cast_ulp(set->used_pmds))) {
		rdmsrl(MSR_P6_PERFCTR0, val);
		if (ovf & ((1ull<<62)|1ull)) {
			__set_bit(0, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
			clear_ovf = (1ull << 62) | 1ull;
		}
		val = (set->pmds[0].value & ~PFM_ATOM_OMASK)
		    | (val & PFM_ATOM_OMASK);
		set->pmds[0].value = val;
	}

	if (test_bit(1, cast_ulp(set->used_pmds))) {
		rdmsrl(MSR_P6_PERFCTR1, val);
		 if (ovf & 2ull) {
			__set_bit(1, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
			clear_ovf |= 2ull;
		}
		val = (set->pmds[1].value & ~PFM_ATOM_OMASK)
		    | (val & PFM_ATOM_OMASK);
		set->pmds[1].value = val;
	}

	if (test_bit(16, cast_ulp(set->used_pmds))) {
		rdmsrl(MSR_CORE_PERF_FIXED_CTR0, val);
 		if (ovf & (1ull << 32)) {
			__set_bit(16, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
			clear_ovf |= 1ull << 32;
		}
		val = (set->pmds[16].value & ~PFM_ATOM_OMASK)
		    | (val & PFM_ATOM_OMASK);
		set->pmds[16].value = val;
	}

	if (test_bit(17, cast_ulp(set->used_pmds))) {
		rdmsrl(MSR_CORE_PERF_FIXED_CTR0+1, val);
 		if (ovf & (2ull << 32)) {
			__set_bit(17, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
			clear_ovf |= 2ull << 32;
		}
		val = (set->pmds[17].value & ~PFM_ATOM_OMASK)
		    | (val & PFM_ATOM_OMASK);
		set->pmds[17].value = val;
	}

	if (test_bit(18, cast_ulp(set->used_pmds))) {
		rdmsrl(MSR_CORE_PERF_FIXED_CTR0+2, val);
 		if (ovf & (4ull << 32)) {
			__set_bit(18, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
			clear_ovf |= 4ull << 32;
		}
		val = (set->pmds[18].value & ~PFM_ATOM_OMASK)
		    | (val & PFM_ATOM_OMASK);
		set->pmds[18].value = val;
	}

	if (clear_ovf)
		wrmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, clear_ovf);

	/* 0 means: no need to save PMDs at upper level */
	return 0;
}

/**
 * pfm_intel_atom_quiesce - stop monitoring without grabbing any lock
 *
 * called from NMI interrupt handler to immediately stop monitoring
 * cannot grab any lock, including perfmon related locks
 */
static void __kprobes pfm_intel_atom_quiesce(void)
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
 * pfm_intel_atom_restore_pmcs - reload PMC registers
 * @ctx: context to restore from
 * @set: current event set
 *
 * restores pmcs and also PEBS Data Save area pointer
 */
static void pfm_intel_atom_restore_pmcs(struct pfm_context *ctx,
					struct pfm_event_set *set)
{
	struct pfm_arch_context *ctx_arch;
	u64 clear_ovf = 0;

	ctx_arch = pfm_ctx_arch(ctx);
	/*
	 * must restore DS pointer before restoring PMCs
	 * as this can potentially reactivate monitoring
	 */
	if (ctx_arch->flags.use_ds)
		wrmsrl(MSR_IA32_DS_AREA, (unsigned long)ctx_arch->ds_area);

	if (test_bit(0, cast_ulp(set->used_pmcs))) {
		wrmsrl(MSR_P6_EVNTSEL0, set->pmcs[0]);
		clear_ovf = 1ull;
	}

	if (test_bit(1, cast_ulp(set->used_pmcs))) {
		wrmsrl(MSR_P6_EVNTSEL1, set->pmcs[1]);
		clear_ovf |= 2ull;
	}

	if (test_bit(16, cast_ulp(set->used_pmcs))) {
		wrmsrl(MSR_CORE_PERF_FIXED_CTR_CTRL, set->pmcs[16]);
		clear_ovf |= 7ull << 32;
	}

	if (test_bit(17, cast_ulp(set->used_pmcs))) {
		wrmsrl(MSR_IA32_PEBS_ENABLE, set->pmcs[17]);
		clear_ovf |= 1ull << 62;
	}

	if (clear_ovf)
		wrmsrl(MSR_CORE_PERF_GLOBAL_OVF_CTRL, clear_ovf);
}

static int pfm_intel_atom_pmc17_check(struct pfm_context *ctx,
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

DEFINE_PER_CPU(u64, saved_global_ctrl);

/**
 * pfm_intel_atom_acquire_pmu_percpu - acquire PMU resource per CPU
 *
 * For Atom, it is necessary to enable all available
 * registers. The firmware rightfully has the fixed counters
 * disabled for backward compatibility with architectural perfmon
 * v1
 *
 * This function is invoked on each online CPU
 */
static void pfm_intel_atom_acquire_pmu_percpu(void)
{
	struct pfm_regmap_desc *d;
	u64 mask = 0;
	unsigned int i;

	/*
 	 * build bitmask of registers that are available to
 	 * us. In some cases, there may be fewer registers than
 	 * what Atom supports due to sharing with other kernel
 	 * subsystems, such as NMI
 	 */
	d = pfm_pmu_conf->pmd_desc;
	for (i=0; i < 16; i++) {
		if ((d[i].type & PFM_REG_I) == 0)
			continue;
		mask |= 1ull << i;
	}
	for (i=16; i < PFM_ATOM_MAX_PMDS; i++) {
		if ((d[i].type & PFM_REG_I) == 0)
			continue;
		mask |= 1ull << (32+i-16);
	}

	/*
	 * keep a local copy of the current MSR_CORE_PERF_GLOBAL_CTRL
	 */
	rdmsrl(MSR_CORE_PERF_GLOBAL_CTRL, __get_cpu_var(saved_global_ctrl));

	PFM_DBG("global=0x%llx set to 0x%llx",
		__get_cpu_var(saved_global_ctrl),
		mask);

	/*
	 * enable all registers
	 *
	 * No need to quiesce PMU. If there is a overflow, it will be
	 * treated as spurious by the handler
	 */
	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, mask);
}

/**
 * pfm_intel_atom_release_pmu_percpu - release PMU resource per CPU
 *
 * For Atom, we restore MSR_CORE_PERF_GLOBAL_CTRL to its orginal value
 */
static void pfm_intel_atom_release_pmu_percpu(void)
{
	PFM_DBG("global_ctrl restored to 0x%llx\n",
			__get_cpu_var(saved_global_ctrl));

	wrmsrl(MSR_CORE_PERF_GLOBAL_CTRL, __get_cpu_var(saved_global_ctrl));
}

static struct pfm_pmu_config pfm_intel_atom_pmu_conf = {
	.pmu_name = "Intel Atom",
	.pmd_desc = pfm_intel_atom_pmd_desc,
	.counter_width   = PFM_ATOM_PMD_WIDTH,
	.num_pmc_entries = PFM_ATOM_MAX_PMCS,
	.num_pmd_entries = PFM_ATOM_MAX_PMDS,
	.pmc_desc = pfm_intel_atom_pmc_desc,
	.probe_pmu = pfm_intel_atom_probe_pmu,
	.version = "1.0",
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmc_write_check = pfm_intel_atom_pmc17_check,
	.pmu_info = &pfm_intel_atom_pmu_info
};

static int __init pfm_intel_atom_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_intel_atom_pmu_conf);
}

static void __exit pfm_intel_atom_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_intel_atom_pmu_conf);
}

module_init(pfm_intel_atom_pmu_init_module);
module_exit(pfm_intel_atom_pmu_cleanup_module);
