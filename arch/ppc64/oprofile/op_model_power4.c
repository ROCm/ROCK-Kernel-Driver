/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/systemcfg.h>
#include <asm/rtas.h>

#define dbg(args...) printk(args)

#include "op_impl.h"

static unsigned long reset_value[OP_MAX_COUNTER];

static int num_counters;
static int oprofile_running;
static int mmcra_has_sihv;

static void power4_reg_setup(struct op_counter_config *ctr,
			     struct op_system_config *sys,
			     int num_ctrs)
{
	int i;

	num_counters = num_ctrs;

	/*
	 * SIHV / SIPR bits are only implemented on POWER4+ (GQ) and above.
	 * However we disable it on all POWER4 until we verify it works
	 * (I was seeing some strange behaviour last time I tried).
	 *
	 * It has been verified to work on POWER5 so we enable it there.
	 */
	if (!(__is_processor(PV_POWER4) || __is_processor(PV_POWER4p)))
		mmcra_has_sihv = 1;

	for (i = 0; i < num_counters; ++i)
		reset_value[i] = 0x80000000UL - ctr[i].count;

	/* XXX setup user and kernel profiling */
}

extern void ppc64_enable_pmcs(void);

static void power4_cpu_setup(void *unused)
{
	unsigned int mmcr0 = mfspr(SPRN_MMCR0);
	unsigned long mmcra = mfspr(SPRN_MMCRA);

	ppc64_enable_pmcs();

	/* set the freeze bit */
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	mmcr0 |= MMCR0_FCM1|MMCR0_PMXE|MMCR0_FCECE;
	mmcr0 |= MMCR0_PMC1INTCONTROL|MMCR0_PMCNINTCONTROL;
	mtspr(SPRN_MMCR0, mmcr0);

	mmcra |= MMCRA_SAMPLE_ENABLE;
	mtspr(SPRN_MMCRA, mmcra);

	dbg("setup on cpu %d, mmcr0 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR0));
	dbg("setup on cpu %d, mmcr1 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR1));
	dbg("setup on cpu %d, mmcra %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCRA));
}

static void power4_start(struct op_counter_config *ctr)
{
	int i;
	unsigned int mmcr0;

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < num_counters; ++i) {
		if (ctr[i].enabled) {
			ctr_write(i, reset_value[i]);
		} else {
			ctr_write(i, 0);
		}
	}

	mmcr0 = mfspr(SPRN_MMCR0);

	/*
	 * We must clear the PMAO bit on some (GQ) chips. Just do it
	 * all the time
	 */
	mmcr0 &= ~MMCR0_PMAO;

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this excetion, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	oprofile_running = 1;

	dbg("start on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);
}

static void power4_stop(void)
{
	unsigned int mmcr0;

	/* freeze counters */
	mmcr0 = mfspr(SPRN_MMCR0);
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	oprofile_running = 0;

	dbg("stop on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);

	mb();
}

/* Fake functions used by canonicalize_pc */
static void __attribute_used__ hypervisor_bucket(void)
{
}

static void __attribute_used__ rtas_bucket(void)
{
}

static void __attribute_used__ kernel_unknown_bucket(void)
{
}

/*
 * On GQ and newer the MMCRA stores the HV and PR bits at the time
 * the SIAR was sampled. We use that to work out if the SIAR was sampled in
 * the hypervisor, our exception vectors or RTAS.
 */
static unsigned long get_pc(void)
{
	unsigned long pc = mfspr(SPRN_SIAR);
	unsigned long mmcra;

	/* Cant do much about it */
	if (!mmcra_has_sihv)
		return pc;

	mmcra = mfspr(SPRN_MMCRA);

	/* Were we in the hypervisor? */
	if ((systemcfg->platform == PLATFORM_PSERIES_LPAR) &&
	    (mmcra & MMCRA_SIHV))
		/* function descriptor madness */
		return *((unsigned long *)hypervisor_bucket);

	/* We were in userspace, nothing to do */
	if (mmcra & MMCRA_SIPR)
		return pc;

	/* Were we in our exception vectors? */
	if (pc < 0x4000UL)
		return (unsigned long)__va(pc);

#ifdef CONFIG_PPC_PSERIES
	/* Were we in RTAS? */
	if (pc >= rtas.base && pc < (rtas.base + rtas.size))
		/* function descriptor madness */
		return *((unsigned long *)rtas_bucket);
#endif

	/* Not sure where we were */
	if (pc < KERNELBASE)
		/* function descriptor madness */
		return *((unsigned long *)kernel_unknown_bucket);

	return pc;
}

static int get_kernel(unsigned long pc)
{
	int is_kernel;

	if (!mmcra_has_sihv) {
		is_kernel = (pc >= KERNELBASE);
	} else {
		unsigned long mmcra = mfspr(SPRN_MMCRA);
		is_kernel = ((mmcra & MMCRA_SIPR) == 0);
	}

	return is_kernel;
}

static void power4_handle_interrupt(struct pt_regs *regs,
				    struct op_counter_config *ctr)
{
	unsigned long pc;
	int is_kernel;
	int val;
	int i;
	unsigned int cpu = smp_processor_id();
	unsigned int mmcr0;

	pc = get_pc();
	is_kernel = get_kernel(pc);

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < num_counters; ++i) {
		val = ctr_read(i);
		if (val < 0) {
			if (oprofile_running && ctr[i].enabled) {
				oprofile_add_sample(pc, is_kernel, i, cpu);
				ctr_write(i, reset_value[i]);
			} else {
				ctr_write(i, 0);
			}
		}
	}

	mmcr0 = mfspr(SPRN_MMCR0);

	/* reset the perfmon trigger */
	mmcr0 |= MMCR0_PMXE;

	/*
	 * We must clear the PMAO bit on some (GQ) chips. Just do it
	 * all the time
	 */
	mmcr0 &= ~MMCR0_PMAO;

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this exception, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);
}

struct op_ppc64_model op_model_power4 = {
	.reg_setup		= power4_reg_setup,
	.cpu_setup		= power4_cpu_setup,
	.start			= power4_start,
	.stop			= power4_stop,
	.handle_interrupt	= power4_handle_interrupt,
};
