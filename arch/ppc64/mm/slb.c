/*
 * PowerPC64 SLB support.
 *
 * Copyright (C) 2004 David Gibson <dwg@au.ibm.com>, IBM
 * Based on earlier code writteh by:
 * Dave Engebretsen and Mike Corrigan {engebret|mikejc}@us.ibm.com
 *    Copyright (c) 2001 Dave Engebretsen
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/paca.h>
#include <asm/naca.h>
#include <asm/cputable.h>

extern void slb_allocate(unsigned long ea);

static inline void create_slbe(unsigned long ea, unsigned long vsid,
			       unsigned long flags, unsigned long entry)
{
	ea = (ea & ESID_MASK) | SLB_ESID_V | entry;
	vsid = (vsid << SLB_VSID_SHIFT) | flags;
	asm volatile("slbmte  %0,%1" :
		     : "r" (vsid), "r" (ea)
		     : "memory" );
}

static void slb_add_bolted(void)
{
#ifndef CONFIG_PPC_ISERIES
	WARN_ON(!irqs_disabled());

	/* If you change this make sure you change SLB_NUM_BOLTED
	 * appropriately too */

	/* Slot 1 - first VMALLOC segment
         * 	Since modules end up there it gets hit very heavily.
         */
	create_slbe(VMALLOCBASE, get_kernel_vsid(VMALLOCBASE),
		    SLB_VSID_KERNEL, 1);

	asm volatile("isync":::"memory");
#endif
}

/* Flush all user entries from the segment table of the current processor. */
void switch_slb(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long offset = get_paca()->slb_cache_ptr;
	unsigned long esid_data;
	unsigned long pc = KSTK_EIP(tsk);
	unsigned long stack = KSTK_ESP(tsk);
	unsigned long unmapped_base;

	if (offset <= SLB_CACHE_ENTRIES) {
		int i;
		asm volatile("isync" : : : "memory");
		for (i = 0; i < offset; i++) {
			esid_data = (unsigned long)get_paca()->slb_cache[i]
				<< SID_SHIFT;
			asm volatile("slbie %0" : : "r" (esid_data));
		}
		asm volatile("isync" : : : "memory");
	} else {
		asm volatile("isync; slbia; isync" : : : "memory");
		slb_add_bolted();
	}

	/* Workaround POWER5 < DD2.1 issue */
	if (offset == 1 || offset > SLB_CACHE_ENTRIES) {
		/* flush segment in EEH region, we shouldn't ever
		 * access addresses in this region. */
		asm volatile("slbie %0" : : "r"(EEHREGIONBASE));
	}

	get_paca()->slb_cache_ptr = 0;
	get_paca()->context = mm->context;

	/*
	 * preload some userspace segments into the SLB.
	 */
	if (test_tsk_thread_flag(tsk, TIF_32BIT))
		unmapped_base = TASK_UNMAPPED_BASE_USER32;
	else
		unmapped_base = TASK_UNMAPPED_BASE_USER64;

	if (pc >= KERNELBASE)
		return;
	slb_allocate(pc);

	if (GET_ESID(pc) == GET_ESID(stack))
		return;

	if (stack >= KERNELBASE)
		return;
	slb_allocate(stack);

	if ((GET_ESID(pc) == GET_ESID(unmapped_base))
	    || (GET_ESID(stack) == GET_ESID(unmapped_base)))
		return;

	if (unmapped_base >= KERNELBASE)
		return;
	slb_allocate(unmapped_base);
}

void slb_initialize(void)
{
#ifdef CONFIG_PPC_ISERIES
	asm volatile("isync; slbia; isync":::"memory");
#else
	unsigned long flags = SLB_VSID_KERNEL;

	/* Invalidate the entire SLB (even slot 0) & all the ERATS */
	if (cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE)
		flags |= SLB_VSID_L;

	asm volatile("isync":::"memory");
	asm volatile("slbmte  %0,%0"::"r" (0) : "memory");
	asm volatile("isync; slbia; isync":::"memory");
	create_slbe(KERNELBASE, get_kernel_vsid(KERNELBASE),
		    flags, 0);

#endif
	slb_add_bolted();
	get_paca()->stab_rr = SLB_NUM_BOLTED;
}
