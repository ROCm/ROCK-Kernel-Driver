/*
 * This file contains the PMU description for the Athlon64 and Opteron64
 * processors. It supports 32 and 64-bit modes.
 *
 * Copyright (c) 2005-2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * Copyright (c) 2007 Advanced Micro Devices, Inc.
 * Contributed by Robert Richter <robert.richter@amd.com>
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
#include <linux/vmalloc.h>
#include <linux/topology.h>
#include <linux/kprobes.h>
#include <linux/pci.h>
#include <linux/perfmon_kern.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_AUTHOR("Robert Richter <robert.richter@amd.com>");
MODULE_DESCRIPTION("AMD64 PMU description table");
MODULE_LICENSE("GPL");

#define PCI_DEVICE_ID_AMD_10H_NB_MISC	0x1203

static int force_nmi;
MODULE_PARM_DESC(force_nmi, "bool: force use of NMI for PMU interrupt");
module_param(force_nmi, bool, 0600);

#define HAS_IBS		0x01	/* has IBS  support */

static	u8  ibs_eilvt_off, ibs_status;	/* AMD: extended interrupt LVT offset */

static void pfm_amd64_restore_pmcs(struct pfm_context *ctx,
				   struct pfm_event_set *set);
static void __kprobes pfm_amd64_quiesce(void);
static int pfm_amd64_has_ovfls(struct pfm_context *ctx);
static int pfm_amd64_stop_save(struct pfm_context *ctx,
			       struct pfm_event_set *set);

#define IBSFETCHCTL_PMC	4 /* pmc4 */
#define IBSFETCHCTL_PMD	4 /* pmd4 */
#define IBSOPSCTL_PMC	5 /* pmc5 */
#define IBSOPSCTL_PMD	7 /* pmd7 */

static u64 enable_mask[PFM_MAX_PMCS];
static u16 max_enable;

static struct pfm_arch_pmu_info pfm_amd64_pmu_info = {
	.stop_save = pfm_amd64_stop_save,
	.has_ovfls = pfm_amd64_has_ovfls,
	.quiesce = pfm_amd64_quiesce,
	.restore_pmcs = pfm_amd64_restore_pmcs
};

#define PFM_AMD64_IBSFETCHVAL	(1ULL<<49) /* valid fetch sample */
#define PFM_AMD64_IBSFETCHEN	(1ULL<<48) /* fetch sampling enabled */
#define PFM_AMD64_IBSOPVAL	(1ULL<<18) /* valid execution sample */
#define PFM_AMD64_IBSOPEN	(1ULL<<17) /* execution sampling enabled */

/*
 * force Local APIC interrupt on overflow
 */
#define PFM_K8_VAL	(1ULL<<20)
#define PFM_K8_NO64	(1ULL<<20)

/*
 * reserved bits must be 1
 *
 * for family 15:
 * - upper 32 bits are reserved
 * - bit 20, bit 21
 *
 * for family 16:
 * - bits 36-39 are reserved
 * - bits 42-63 are reserved
 * - bit 20, bit 21
 *
 * for IBS registers:
 * 	IBSFETCHCTL: all bits are reserved except bits 57, 48, 15:0
 * 	IBSOPSCTL  : all bits are reserved except bits 17, 15:0
 */
#define PFM_K8_RSVD 	((~((1ULL<<32)-1)) | (1ULL<<20) | (1ULL<<21))
#define PFM_16_RSVD ((0x3fffffULL<<42) | (0xfULL<<36) | (1ULL<<20) | (1ULL<<21))
#define PFM_AMD64_IBSFETCHCTL_RSVD	(~((1ULL<<48)|(1ULL<<57)|0xffffULL))
#define PFM_AMD64_IBSOPCTL_RSVD		(~((1ULL<<17)|0xffffULL))

