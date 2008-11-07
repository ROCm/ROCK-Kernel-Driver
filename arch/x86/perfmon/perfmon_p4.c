/*
 * This file contains the P4/Xeon PMU register description tables
 * for both 32 and 64 bit modes.
 *
 * Copyright (c) 2005 Intel Corporation
 * Contributed by Bryan Wilkerson <bryan.p.wilkerson@intel.com>
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
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <asm/msr.h>
#include <asm/apic.h>

MODULE_AUTHOR("Bryan Wilkerson <bryan.p.wilkerson@intel.com>");
MODULE_DESCRIPTION("P4/Xeon/EM64T PMU description table");
MODULE_LICENSE("GPL");

static int force;
MODULE_PARM_DESC(force, "bool: force module to load succesfully");
module_param(force, bool, 0600);

static int force_nmi;
MODULE_PARM_DESC(force_nmi, "bool: force use of NMI for PMU interrupt");
module_param(force_nmi, bool, 0600);

/*
 * For extended register information in addition to address that is used
 * at runtime to figure out the mapping of reg addresses to logical procs
 * and association of registers to hardware specific features
 */
struct pfm_p4_regmap {
	/*
	 * one each for the logical CPUs.  Index 0 corresponds to T0 and
	 * index 1 corresponds to T1.  Index 1 can be zero if no T1
	 * complement reg exists.
	 */
	unsigned long addrs[2]; /* 2 = number of threads */
	unsigned int ctr;	/* for CCCR/PERFEVTSEL, associated counter */
	unsigned int reg_type;
};

/*
 * bitmask for pfm_p4_regmap.reg_type
 */
#define PFM_REGT_NA		0x0000	/* not available */
#define PFM_REGT_EN		0x0001	/* has enable bit (cleared on ctxsw) */
#define PFM_REGT_ESCR		0x0002	/* P4: ESCR */
#define PFM_REGT_CCCR		0x0004	/* P4: CCCR */
#define PFM_REGT_PEBS		0x0010	/* PEBS related */
#define PFM_REGT_NOHT		0x0020	/* unavailable with HT */
#define PFM_REGT_CTR		0x0040	/* counter */

/*
 * architecture specific context extension.
 * located at: (struct pfm_arch_context *)(ctx+1)
 */
struct pfm_arch_p4_context {
	u32	npend_ovfls;	/* P4 NMI #pending ovfls */
	u32	reserved;
	u64	povfl_pmds[PFM_PMD_BV]; /* P4 NMI overflowed counters */
	u64	saved_cccrs[PFM_MAX_PMCS];
};

/*
 * ESCR reserved bitmask:
 * - bits 31 - 63 reserved
 * - T1_OS and T1_USR bits are reserved - set depending on logical proc
 *      user mode application should use T0_OS and T0_USR to indicate
 * RSVD: reserved bits must be 1
 */
#define PFM_ESCR_RSVD  ~0x000000007ffffffcULL

/*
 * CCCR default value:
 * 	- OVF_PMI_T0=1 (bit 26)
 * 	- OVF_PMI_T1=0 (bit 27) (set if necessary in pfm_write_reg())
 * 	- all other bits are zero
 *
 * OVF_PMI is forced to zero if PFM_REGFL_NO_EMUL64 is set on CCCR
 */
#define PFM_CCCR_DFL	(1ULL<<26) | (3ULL<<16)

/*
 * CCCR reserved fields:
 * 	- bits 0-11, 25-29, 31-63
 * 	- OVF_PMI (26-27), override with REGFL_NO_EMUL64
 *
 * RSVD: reserved bits must be 1
 */
#define PFM_CCCR_RSVD     ~((0xfull<<12)  \
			| (0x7full<<18) \
			| (0x1ull<<30))

#define PFM_P4_NO64	(3ULL<<26) /* use 3 even in non HT mode */

#define PEBS_PMD	8  /* thread0: IQ_CTR4, thread1: IQ_CTR5 */

/*
 * With HyperThreading enabled:
 *
 *  The ESCRs and CCCRs are divided in half with the top half
 *  belonging to logical processor 0 and the bottom half going to
 *  logical processor 1. Thus only half of the PMU resources are
 *  accessible to applications.
 *
 *  PEBS is not available due to the fact that:
 *  	- MSR_PEBS_MATRIX_VERT is shared between the threads
 *      - IA32_PEBS_ENABLE is shared between the threads
 *
 * With HyperThreading disabled:
 *
 * The full set of PMU resources is exposed to applications.
 *
 * The mapping is chosen such that PMCxx -> MSR is the same
 * in HT and non HT mode, if register is present in HT mode.
 *
 */
#define PFM_REGT_NHTESCR (PFM_REGT_ESCR|PFM_REGT_NOHT)
#define PFM_REGT_NHTCCCR (PFM_REGT_CCCR|PFM_REGT_NOHT|PFM_REGT_EN)
#define PFM_REGT_NHTPEBS (PFM_REGT_PEBS|PFM_REGT_NOHT|PFM_REGT_EN)
#define PFM_REGT_NHTCTR  (PFM_REGT_CTR|PFM_REGT_NOHT)
#define PFM_REGT_ENAC    (PFM_REGT_CCCR|PFM_REGT_EN)

static void pfm_p4_write_pmc(struct pfm_context *ctx, unsigned int cnum, u64 value);
static void pfm_p4_write_pmd(struct pfm_context *ctx, unsigned int cnum, u64 value);
static u64 pfm_p4_read_pmd(struct pfm_context *ctx, unsigned int cnum);
static u64 pfm_p4_read_pmc(struct pfm_context *ctx, unsigned int cnum);
static int pfm_p4_create_context(struct pfm_context *ctx, u32 ctx_flags);
static void pfm_p4_free_context(struct pfm_context *ctx);
static int pfm_p4_has_ovfls(struct pfm_context *ctx);
static int pfm_p4_stop_save(struct pfm_context *ctx, struct pfm_event_set *set);
static void pfm_p4_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set);
static void pfm_p4_nmi_copy_state(struct pfm_context *ctx);
static void __kprobes pfm_p4_quiesce(void);

static u64 enable_mask[PFM_MAX_PMCS];
static u16 max_enable;