static struct pfm_regmap_desc pfm_amd64_pmc_desc[] = {
/* pmc0  */ PMC_D(PFM_REG_I64, "PERFSEL0", PFM_K8_VAL, PFM_K8_RSVD, PFM_K8_NO64, MSR_K7_EVNTSEL0),
/* pmc1  */ PMC_D(PFM_REG_I64, "PERFSEL1", PFM_K8_VAL, PFM_K8_RSVD, PFM_K8_NO64, MSR_K7_EVNTSEL1),
/* pmc2  */ PMC_D(PFM_REG_I64, "PERFSEL2", PFM_K8_VAL, PFM_K8_RSVD, PFM_K8_NO64, MSR_K7_EVNTSEL2),
/* pmc3  */ PMC_D(PFM_REG_I64, "PERFSEL3", PFM_K8_VAL, PFM_K8_RSVD, PFM_K8_NO64, MSR_K7_EVNTSEL3),
/* pmc4  */ PMC_D(PFM_REG_I,   "IBSFETCHCTL", 0, PFM_AMD64_IBSFETCHCTL_RSVD, 0, MSR_AMD64_IBSFETCHCTL),
/* pmc5  */ PMC_D(PFM_REG_I,   "IBSOPCTL",    0, PFM_AMD64_IBSOPCTL_RSVD,    0, MSR_AMD64_IBSOPCTL),
};
#define PFM_AMD_NUM_PMCS ARRAY_SIZE(pfm_amd64_pmc_desc)

#define PFM_REG_IBS (PFM_REG_I|PFM_REG_INTR)

/*
 * AMD64 counters are 48 bits, upper bits are reserved
 */
#define PFM_AMD64_CTR_RSVD	(~((1ULL<<48)-1))

#define PFM_AMD_D(n) \
	{ .type = PFM_REG_C,			\
	  .desc = "PERFCTR"#n,			\
	  .hw_addr = MSR_K7_PERFCTR0+n,		\
	  .rsvd_msk = PFM_AMD64_CTR_RSVD,	\
	  .dep_pmcs[0] = 1ULL << n		\
	}

#define PFM_AMD_IBSO(t, s, a) \
	{ .type = t,			\
	  .desc = s,			\
	  .hw_addr = a,			\
	  .rsvd_msk = 0,		\
	  .dep_pmcs[0] = 1ULL << 5	\
	}

#define PFM_AMD_IBSF(t, s, a) \
	{ .type = t,			\
	  .desc = s,			\
	  .hw_addr = a,			\
	  .rsvd_msk = 0,		\
	  .dep_pmcs[0] = 1ULL << 6	\
	}

static struct pfm_regmap_desc pfm_amd64_pmd_desc[] = {
/* pmd0  */ PFM_AMD_D(0),
/* pmd1  */ PFM_AMD_D(1),
/* pmd2  */ PFM_AMD_D(2),
/* pmd3  */ PFM_AMD_D(3),
/* pmd4  */ PFM_AMD_IBSF(PFM_REG_IBS, "IBSFETCHCTL",	MSR_AMD64_IBSFETCHCTL),
/* pmd5  */ PFM_AMD_IBSF(PFM_REG_IRO, "IBSFETCHLINAD",	MSR_AMD64_IBSFETCHLINAD),
/* pmd6  */ PFM_AMD_IBSF(PFM_REG_IRO, "IBSFETCHPHYSAD", MSR_AMD64_IBSFETCHPHYSAD),
/* pmd7  */ PFM_AMD_IBSO(PFM_REG_IBS, "IBSOPCTL",	MSR_AMD64_IBSOPCTL),
/* pmd8  */ PFM_AMD_IBSO(PFM_REG_IRO, "IBSOPRIP",	MSR_AMD64_IBSOPRIP),
/* pmd9  */ PFM_AMD_IBSO(PFM_REG_IRO, "IBSOPDATA",	MSR_AMD64_IBSOPDATA),
/* pmd10 */ PFM_AMD_IBSO(PFM_REG_IRO, "IBSOPDATA2",	MSR_AMD64_IBSOPDATA2),
/* pmd11 */ PFM_AMD_IBSO(PFM_REG_IRO, "IBSOPDATA3",	MSR_AMD64_IBSOPDATA3),
/* pmd12 */ PFM_AMD_IBSO(PFM_REG_IRO, "IBSDCLINAD",	MSR_AMD64_IBSDCLINAD),
/* pmd13 */ PFM_AMD_IBSO(PFM_REG_IRO, "IBSDCPHYSAD",	MSR_AMD64_IBSDCPHYSAD),
};
#define PFM_AMD_NUM_PMDS ARRAY_SIZE(pfm_amd64_pmd_desc)

static struct pfm_context **pfm_nb_sys_owners;
static struct pfm_context *pfm_nb_task_owner;

static struct pfm_pmu_config pfm_amd64_pmu_conf;

#define is_ibs_pmc(x) (x == 4 || x == 5)

static void pfm_amd64_setup_eilvt_per_cpu(void *info)
{
	u8 lvt_off;

	/* program the IBS vector to the perfmon vector */
	lvt_off =  setup_APIC_eilvt_ibs(LOCAL_PERFMON_VECTOR,
					APIC_EILVT_MSG_FIX, 0);
	PFM_DBG("APIC_EILVT%d set to 0x%x", lvt_off, LOCAL_PERFMON_VECTOR);
	ibs_eilvt_off = lvt_off;
}

static int pfm_amd64_setup_eilvt(void)
{
#define IBSCTL_LVTOFFSETVAL		(1 << 8)
#define IBSCTL				0x1cc
	struct pci_dev *cpu_cfg;
	int nodes;
	u32 value = 0;

	/* per CPU setup */
	on_each_cpu(pfm_amd64_setup_eilvt_per_cpu, NULL, 1);

	nodes = 0;
	cpu_cfg = NULL;
	do {
		cpu_cfg = pci_get_device(PCI_VENDOR_ID_AMD,
					 PCI_DEVICE_ID_AMD_10H_NB_MISC,
					 cpu_cfg);
		if (!cpu_cfg)
			break;
		++nodes;
		pci_write_config_dword(cpu_cfg, IBSCTL, ibs_eilvt_off
				       | IBSCTL_LVTOFFSETVAL);
		pci_read_config_dword(cpu_cfg, IBSCTL, &value);
		if (value != (ibs_eilvt_off | IBSCTL_LVTOFFSETVAL)) {
			PFM_DBG("Failed to setup IBS LVT offset, "
				"IBSCTL = 0x%08x", value);
			return 1;
		}
	} while (1);

	if (!nodes) {
		PFM_DBG("No CPU node configured for IBS");
		return 1;
	}

#ifdef CONFIG_NUMA
	/* Sanity check */
	/* Works only for 64bit with proper numa implementation. */
	if (nodes != num_possible_nodes()) {
		PFM_DBG("Failed to setup CPU node(s) for IBS, "
			"found: %d, expected %d",
			nodes, num_possible_nodes());
		return 1;
	}
#endif
	return 0;
}

/*
 * There can only be one user per socket for the Northbridge (NB) events,
 * so we enforce mutual exclusion as follows:
 * 	- per-thread : only one context machine-wide can use NB events
 * 	- system-wide: only one context per processor socket
 *
 * Exclusion is enforced at:
 * 	- pfm_load_context()
 * 	- pfm_write_pmcs() for attached contexts
 *
 * Exclusion is released at:
 * 	- pfm_unload_context() or any calls that implicitely uses it
 *
 * return:
 * 	0  : successfully acquire NB access
 * 	< 0:  errno, failed to acquire NB access
 */
static int pfm_amd64_acquire_nb(struct pfm_context *ctx)
{
	struct pfm_context **entry, *old;
	int proc_id;

#ifdef CONFIG_SMP
	proc_id = cpu_data(smp_processor_id()).phys_proc_id;
#else
	proc_id = 0;
#endif

	if (ctx->flags.system)
		entry = &pfm_nb_sys_owners[proc_id];
	else
		entry = &pfm_nb_task_owner;

	old = cmpxchg(entry, NULL, ctx);
	if (!old) {
		if (ctx->flags.system)
			PFM_DBG("acquired Northbridge event access on socket %u", proc_id);
		else
			PFM_DBG("acquired Northbridge event access globally");
	} else if (old != ctx) {
		if (ctx->flags.system)
			PFM_DBG("NorthBridge event conflict on socket %u", proc_id);
		else
			PFM_DBG("global NorthBridge event conflict");
		return -EBUSY;
	}
	return 0;
}

/*
 * invoked from pfm_write_pmcs() when pfm_nb_sys_owners is not NULL,i.e.,
 * when we have detected a multi-core processor.
 *
 * context is locked, interrupts are masked
 */
static int pfm_amd64_pmc_write_check(struct pfm_context *ctx,
			     struct pfm_event_set *set,
			     struct pfarg_pmc *req)
{
	unsigned int event;

	/*
	 * delay checking NB event until we load the context
	 */
	if (ctx->state == PFM_CTX_UNLOADED)
		return 0;