static struct pfm_p4_regmap pmc_addrs[PFM_MAX_PMCS] = {
	/*pmc 0 */    {{MSR_P4_BPU_ESCR0, MSR_P4_BPU_ESCR1}, 0, PFM_REGT_ESCR}, /*   BPU_ESCR0,1 */
	/*pmc 1 */    {{MSR_P4_IS_ESCR0, MSR_P4_IS_ESCR1}, 0, PFM_REGT_ESCR}, /*    IS_ESCR0,1 */
	/*pmc 2 */    {{MSR_P4_MOB_ESCR0, MSR_P4_MOB_ESCR1}, 0, PFM_REGT_ESCR}, /*   MOB_ESCR0,1 */
	/*pmc 3 */    {{MSR_P4_ITLB_ESCR0, MSR_P4_ITLB_ESCR1}, 0, PFM_REGT_ESCR}, /*  ITLB_ESCR0,1 */
	/*pmc 4 */    {{MSR_P4_PMH_ESCR0, MSR_P4_PMH_ESCR1}, 0, PFM_REGT_ESCR}, /*   PMH_ESCR0,1 */
	/*pmc 5 */    {{MSR_P4_IX_ESCR0, MSR_P4_IX_ESCR1}, 0, PFM_REGT_ESCR}, /*    IX_ESCR0,1 */
	/*pmc 6 */    {{MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1}, 0, PFM_REGT_ESCR}, /*   FSB_ESCR0,1 */
	/*pmc 7 */    {{MSR_P4_BSU_ESCR0, MSR_P4_BSU_ESCR1}, 0, PFM_REGT_ESCR}, /*   BSU_ESCR0,1 */
	/*pmc 8 */    {{MSR_P4_MS_ESCR0, MSR_P4_MS_ESCR1}, 0, PFM_REGT_ESCR}, /*    MS_ESCR0,1 */
	/*pmc 9 */    {{MSR_P4_TC_ESCR0, MSR_P4_TC_ESCR1}, 0, PFM_REGT_ESCR}, /*    TC_ESCR0,1 */
	/*pmc 10*/    {{MSR_P4_TBPU_ESCR0, MSR_P4_TBPU_ESCR1}, 0, PFM_REGT_ESCR}, /*  TBPU_ESCR0,1 */
	/*pmc 11*/    {{MSR_P4_FLAME_ESCR0, MSR_P4_FLAME_ESCR1}, 0, PFM_REGT_ESCR}, /* FLAME_ESCR0,1 */
	/*pmc 12*/    {{MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1}, 0, PFM_REGT_ESCR}, /*  FIRM_ESCR0,1 */
	/*pmc 13*/    {{MSR_P4_SAAT_ESCR0, MSR_P4_SAAT_ESCR1}, 0, PFM_REGT_ESCR}, /*  SAAT_ESCR0,1 */
	/*pmc 14*/    {{MSR_P4_U2L_ESCR0, MSR_P4_U2L_ESCR1}, 0, PFM_REGT_ESCR}, /*   U2L_ESCR0,1 */
	/*pmc 15*/    {{MSR_P4_DAC_ESCR0, MSR_P4_DAC_ESCR1}, 0, PFM_REGT_ESCR}, /*   DAC_ESCR0,1 */
	/*pmc 16*/    {{MSR_P4_IQ_ESCR0, MSR_P4_IQ_ESCR1}, 0, PFM_REGT_ESCR}, /*    IQ_ESCR0,1 (only model 1 and 2) */
	/*pmc 17*/    {{MSR_P4_ALF_ESCR0, MSR_P4_ALF_ESCR1}, 0, PFM_REGT_ESCR}, /*   ALF_ESCR0,1 */
	/*pmc 18*/    {{MSR_P4_RAT_ESCR0, MSR_P4_RAT_ESCR1}, 0, PFM_REGT_ESCR}, /*   RAT_ESCR0,1 */
	/*pmc 19*/    {{MSR_P4_SSU_ESCR0, 0}, 0, PFM_REGT_ESCR}, /*   SSU_ESCR0   */
	/*pmc 20*/    {{MSR_P4_CRU_ESCR0, MSR_P4_CRU_ESCR1}, 0, PFM_REGT_ESCR}, /*   CRU_ESCR0,1 */
	/*pmc 21*/    {{MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3}, 0, PFM_REGT_ESCR}, /*   CRU_ESCR2,3 */
	/*pmc 22*/    {{MSR_P4_CRU_ESCR4, MSR_P4_CRU_ESCR5}, 0, PFM_REGT_ESCR}, /*   CRU_ESCR4,5 */