	/*
	 * check event is NB event
	 */
	event = (unsigned int)(req->reg_value & 0xff);
	if (event < 0xee)
		return 0;

	return pfm_amd64_acquire_nb(ctx);
}

/*
 * invoked on pfm_load_context().
 * context is locked, interrupts are masked
 */
static int pfm_amd64_load_context(struct pfm_context *ctx)
{
	struct pfm_event_set *set;
	unsigned int i, n;

	/*
	 * scan all sets for NB events
	 */
	list_for_each_entry(set, &ctx->set_list, list) {
		n = set->nused_pmcs;
		for (i = 0; n; i++) {
			if (!test_bit(i, cast_ulp(set->used_pmcs)))
				continue;

			if (!is_ibs_pmc(i) && (set->pmcs[i] & 0xff) >= 0xee)
				goto found;
			n--;
		}
	}
	return 0;
found:
	return pfm_amd64_acquire_nb(ctx);
}

/*
 * invoked on pfm_unload_context()
 */
static void pfm_amd64_unload_context(struct pfm_context *ctx)
{
	struct pfm_context **entry, *old;
	int proc_id;

#ifdef CONFIG_SMP
	proc_id = cpu_data(smp_processor_id()).phys_proc_id;
#else
	proc_id = 0;
#endif

	/*
	 * unload always happens on the monitored CPU in system-wide
	 */
	if (ctx->flags.system)
		entry = &pfm_nb_sys_owners[proc_id];
	else
		entry = &pfm_nb_task_owner;

	old = cmpxchg(entry, ctx, NULL);
	if (old == ctx) {
		if (ctx->flags.system)
			PFM_DBG("released NorthBridge on socket %u", proc_id);
		else
			PFM_DBG("released NorthBridge events globally");
	}
}

/*
 * detect if we need to activate NorthBridge event access control
 */
static int pfm_amd64_setup_nb_event_control(void)
{
	unsigned int c, n = 0;
	unsigned int max_phys = 0;

#ifdef CONFIG_SMP
	for_each_possible_cpu(c) {
		if (cpu_data(c).phys_proc_id > max_phys)
			max_phys = cpu_data(c).phys_proc_id;
	}
#else
	max_phys = 0;
#endif
	if (max_phys > 255) {
		PFM_INFO("socket id %d is too big to handle", max_phys);
		return -ENOMEM;
	}

	n = max_phys + 1;
	if (n < 2)
		return 0;

	pfm_nb_sys_owners = vmalloc(n * sizeof(*pfm_nb_sys_owners));
	if (!pfm_nb_sys_owners)
		return -ENOMEM;

	memset(pfm_nb_sys_owners, 0, n * sizeof(*pfm_nb_sys_owners));
	pfm_nb_task_owner = NULL;

	/*
	 * activate write-checker for PMC registers
	 */
	for (c = 0; c < PFM_AMD_NUM_PMCS; c++) {
		if (!is_ibs_pmc(c))
			pfm_amd64_pmc_desc[c].type |= PFM_REG_WC;
	}

	pfm_amd64_pmu_info.load_context = pfm_amd64_load_context;
	pfm_amd64_pmu_info.unload_context = pfm_amd64_unload_context;

	pfm_amd64_pmu_conf.pmc_write_check = pfm_amd64_pmc_write_check;

	PFM_INFO("NorthBridge event access control enabled");

	return 0;
}

/*
 * disable registers which are not available on
 * the host (applies to IBS registers)
 */
static void pfm_amd64_check_registers(void)
{
	u16 i;

	PFM_DBG("has_ibs=%d", !!(ibs_status & HAS_IBS));

	__set_bit(0, cast_ulp(enable_mask));
	__set_bit(1, cast_ulp(enable_mask));
	__set_bit(2, cast_ulp(enable_mask));
	__set_bit(3, cast_ulp(enable_mask));
	max_enable = 3+1;


	/*
	 * remove IBS registers if feature not present
	 */
	if (!(ibs_status & HAS_IBS)) {
		pfm_amd64_pmc_desc[4].type = PFM_REG_NA;
		pfm_amd64_pmc_desc[5].type = PFM_REG_NA;
		for (i = 4; i < 14; i++)
			pfm_amd64_pmd_desc[i].type = PFM_REG_NA;
	} else {
		__set_bit(16, cast_ulp(enable_mask));
		__set_bit(17, cast_ulp(enable_mask));
		max_enable = 17 + 1;
	}

	/*
	 * adjust reserved bit fields for family 16
	 */
	if (current_cpu_data.x86 == 16) {
		for (i = 0; i < PFM_AMD_NUM_PMCS; i++)
			if (pfm_amd64_pmc_desc[i].rsvd_msk == PFM_K8_RSVD)
				pfm_amd64_pmc_desc[i].rsvd_msk = PFM_16_RSVD;
	}
}