	/*pmc 23*/    {{MSR_P4_BPU_CCCR0, MSR_P4_BPU_CCCR2}, 0, PFM_REGT_ENAC}, /*   BPU_CCCR0,2 */
	/*pmc 24*/    {{MSR_P4_BPU_CCCR1, MSR_P4_BPU_CCCR3}, 1, PFM_REGT_ENAC}, /*   BPU_CCCR1,3 */
	/*pmc 25*/    {{MSR_P4_MS_CCCR0, MSR_P4_MS_CCCR2}, 2, PFM_REGT_ENAC}, /*    MS_CCCR0,2 */
	/*pmc 26*/    {{MSR_P4_MS_CCCR1, MSR_P4_MS_CCCR3}, 3, PFM_REGT_ENAC}, /*    MS_CCCR1,3 */
	/*pmc 27*/    {{MSR_P4_FLAME_CCCR0, MSR_P4_FLAME_CCCR2}, 4, PFM_REGT_ENAC}, /* FLAME_CCCR0,2 */
	/*pmc 28*/    {{MSR_P4_FLAME_CCCR1, MSR_P4_FLAME_CCCR3}, 5, PFM_REGT_ENAC}, /* FLAME_CCCR1,3 */
	/*pmc 29*/    {{MSR_P4_IQ_CCCR0, MSR_P4_IQ_CCCR2}, 6, PFM_REGT_ENAC}, /*    IQ_CCCR0,2 */
	/*pmc 30*/    {{MSR_P4_IQ_CCCR1, MSR_P4_IQ_CCCR3}, 7, PFM_REGT_ENAC}, /*    IQ_CCCR1,3 */
	/*pmc 31*/    {{MSR_P4_IQ_CCCR4, MSR_P4_IQ_CCCR5}, 8, PFM_REGT_ENAC}, /*    IQ_CCCR4,5 */
	/* non HT extensions */
	/*pmc 32*/    {{MSR_P4_BPU_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   BPU_ESCR1   */
	/*pmc 33*/    {{MSR_P4_IS_ESCR1,     0},  0, PFM_REGT_NHTESCR}, /*    IS_ESCR1   */
	/*pmc 34*/    {{MSR_P4_MOB_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   MOB_ESCR1   */
	/*pmc 35*/    {{MSR_P4_ITLB_ESCR1,   0},  0, PFM_REGT_NHTESCR}, /*  ITLB_ESCR1   */
	/*pmc 36*/    {{MSR_P4_PMH_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   PMH_ESCR1   */
	/*pmc 37*/    {{MSR_P4_IX_ESCR1,     0},  0, PFM_REGT_NHTESCR}, /*    IX_ESCR1   */
	/*pmc 38*/    {{MSR_P4_FSB_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   FSB_ESCR1   */
	/*pmc 39*/    {{MSR_P4_BSU_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   BSU_ESCR1   */
	/*pmc 40*/    {{MSR_P4_MS_ESCR1,     0},  0, PFM_REGT_NHTESCR}, /*    MS_ESCR1   */
	/*pmc 41*/    {{MSR_P4_TC_ESCR1,     0},  0, PFM_REGT_NHTESCR}, /*    TC_ESCR1   */
	/*pmc 42*/    {{MSR_P4_TBPU_ESCR1,   0},  0, PFM_REGT_NHTESCR}, /*  TBPU_ESCR1   */
	/*pmc 43*/    {{MSR_P4_FLAME_ESCR1,  0},  0, PFM_REGT_NHTESCR}, /* FLAME_ESCR1   */
	/*pmc 44*/    {{MSR_P4_FIRM_ESCR1,   0},  0, PFM_REGT_NHTESCR}, /*  FIRM_ESCR1   */
	/*pmc 45*/    {{MSR_P4_SAAT_ESCR1,   0},  0, PFM_REGT_NHTESCR}, /*  SAAT_ESCR1   */
	/*pmc 46*/    {{MSR_P4_U2L_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   U2L_ESCR1   */
	/*pmc 47*/    {{MSR_P4_DAC_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   DAC_ESCR1   */
	/*pmc 48*/    {{MSR_P4_IQ_ESCR1,     0},  0, PFM_REGT_NHTESCR}, /*    IQ_ESCR1   (only model 1 and 2) */
	/*pmc 49*/    {{MSR_P4_ALF_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   ALF_ESCR1   */
	/*pmc 50*/    {{MSR_P4_RAT_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   RAT_ESCR1   */
	/*pmc 51*/    {{MSR_P4_CRU_ESCR1,    0},  0, PFM_REGT_NHTESCR}, /*   CRU_ESCR1   */
	/*pmc 52*/    {{MSR_P4_CRU_ESCR3,    0},  0, PFM_REGT_NHTESCR}, /*   CRU_ESCR3   */
	/*pmc 53*/    {{MSR_P4_CRU_ESCR5,    0},  0, PFM_REGT_NHTESCR}, /*   CRU_ESCR5   */
	/*pmc 54*/    {{MSR_P4_BPU_CCCR1,    0},  9, PFM_REGT_NHTCCCR}, /*   BPU_CCCR1   */
	/*pmc 55*/    {{MSR_P4_BPU_CCCR3,    0}, 10, PFM_REGT_NHTCCCR}, /*   BPU_CCCR3   */
	/*pmc 56*/    {{MSR_P4_MS_CCCR1,     0}, 11, PFM_REGT_NHTCCCR}, /*    MS_CCCR1   */
	/*pmc 57*/    {{MSR_P4_MS_CCCR3,     0}, 12, PFM_REGT_NHTCCCR}, /*    MS_CCCR3   */
	/*pmc 58*/    {{MSR_P4_FLAME_CCCR1,  0}, 13, PFM_REGT_NHTCCCR}, /* FLAME_CCCR1   */
	/*pmc 59*/    {{MSR_P4_FLAME_CCCR3,  0}, 14, PFM_REGT_NHTCCCR}, /* FLAME_CCCR3   */
	/*pmc 60*/    {{MSR_P4_IQ_CCCR2,     0}, 15, PFM_REGT_NHTCCCR}, /*    IQ_CCCR2   */
	/*pmc 61*/    {{MSR_P4_IQ_CCCR3,     0}, 16, PFM_REGT_NHTCCCR}, /*    IQ_CCCR3   */
	/*pmc 62*/    {{MSR_P4_IQ_CCCR5,     0}, 17, PFM_REGT_NHTCCCR}, /*    IQ_CCCR5   */
	/*pmc 63*/    {{0x3f2,     0}, 0, PFM_REGT_NHTPEBS},/* PEBS_MATRIX_VERT */
	/*pmc 64*/    {{0x3f1,     0}, 0, PFM_REGT_NHTPEBS} /* PEBS_ENABLE   */
};

static struct pfm_p4_regmap pmd_addrs[PFM_MAX_PMDS] = {
	/*pmd 0 */    {{MSR_P4_BPU_PERFCTR0, MSR_P4_BPU_PERFCTR2}, 0, PFM_REGT_CTR},  /*   BPU_CTR0,2  */
	/*pmd 1 */    {{MSR_P4_BPU_PERFCTR1, MSR_P4_BPU_PERFCTR3}, 0, PFM_REGT_CTR},  /*   BPU_CTR1,3  */
	/*pmd 2 */    {{MSR_P4_MS_PERFCTR0, MSR_P4_MS_PERFCTR2}, 0, PFM_REGT_CTR},  /*    MS_CTR0,2  */
	/*pmd 3 */    {{MSR_P4_MS_PERFCTR1, MSR_P4_MS_PERFCTR3}, 0, PFM_REGT_CTR},  /*    MS_CTR1,3  */
	/*pmd 4 */    {{MSR_P4_FLAME_PERFCTR0, MSR_P4_FLAME_PERFCTR2}, 0, PFM_REGT_CTR},  /* FLAME_CTR0,2  */
	/*pmd 5 */    {{MSR_P4_FLAME_PERFCTR1, MSR_P4_FLAME_PERFCTR3}, 0, PFM_REGT_CTR},  /* FLAME_CTR1,3  */
	/*pmd 6 */    {{MSR_P4_IQ_PERFCTR0, MSR_P4_IQ_PERFCTR2}, 0, PFM_REGT_CTR},  /*    IQ_CTR0,2  */
	/*pmd 7 */    {{MSR_P4_IQ_PERFCTR1, MSR_P4_IQ_PERFCTR3}, 0, PFM_REGT_CTR},  /*    IQ_CTR1,3  */
	/*pmd 8 */    {{MSR_P4_IQ_PERFCTR4, MSR_P4_IQ_PERFCTR5}, 0, PFM_REGT_CTR},  /*    IQ_CTR4,5  */
	/*
	 * non HT extensions
	 */
	/*pmd 9 */    {{MSR_P4_BPU_PERFCTR2,     0}, 0, PFM_REGT_NHTCTR},  /*   BPU_CTR2    */
	/*pmd 10*/    {{MSR_P4_BPU_PERFCTR3,     0}, 0, PFM_REGT_NHTCTR},  /*   BPU_CTR3    */
	/*pmd 11*/    {{MSR_P4_MS_PERFCTR2,     0}, 0, PFM_REGT_NHTCTR},  /*    MS_CTR2    */
	/*pmd 12*/    {{MSR_P4_MS_PERFCTR3,     0}, 0, PFM_REGT_NHTCTR},  /*    MS_CTR3    */
	/*pmd 13*/    {{MSR_P4_FLAME_PERFCTR2,     0}, 0, PFM_REGT_NHTCTR},  /* FLAME_CTR2    */
	/*pmd 14*/    {{MSR_P4_FLAME_PERFCTR3,     0}, 0, PFM_REGT_NHTCTR},  /* FLAME_CTR3    */
	/*pmd 15*/    {{MSR_P4_IQ_PERFCTR2,     0}, 0, PFM_REGT_NHTCTR},  /*    IQ_CTR2    */
	/*pmd 16*/    {{MSR_P4_IQ_PERFCTR3,     0}, 0, PFM_REGT_NHTCTR},  /*    IQ_CTR3    */
	/*pmd 17*/    {{MSR_P4_IQ_PERFCTR5,     0}, 0, PFM_REGT_NHTCTR},  /*    IQ_CTR5    */
};

static struct pfm_arch_pmu_info pfm_p4_pmu_info = {
	.write_pmc = pfm_p4_write_pmc,
	.write_pmd = pfm_p4_write_pmd,
	.read_pmc = pfm_p4_read_pmc,
	.read_pmd = pfm_p4_read_pmd,
	.create_context = pfm_p4_create_context,
	.free_context = pfm_p4_free_context,
	.has_ovfls = pfm_p4_has_ovfls,
	.stop_save = pfm_p4_stop_save,
	.restore_pmcs = pfm_p4_restore_pmcs,
	.nmi_copy_state = pfm_p4_nmi_copy_state,
	.quiesce = pfm_p4_quiesce
};

static struct pfm_regmap_desc pfm_p4_pmc_desc[] = {
/* pmc0  */ PMC_D(PFM_REG_I, "BPU_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_BPU_ESCR0),
/* pmc1  */ PMC_D(PFM_REG_I, "IS_ESCR0"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_IQ_ESCR0),
/* pmc2  */ PMC_D(PFM_REG_I, "MOB_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_MOB_ESCR0),
/* pmc3  */ PMC_D(PFM_REG_I, "ITLB_ESCR0" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_ITLB_ESCR0),
/* pmc4  */ PMC_D(PFM_REG_I, "PMH_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_PMH_ESCR0),
/* pmc5  */ PMC_D(PFM_REG_I, "IX_ESCR0"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_IX_ESCR0),
/* pmc6  */ PMC_D(PFM_REG_I, "FSB_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_FSB_ESCR0),
/* pmc7  */ PMC_D(PFM_REG_I, "BSU_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_BSU_ESCR0),
/* pmc8  */ PMC_D(PFM_REG_I, "MS_ESCR0"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_MS_ESCR0),
/* pmc9  */ PMC_D(PFM_REG_I, "TC_ESCR0"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_TC_ESCR0),
/* pmc10 */ PMC_D(PFM_REG_I, "TBPU_ESCR0" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_TBPU_ESCR0),
/* pmc11 */ PMC_D(PFM_REG_I, "FLAME_ESCR0", 0x0, PFM_ESCR_RSVD, 0, MSR_P4_FLAME_ESCR0),
/* pmc12 */ PMC_D(PFM_REG_I, "FIRM_ESCR0" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_FIRM_ESCR0),
/* pmc13 */ PMC_D(PFM_REG_I, "SAAT_ESCR0" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_SAAT_ESCR0),
/* pmc14 */ PMC_D(PFM_REG_I, "U2L_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_U2L_ESCR0),
/* pmc15 */ PMC_D(PFM_REG_I, "DAC_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_DAC_ESCR0),
/* pmc16 */ PMC_D(PFM_REG_I, "IQ_ESCR0"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_IQ_ESCR0), /* only model 1 and 2*/
/* pmc17 */ PMC_D(PFM_REG_I, "ALF_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_ALF_ESCR0),
/* pmc18 */ PMC_D(PFM_REG_I, "RAT_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_RAT_ESCR0),
/* pmc19 */ PMC_D(PFM_REG_I, "SSU_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_SSU_ESCR0),
/* pmc20 */ PMC_D(PFM_REG_I, "CRU_ESCR0"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_CRU_ESCR0),
/* pmc21 */ PMC_D(PFM_REG_I, "CRU_ESCR2"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_CRU_ESCR2),
/* pmc22 */ PMC_D(PFM_REG_I, "CRU_ESCR4"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_CRU_ESCR4),
/* pmc23 */ PMC_D(PFM_REG_I64, "BPU_CCCR0"  , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_BPU_CCCR0),
/* pmc24 */ PMC_D(PFM_REG_I64, "BPU_CCCR1"  , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_BPU_CCCR1),
/* pmc25 */ PMC_D(PFM_REG_I64, "MS_CCCR0"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_MS_CCCR0),
/* pmc26 */ PMC_D(PFM_REG_I64, "MS_CCCR1"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_MS_CCCR1),
/* pmc27 */ PMC_D(PFM_REG_I64, "FLAME_CCCR0", PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_FLAME_CCCR0),
/* pmc28 */ PMC_D(PFM_REG_I64, "FLAME_CCCR1", PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_FLAME_CCCR1),
/* pmc29 */ PMC_D(PFM_REG_I64, "IQ_CCCR0"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_IQ_CCCR0),
/* pmc30 */ PMC_D(PFM_REG_I64, "IQ_CCCR1"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_IQ_CCCR1),
/* pmc31 */ PMC_D(PFM_REG_I64, "IQ_CCCR4"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_IQ_CCCR4),
		/* No HT extension */
/* pmc32 */ PMC_D(PFM_REG_I, "BPU_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_BPU_ESCR1),
/* pmc33 */ PMC_D(PFM_REG_I, "IS_ESCR1"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_IS_ESCR1),
/* pmc34 */ PMC_D(PFM_REG_I, "MOB_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_MOB_ESCR1),
/* pmc35 */ PMC_D(PFM_REG_I, "ITLB_ESCR1" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_ITLB_ESCR1),
/* pmc36 */ PMC_D(PFM_REG_I, "PMH_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_PMH_ESCR1),
/* pmc37 */ PMC_D(PFM_REG_I, "IX_ESCR1"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_IX_ESCR1),
/* pmc38 */ PMC_D(PFM_REG_I, "FSB_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_FSB_ESCR1),
/* pmc39 */ PMC_D(PFM_REG_I, "BSU_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_BSU_ESCR1),
/* pmc40 */ PMC_D(PFM_REG_I, "MS_ESCR1"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_MS_ESCR1),
/* pmc41 */ PMC_D(PFM_REG_I, "TC_ESCR1"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_TC_ESCR1),
/* pmc42 */ PMC_D(PFM_REG_I, "TBPU_ESCR1" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_TBPU_ESCR1),
/* pmc43 */ PMC_D(PFM_REG_I, "FLAME_ESCR1", 0x0, PFM_ESCR_RSVD, 0, MSR_P4_FLAME_ESCR1),
/* pmc44 */ PMC_D(PFM_REG_I, "FIRM_ESCR1" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_FIRM_ESCR1),
/* pmc45 */ PMC_D(PFM_REG_I, "SAAT_ESCR1" , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_SAAT_ESCR1),
/* pmc46 */ PMC_D(PFM_REG_I, "U2L_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_U2L_ESCR1),
/* pmc47 */ PMC_D(PFM_REG_I, "DAC_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_DAC_ESCR1),
/* pmc48 */ PMC_D(PFM_REG_I, "IQ_ESCR1"   , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_IQ_ESCR1), /* only model 1 and 2 */
/* pmc49 */ PMC_D(PFM_REG_I, "ALF_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_ALF_ESCR1),
/* pmc50 */ PMC_D(PFM_REG_I, "RAT_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_RAT_ESCR1),
/* pmc51 */ PMC_D(PFM_REG_I, "CRU_ESCR1"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_CRU_ESCR1),
/* pmc52 */ PMC_D(PFM_REG_I, "CRU_ESCR3"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_CRU_ESCR3),
/* pmc53 */ PMC_D(PFM_REG_I, "CRU_ESCR5"  , 0x0, PFM_ESCR_RSVD, 0, MSR_P4_CRU_ESCR5),
/* pmc54 */ PMC_D(PFM_REG_I64, "BPU_CCCR2"  , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_BPU_CCCR2),
/* pmc55 */ PMC_D(PFM_REG_I64, "BPU_CCCR3"  , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_BPU_CCCR3),
/* pmc56 */ PMC_D(PFM_REG_I64, "MS_CCCR2"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_MS_CCCR2),
/* pmc57 */ PMC_D(PFM_REG_I64, "MS_CCCR3"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_MS_CCCR3),
/* pmc58 */ PMC_D(PFM_REG_I64, "FLAME_CCCR2", PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_FLAME_CCCR2),
/* pmc59 */ PMC_D(PFM_REG_I64, "FLAME_CCCR3", PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_FLAME_CCCR3),
/* pmc60 */ PMC_D(PFM_REG_I64, "IQ_CCCR2"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_IQ_CCCR2),
/* pmc61 */ PMC_D(PFM_REG_I64, "IQ_CCCR3"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_IQ_CCCR3),
/* pmc62 */ PMC_D(PFM_REG_I64, "IQ_CCCR5"   , PFM_CCCR_DFL, PFM_CCCR_RSVD, PFM_P4_NO64, MSR_P4_IQ_CCCR5),
/* pmc63 */ PMC_D(PFM_REG_I, "PEBS_MATRIX_VERT", 0, 0xffffffffffffffecULL, 0, 0x3f2),
/* pmc64 */ PMC_D(PFM_REG_I, "PEBS_ENABLE", 0, 0xfffffffff8ffe000ULL, 0, 0x3f1)
};
#define PFM_P4_NUM_PMCS ARRAY_SIZE(pfm_p4_pmc_desc)

/*
 * See section 15.10.6.6 for details about the IQ block
 */
static struct pfm_regmap_desc pfm_p4_pmd_desc[] = {
/* pmd0  */ PMD_D(PFM_REG_C, "BPU_CTR0", MSR_P4_BPU_PERFCTR0),
/* pmd1  */ PMD_D(PFM_REG_C, "BPU_CTR1", MSR_P4_BPU_PERFCTR1),
/* pmd2  */ PMD_D(PFM_REG_C, "MS_CTR0", MSR_P4_MS_PERFCTR0),
/* pmd3  */ PMD_D(PFM_REG_C, "MS_CTR1", MSR_P4_MS_PERFCTR1),
/* pmd4  */ PMD_D(PFM_REG_C, "FLAME_CTR0", MSR_P4_FLAME_PERFCTR0),
/* pmd5  */ PMD_D(PFM_REG_C, "FLAME_CTR1", MSR_P4_FLAME_PERFCTR1),
/* pmd6  */ PMD_D(PFM_REG_C, "IQ_CTR0", MSR_P4_IQ_PERFCTR0),
/* pmd7  */ PMD_D(PFM_REG_C, "IQ_CTR1", MSR_P4_IQ_PERFCTR1),
/* pmd8  */ PMD_D(PFM_REG_C, "IQ_CTR4", MSR_P4_IQ_PERFCTR4),
		/* no HT extension */
/* pmd9  */ PMD_D(PFM_REG_C, "BPU_CTR2", MSR_P4_BPU_PERFCTR2),
/* pmd10 */ PMD_D(PFM_REG_C, "BPU_CTR3", MSR_P4_BPU_PERFCTR3),
/* pmd11 */ PMD_D(PFM_REG_C, "MS_CTR2", MSR_P4_MS_PERFCTR2),
/* pmd12 */ PMD_D(PFM_REG_C, "MS_CTR3", MSR_P4_MS_PERFCTR3),
/* pmd13 */ PMD_D(PFM_REG_C, "FLAME_CTR2", MSR_P4_FLAME_PERFCTR2),
/* pmd14 */ PMD_D(PFM_REG_C, "FLAME_CTR3", MSR_P4_FLAME_PERFCTR3),
/* pmd15 */ PMD_D(PFM_REG_C, "IQ_CTR2", MSR_P4_IQ_PERFCTR2),
/* pmd16 */ PMD_D(PFM_REG_C, "IQ_CTR3", MSR_P4_IQ_PERFCTR3),
/* pmd17 */ PMD_D(PFM_REG_C, "IQ_CTR5", MSR_P4_IQ_PERFCTR5)
};
#define PFM_P4_NUM_PMDS ARRAY_SIZE(pfm_p4_pmd_desc)

/*
 * Due to hotplug CPU support, threads may not necessarily
 * be activated at the time the module is inserted. We need
 * to check whether  they could be activated by looking at
 * the present CPU (present != online).
 */
static int pfm_p4_probe_pmu(void)
{
	unsigned int i;
	int ht_enabled;

	/*
	 * only works on Intel processors
	 */
	if (current_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		PFM_INFO("not running on Intel processor");
		return -1;
	}

	if (current_cpu_data.x86 != 15) {
		PFM_INFO("unsupported family=%d", current_cpu_data.x86);
		return -1;
	}

	switch (current_cpu_data.x86_model) {
	case 0 ... 2:
		break;
	case 3 ... 6:
		/*
		 * IQ_ESCR0, IQ_ESCR1 only present on model 1, 2
		 */
		pfm_p4_pmc_desc[16].type = PFM_REG_NA;
		pfm_p4_pmc_desc[48].type = PFM_REG_NA;
		break;
	default:
		/*
		 * do not know if they all work the same, so reject
		 * for now
		 */
		if (!force) {
			PFM_INFO("unsupported model %d",
				 current_cpu_data.x86_model);
			return -1;
		}
	}

	/*
	 * check for local APIC (required)
	 */
	if (!cpu_has_apic) {
		PFM_INFO("no local APIC, unsupported");
		return -1;
	}
#ifdef CONFIG_SMP
	ht_enabled = (cpus_weight(__get_cpu_var(cpu_core_map))
		   / current_cpu_data.x86_max_cores) > 1;
#else
	ht_enabled = 0;
#endif
	if (cpu_has_ht) {

		PFM_INFO("HyperThreading supported, status %s",
			 ht_enabled ? "on": "off");
		/*
		 * disable registers not supporting HT
		 */
		if (ht_enabled) {
			PFM_INFO("disabling half the registers for HT");
			for (i = 0; i < PFM_P4_NUM_PMCS; i++) {
				if (pmc_addrs[(i)].reg_type & PFM_REGT_NOHT)
					pfm_p4_pmc_desc[i].type = PFM_REG_NA;
			}
			for (i = 0; i < PFM_P4_NUM_PMDS; i++) {
				if (pmd_addrs[(i)].reg_type & PFM_REGT_NOHT)
					pfm_p4_pmd_desc[i].type = PFM_REG_NA;
			}
		}
	}

	if (cpu_has_ds) {
		PFM_INFO("Data Save Area (DS) supported");

		if (cpu_has_pebs) {
			/*
			 * PEBS does not work with HyperThreading enabled
			 */
			if (ht_enabled)
				PFM_INFO("PEBS supported, status off (because of HT)");
			else
				PFM_INFO("PEBS supported, status on");
		}
	}

	/*
	 * build enable mask
	 */
	for (i = 0; i < PFM_P4_NUM_PMCS; i++) {
		if (pmc_addrs[(i)].reg_type & PFM_REGT_EN) {
			__set_bit(i, cast_ulp(enable_mask));
			max_enable = i + 1;
		}
	}

	if (force_nmi)
		pfm_p4_pmu_info.flags |= PFM_X86_FL_USE_NMI;
	return 0;
}
static inline int get_smt_id(void)
{
#ifdef CONFIG_SMP
	int cpu = smp_processor_id();
	return (cpu != first_cpu(__get_cpu_var(cpu_sibling_map)));
#else
	return 0;
#endif
}

static void __pfm_write_reg_p4(const struct pfm_p4_regmap *xreg, u64 val)
{
	u64 pmi;
	int smt_id;

	smt_id = get_smt_id();
	/*
	 * HT is only supported by P4-style PMU
	 *
	 * Adjust for T1 if necessary:
	 *
	 * - move the T0_OS/T0_USR bits into T1 slots
	 * - move the OVF_PMI_T0 bits into T1 slot
	 *
	 * The P4/EM64T T1 is cleared by description table.
	 * User only works with T0.
	 */
	if (smt_id) {
		if (xreg->reg_type & PFM_REGT_ESCR) {

			/* copy T0_USR & T0_OS to T1 */
			val |= ((val & 0xc) >> 2);

			/* clear bits T0_USR & T0_OS */
			val &= ~0xc;

		} else if (xreg->reg_type & PFM_REGT_CCCR) {
			pmi = (val >> 26) & 0x1;
			if (pmi) {
				val &= ~(1UL<<26);
				val |= 1UL<<27;
			}
		}
	}
	if (xreg->addrs[smt_id])
		wrmsrl(xreg->addrs[smt_id], val);
}

void __pfm_read_reg_p4(const struct pfm_p4_regmap *xreg, u64 *val)
{
	int smt_id;

	smt_id = get_smt_id();

	if (likely(xreg->addrs[smt_id])) {
		rdmsrl(xreg->addrs[smt_id], *val);
		/*
		 * HT is only supported by P4-style PMU
		 *
		 * move the Tx_OS and Tx_USR bits into
		 * T0 slots setting the T1 slots to zero
		 */
		if (xreg->reg_type & PFM_REGT_ESCR) {
			if (smt_id)
				*val |= (((*val) & 0x3) << 2);

			/*
			 * zero out bits that are reserved
			 * (including T1_OS and T1_USR)
			 */
			*val &= PFM_ESCR_RSVD;
		}
	} else {
		*val = 0;
	}
}
static void pfm_p4_write_pmc(struct pfm_context *ctx, unsigned int cnum, u64 value)
{
	__pfm_write_reg_p4(&pmc_addrs[cnum], value);
}

static void pfm_p4_write_pmd(struct pfm_context *ctx, unsigned int cnum, u64 value)
{
	__pfm_write_reg_p4(&pmd_addrs[cnum], value);
}

static u64 pfm_p4_read_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	u64 tmp;
	__pfm_read_reg_p4(&pmd_addrs[cnum], &tmp);
	return tmp;
}

static u64 pfm_p4_read_pmc(struct pfm_context *ctx, unsigned int cnum)
{
	u64 tmp;
	__pfm_read_reg_p4(&pmc_addrs[cnum], &tmp);
	return tmp;
}

struct pfm_ds_area_p4 {
	unsigned long	bts_buf_base;
	unsigned long	bts_index;
	unsigned long	bts_abs_max;
	unsigned long	bts_intr_thres;
	unsigned long	pebs_buf_base;
	unsigned long	pebs_index;
	unsigned long	pebs_abs_max;
	unsigned long	pebs_intr_thres;
	u64		pebs_cnt_reset;
};


static int pfm_p4_stop_save(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_arch_context *ctx_arch;
	struct pfm_ds_area_p4 *ds = NULL;
	u64 used_mask[PFM_PMC_BV];
	u16 i, j, count, pebs_idx = ~0;
	u16 max_pmc;
	u64 cccr, ctr1, ctr2, ovfl_mask;

	pmu_info = &pfm_p4_pmu_info;
	ctx_arch = pfm_ctx_arch(ctx);
	max_pmc = ctx->regs.max_pmc;
	ovfl_mask = pfm_pmu_conf->ovfl_mask;

	/*
	 * build used enable PMC bitmask
	 * if user did not set any CCCR, then mask is
	 * empty and there is nothing to do because nothing
	 * was started
	 */
	bitmap_and(cast_ulp(used_mask),
		   cast_ulp(set->used_pmcs),
		   cast_ulp(enable_mask),
		   max_enable);

	count = bitmap_weight(cast_ulp(used_mask), max_enable);

	PFM_DBG_ovfl("npend=%u ena_mask=0x%llx u_pmcs=0x%llx count=%u num=%u",
		set->npend_ovfls,
		(unsigned long long)enable_mask[0],
		(unsigned long long)set->used_pmcs[0],
		count, max_enable);

	/*
	 * ensures we do not destroy pending overflow
	 * information. If pended interrupts are already
	 * known, then we just stop monitoring.
	 */
	if (set->npend_ovfls) {
		/*
		 * clear enable bit
		 * unfortunately, this is very expensive!
		 */
		for (i = 0; count; i++) {
			if (test_bit(i, cast_ulp(used_mask))) {
				__pfm_write_reg_p4(pmc_addrs+i, 0);
				count--;
			}
		}
		/* need save PMDs at upper level */
		return 1;
	}

	if (ctx_arch->flags.use_pebs) {
		ds = ctx_arch->ds_area;
		pebs_idx = PEBS_PMD;
		PFM_DBG("ds=%p pebs_idx=0x%llx thres=0x%llx",
			ds,
			(unsigned long long)ds->pebs_index,
			(unsigned long long)ds->pebs_intr_thres);
	}

	/*
	 * stop monitoring AND collect pending overflow information AND
	 * save pmds.
	 *
	 * We need to access the CCCR twice, once to get overflow info
	 * and a second to stop monitoring (which destroys the OVF flag)
	 * Similarly, we need to read the counter twice to check whether
	 * it did overflow between the CCR read and the CCCR write.
	 */
	for (i = 0; count; i++) {
		if (i != pebs_idx && test_bit(i, cast_ulp(used_mask))) {
			/*
			 * controlled counter
			 */
			j = pmc_addrs[i].ctr;

			/* read CCCR (PMC) value */
			__pfm_read_reg_p4(pmc_addrs+i, &cccr);

			/* read counter (PMD) controlled by PMC */
			__pfm_read_reg_p4(pmd_addrs+j, &ctr1);

			/* clear CCCR value: stop counter but destroy OVF */
			__pfm_write_reg_p4(pmc_addrs+i, 0);

			/* read counter controlled by CCCR again */
			__pfm_read_reg_p4(pmd_addrs+j, &ctr2);

			/*
			 * there is an overflow if either:
			 * 	- CCCR.ovf is set (and we just cleared it)
			 * 	- ctr2 < ctr1
			 * in that case we set the bit corresponding to the
			 * overflowed PMD  in povfl_pmds.
			 */
			if ((cccr & (1ULL<<31)) || (ctr2 < ctr1)) {
				__set_bit(j, cast_ulp(set->povfl_pmds));
				set->npend_ovfls++;
			}
			ctr2 = (set->pmds[j].value & ~ovfl_mask) | (ctr2 & ovfl_mask);
			set->pmds[j].value = ctr2;
			count--;
		}
	}
	/*
	 * check for PEBS buffer full and set the corresponding PMD overflow
	 */
	if (ctx_arch->flags.use_pebs) {
		PFM_DBG("ds=%p pebs_idx=0x%lx thres=0x%lx", ds, ds->pebs_index, ds->pebs_intr_thres);
		if (ds->pebs_index >= ds->pebs_intr_thres
		    && test_bit(PEBS_PMD, cast_ulp(set->used_pmds))) {
			__set_bit(PEBS_PMD, cast_ulp(set->povfl_pmds));
			set->npend_ovfls++;
		}
	}
	/* 0 means: no need to save the PMD at higher level */
	return 0;
}

static int pfm_p4_create_context(struct pfm_context *ctx, u32 ctx_flags)
{
	struct pfm_arch_context *ctx_arch;

	ctx_arch = pfm_ctx_arch(ctx);

	ctx_arch->data = kzalloc(sizeof(struct pfm_arch_p4_context), GFP_KERNEL);
	if (!ctx_arch->data)
		return -ENOMEM;

	return 0;
}

static void pfm_p4_free_context(struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;

	ctx_arch = pfm_ctx_arch(ctx);
	/*
	 * we do not check if P4, because it would be NULL and
	 * kfree can deal with NULL
	 */
	kfree(ctx_arch->data);
}

/*
 * detect is counters have overflowed.
 * return:
 * 	0 : no overflow
 * 	1 : at least one overflow
 *
 * used by Intel P4
 */
static int __kprobes pfm_p4_has_ovfls(struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_p4_regmap *xrc, *xrd;
	struct pfm_arch_context *ctx_arch;
	struct pfm_arch_p4_context *p4;
	u64 ena_mask[PFM_PMC_BV];
	u64 cccr, ctr1, ctr2;
	int n, i, j;

	pmu_info = &pfm_p4_pmu_info;

	ctx_arch = pfm_ctx_arch(ctx);
	xrc = pmc_addrs;
	xrd = pmd_addrs;
	p4 = ctx_arch->data;

	bitmap_and(cast_ulp(ena_mask),
		   cast_ulp(ctx->regs.pmcs),
		   cast_ulp(enable_mask),
		   max_enable);

	n = bitmap_weight(cast_ulp(ena_mask), max_enable);

	for (i = 0; n; i++) {
		if (!test_bit(i, cast_ulp(ena_mask)))
			continue;
		/*
		 * controlled counter
		 */
		j = xrc[i].ctr;

		/* read CCCR (PMC) value */
		__pfm_read_reg_p4(xrc+i, &cccr);

		/* read counter (PMD) controlled by PMC */
		__pfm_read_reg_p4(xrd+j, &ctr1);

		/* clear CCCR value: stop counter but destroy OVF */
		__pfm_write_reg_p4(xrc+i, 0);

		/* read counter controlled by CCCR again */
		__pfm_read_reg_p4(xrd+j, &ctr2);

		/*
		 * there is an overflow if either:
		 * 	- CCCR.ovf is set (and we just cleared it)
		 * 	- ctr2 < ctr1
		 * in that case we set the bit corresponding to the
		 * overflowed PMD in povfl_pmds.
		 */
		if ((cccr & (1ULL<<31)) || (ctr2 < ctr1)) {
			__set_bit(j, cast_ulp(p4->povfl_pmds));
			p4->npend_ovfls++;
		}
		p4->saved_cccrs[i] = cccr;
		n--;
	}
	/*
	 * if there was no overflow, then it means the NMI was not really
	 * for us, so we have to resume monitoring
	 */
	if (unlikely(!p4->npend_ovfls)) {
		for (i = 0; n; i++) {
			if (!test_bit(i, cast_ulp(ena_mask)))
				continue;
			__pfm_write_reg_p4(xrc+i, p4->saved_cccrs[i]);
		}
	}
	return 0;
}

void pfm_p4_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_arch_context *ctx_arch;
	u64 *mask;
	u16 i, num;

	ctx_arch = pfm_ctx_arch(ctx);
	pmu_info = pfm_pmu_info();

	/*
	 * must restore DS pointer before restoring PMCs
	 * as this can potentially reactivate monitoring
	 */
	if (ctx_arch->flags.use_ds)
		wrmsrl(MSR_IA32_DS_AREA, (unsigned long)ctx_arch->ds_area);

	/*
	 * must restore everything because there are some dependencies
	 * (e.g., ESCR and CCCR)
	 */
	num = ctx->regs.num_pmcs;
	mask = ctx->regs.pmcs;
	for (i = 0; num; i++) {
		if (test_bit(i, cast_ulp(mask))) {
			pfm_arch_write_pmc(ctx, i, set->pmcs[i]);
			num--;
		}
	}
}

/*
 * invoked only when NMI is used. Called from the LOCAL_PERFMON_VECTOR
 * handler to copy P4 overflow state captured when the NMI triggered.
 * Given that on P4, stopping monitoring destroy the overflow information
 * we save it in pfm_has_ovfl_p4() where monitoring is also stopped.
 *
 * Here we propagate the overflow state to current active set. The
 * freeze_pmu() call we not overwrite this state because npend_ovfls
 * is non-zero.
 */
static void pfm_p4_nmi_copy_state(struct pfm_context *ctx)
{
	struct pfm_arch_context *ctx_arch;
	struct pfm_event_set *set;
	struct pfm_arch_p4_context *p4;

	ctx_arch = pfm_ctx_arch(ctx);
	p4 = ctx_arch->data;
	set = ctx->active_set;

	if (p4->npend_ovfls) {
		set->npend_ovfls = p4->npend_ovfls;

		bitmap_copy(cast_ulp(set->povfl_pmds),
			    cast_ulp(p4->povfl_pmds),
			    ctx->regs.max_pmd);

		p4->npend_ovfls = 0;
	}
}

/**
 * pfm_p4_quiesce - stop monitoring without grabbing any lock
 *
 * called from NMI interrupt handler to immediately stop monitoring
 * cannot grab any lock, including perfmon related locks
 */
static void __kprobes pfm_p4_quiesce(void)
{
	u16 i;
	/*
	 * quiesce PMU by clearing available registers that have
	 * the start/stop capability
	 */
	for (i = 0; i < pfm_pmu_conf->regs_all.max_pmc; i++) {
		if (test_bit(i, cast_ulp(pfm_pmu_conf->regs_all.pmcs))
		    && test_bit(i, cast_ulp(enable_mask)))
			__pfm_write_reg_p4(pmc_addrs+i, 0);
	}
}


static struct pfm_pmu_config pfm_p4_pmu_conf = {
	.pmu_name = "Intel P4",
	.counter_width = 40,
	.pmd_desc = pfm_p4_pmd_desc,
	.pmc_desc = pfm_p4_pmc_desc,
	.num_pmc_entries = PFM_P4_NUM_PMCS,
	.num_pmd_entries = PFM_P4_NUM_PMDS,
	.probe_pmu = pfm_p4_probe_pmu,
	.version = "1.0",
	.flags = PFM_PMU_BUILTIN_FLAG,
	.owner = THIS_MODULE,
	.pmu_info = &pfm_p4_pmu_info
};

static int __init pfm_p4_pmu_init_module(void)
{
	return pfm_pmu_register(&pfm_p4_pmu_conf);
}

static void __exit pfm_p4_pmu_cleanup_module(void)
{
	pfm_pmu_unregister(&pfm_p4_pmu_conf);
}

module_init(pfm_p4_pmu_init_module);
module_exit(pfm_p4_pmu_cleanup_module);