static int pfm_amd64_probe_pmu(void)
{
	u64 val = 0;
	if (current_cpu_data.x86_vendor != X86_VENDOR_AMD) {
		PFM_INFO("not an AMD processor");
		return -1;
	}

	switch (current_cpu_data.x86) {
	case 16:
	case 15:
	case  6:
		break;
	default:
		PFM_INFO("unsupported family=%d", current_cpu_data.x86);
		return -1;
	}

	/* check for IBS */
	if (cpu_has(&current_cpu_data, X86_FEATURE_IBS)) {
		ibs_status |= HAS_IBS;
		rdmsrl(MSR_AMD64_IBSCTL, val);
	}

	PFM_INFO("found family=%d IBSCTL=0x%llx", current_cpu_data.x86, (unsigned long long)val);

	/*
	 * check for local APIC (required)
	 */
	if (!cpu_has_apic) {
		PFM_INFO("no local APIC, unsupported");
		return -1;
	}

	if (current_cpu_data.x86_max_cores > 1
	    && pfm_amd64_setup_nb_event_control())
		return -1;

	if (force_nmi)
		pfm_amd64_pmu_info.flags |= PFM_X86_FL_USE_NMI;

	if (ibs_status & HAS_IBS) {
		/* Setup extended interrupt */
		if (pfm_amd64_setup_eilvt()) {
			PFM_INFO("Failed to initialize extended interrupts "
				 "for IBS");
			ibs_status  &= ~HAS_IBS;
			PFM_INFO("Unable to use IBS");
		} else {
			PFM_INFO("IBS supported");
		}
	}

	pfm_amd64_check_registers();

	return 0;
}

/*
 * detect is counters have overflowed.
 * return:
 * 	0 : no overflow
 * 	1 : at least one overflow
 */
static int __kprobes pfm_amd64_has_ovfls(struct pfm_context *ctx)
{
	struct pfm_regmap_desc *xrd;
	u64 *cnt_mask;
	u64 wmask, val;
	u16 i, num;

	/*
	 * Check for IBS events
	 */
	if (ibs_status & HAS_IBS) {
		rdmsrl(MSR_AMD64_IBSFETCHCTL, val);
		if (val & PFM_AMD64_IBSFETCHVAL)
			return 1;
		rdmsrl(MSR_AMD64_IBSOPCTL, val);
		if (val & PFM_AMD64_IBSOPVAL)
			return 1;
	}
	/*
	 * Check regular counters
	 */
	cnt_mask = ctx->regs.cnt_pmds;
	num = ctx->regs.num_counters;
	wmask = 1ULL << pfm_pmu_conf->counter_width;
	xrd = pfm_amd64_pmd_desc;

	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(cnt_mask))) {
			rdmsrl(xrd[i].hw_addr, val);
			if (!(val & wmask))
				return 1;
			num--;
		}
	}
	return 0;
}

/*
 * Must check for IBS event BEFORE stop_save_p6 because
 * stopping monitoring does destroy IBS state information
 * in IBSFETCHCTL/IBSOPCTL because they are tagged as enable
 * registers.
 */
static int pfm_amd64_stop_save(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *pmu_info;
	u64 used_mask[PFM_PMC_BV];
	u64 *cnt_pmds;
	u64 val, wmask, ovfl_mask;
	u32 i, count, use_ibs;

	pmu_info = pfm_pmu_info();

	/*
	 * IBS used if:
	 *   - on family 10h processor with IBS
	 *   - at least one of the IBS PMD registers is used
	 */
	use_ibs = (ibs_status & HAS_IBS)
		&& (test_bit(IBSFETCHCTL_PMD, cast_ulp(set->used_pmds))
		    || test_bit(IBSOPSCTL_PMD, cast_ulp(set->used_pmds)));

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
	 *
	 * With IBS, we need to do read-modify-write to preserve the content
	 * for OpsCTL and FetchCTL because they are also used as PMDs and saved
	 * below
	 */
	if (use_ibs) {
		for (i = 0; count; i++) {
			if (test_bit(i, cast_ulp(used_mask))) {
				if (i == IBSFETCHCTL_PMC) {
					rdmsrl(pfm_pmu_conf->pmc_desc[i].hw_addr, val);
					val &= ~PFM_AMD64_IBSFETCHEN;
				} else if (i == IBSOPSCTL_PMC) {
					rdmsrl(pfm_pmu_conf->pmc_desc[i].hw_addr, val);
					val &= ~PFM_AMD64_IBSOPEN;
				} else
					val = 0;
				wrmsrl(pfm_pmu_conf->pmc_desc[i].hw_addr, val);
				count--;
			}
		}
	} else {
		for (i = 0; count; i++) {
			if (test_bit(i, cast_ulp(used_mask))) {
				wrmsrl(pfm_pmu_conf->pmc_desc[i].hw_addr, 0);
				count--;
			}
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
	 * Must check for counting PMDs because of virtual PMDs and IBS
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

	/*
	 * check if IBS contains valid data, and mark the corresponding
	 * PMD has overflowed
	 */
	if (use_ibs) {
		if (set->pmds[IBSFETCHCTL_PMD].value & PFM_AMD64_IBSFETCHVAL) {
			__set_bit(IBSFETCHCTL_PMD, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
		}
		if (set->pmds[IBSOPSCTL_PMD].value & PFM_AMD64_IBSOPVAL) {
			__set_bit(IBSOPSCTL_PMD, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
		}
	}
	/* 0 means: no need to save PMDs at upper level */
	return 0;
}

/**
 * pfm_amd64_quiesce_pmu -- stop monitoring without grabbing any lock
 *
 * called from NMI interrupt handler to immediately stop monitoring
 * cannot grab any lock, including perfmon related locks
 */
static void __kprobes pfm_amd64_quiesce(void)
{
	/*
	 * quiesce PMU by clearing available registers that have
	 * the start/stop capability
	 */
	if (test_bit(0, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_K7_EVNTSEL0, 0);
	if (test_bit(1, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_K7_EVNTSEL0+1, 0);
	if (test_bit(2, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_K7_EVNTSEL0+2, 0);
	if (test_bit(3, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_K7_EVNTSEL0+3, 0);

	if (test_bit(4, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_AMD64_IBSFETCHCTL, 0);
	if (test_bit(5, cast_ulp(pfm_pmu_conf->regs_all.pmcs)))
		wrmsrl(MSR_AMD64_IBSOPCTL, 0);
}

/**
 * pfm_amd64_restore_pmcs - reload PMC registers
 * @ctx: context to restore from
 * @set: current event set
 *
 * optimized version of pfm_arch_restore_pmcs(). On AMD64, we can
 * afford to only restore the pmcs registers we use, because they are
 * all independent from each other.
 */
static void pfm_amd64_restore_pmcs(struct pfm_context *ctx,
				   struct pfm_event_set *set)
{
	u64 *mask;
	u16 i, num;

	mask = set->used_pmcs;
	num = set->nused_pmcs;
	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(mask))) {
			wrmsrl(pfm_amd64_pmc_desc[i].hw_addr, set->pmcs[i]);
			num--;
		}
	}
}

static struct pfm_pmu_config pfm_amd64_pmu_conf = {
	.pmu_name = "AMD64",
	.counter_width = 47,
	.pmd_desc = pfm_amd64_pmd_desc,
	.pmc_desc = pfm_amd64_pmc_desc,
	.num_pmc_entries = PFM_AMD_NUM_PMCS,
	.num_pmd_entries = PFM_AMD_NUM_PMDS,
	.probe_pmu = pfm_amd64_probe_pmu,
	.version = "1.2",
	.pmu_info = &pfm_amd64_pmu_info,
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
};

static int __init pfm_amd64_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_amd64_pmu_conf);
}

static void __exit pfm_amd64_pmu_cleanup_module(void)
{
	if (pfm_nb_sys_owners)
		vfree(pfm_nb_sys_owners);

	pfm_pmu_unregister(&pfm_amd64_pmu_conf);
}

module_init(pfm_amd64_pmu_init_module);
module_exit(pfm_amd64_pmu_cleanup_module);
