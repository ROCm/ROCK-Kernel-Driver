/*
 * PowerPC64 Segment Translation Support.
 *
 * Dave Engebretsen and Mike Corrigan {engebret|mikejc}@us.ibm.com
 *    Copyright (c) 2001 Dave Engebretsen
 *
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
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

static int make_ste(unsigned long stab, unsigned long esid, unsigned long vsid);
static void make_slbe(unsigned long esid, unsigned long vsid, int large,
		      int kernel_segment);

static inline void slb_add_bolted(void)
{
#ifndef CONFIG_PPC_ISERIES
	unsigned long esid = GET_ESID(VMALLOCBASE);
	unsigned long vsid = get_kernel_vsid(VMALLOCBASE);

	WARN_ON(!irqs_disabled());

	/*
	 * Bolt in the first vmalloc segment. Since modules end
	 * up there it gets hit very heavily.
	 */
	get_paca()->xStab_data.next_round_robin = 1;
	make_slbe(esid, vsid, 0, 1);
#endif
}

/*
 * Build an entry for the base kernel segment and put it into
 * the segment table or SLB.  All other segment table or SLB
 * entries are faulted in.
 */
void stab_initialize(unsigned long stab)
{
	unsigned long esid, vsid; 
	int seg0_largepages = 0;

	esid = GET_ESID(KERNELBASE);
	vsid = get_kernel_vsid(esid << SID_SHIFT); 

	if (cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE)
		seg0_largepages = 1;

	if (cur_cpu_spec->cpu_features & CPU_FTR_SLB) {
		/* Invalidate the entire SLB & all the ERATS */
#ifdef CONFIG_PPC_ISERIES
		asm volatile("isync; slbia; isync":::"memory");
#else
		asm volatile("isync":::"memory");
		asm volatile("slbmte  %0,%0"::"r" (0) : "memory");
		asm volatile("isync; slbia; isync":::"memory");
		get_paca()->xStab_data.next_round_robin = 0;
		make_slbe(esid, vsid, seg0_largepages, 1);
		asm volatile("isync":::"memory");
#endif

		slb_add_bolted();
	} else {
		asm volatile("isync; slbia; isync":::"memory");
		make_ste(stab, esid, vsid);

		/* Order update */
		asm volatile("sync":::"memory"); 
	}
}

/* Both the segment table and SLB code uses the following cache */
#define NR_STAB_CACHE_ENTRIES 8
DEFINE_PER_CPU(long, stab_cache_ptr);
DEFINE_PER_CPU(long, stab_cache[NR_STAB_CACHE_ENTRIES]);

/*
 * Segment table stuff
 */

/*
 * Create a segment table entry for the given esid/vsid pair.
 */
static int make_ste(unsigned long stab, unsigned long esid, unsigned long vsid)
{
	unsigned long entry, group, old_esid, castout_entry, i;
	unsigned int global_entry;
	STE *ste, *castout_ste;
	unsigned long kernel_segment = (REGION_ID(esid << SID_SHIFT) != 
					USER_REGION_ID);

	/* Search the primary group first. */
	global_entry = (esid & 0x1f) << 3;
	ste = (STE *)(stab | ((esid & 0x1f) << 7)); 

	/* Find an empty entry, if one exists. */
	for (group = 0; group < 2; group++) {
		for (entry = 0; entry < 8; entry++, ste++) {
			if (!(ste->dw0.dw0.v)) {
				ste->dw0.dword0 = 0;
				ste->dw1.dword1 = 0;
				ste->dw1.dw1.vsid = vsid;
				ste->dw0.dw0.esid = esid;
				ste->dw0.dw0.kp = 1;
				if (!kernel_segment)
					ste->dw0.dw0.ks = 1;
				asm volatile("eieio":::"memory");
				ste->dw0.dw0.v = 1;
				return (global_entry | entry);
			}
		}
		/* Now search the secondary group. */
		global_entry = ((~esid) & 0x1f) << 3;
		ste = (STE *)(stab | (((~esid) & 0x1f) << 7)); 
	}

	/*
	 * Could not find empty entry, pick one with a round robin selection.
	 * Search all entries in the two groups.
	 */
	castout_entry = get_paca()->xStab_data.next_round_robin;
	for (i = 0; i < 16; i++) {
		if (castout_entry < 8) {
			global_entry = (esid & 0x1f) << 3;
			ste = (STE *)(stab | ((esid & 0x1f) << 7)); 
			castout_ste = ste + castout_entry;
		} else {
			global_entry = ((~esid) & 0x1f) << 3;
			ste = (STE *)(stab | (((~esid) & 0x1f) << 7)); 
			castout_ste = ste + (castout_entry - 8);
		}

		/* Dont cast out the first kernel segment */
		if (castout_ste->dw0.dw0.esid != GET_ESID(KERNELBASE))
			break;

		castout_entry = (castout_entry + 1) & 0xf;
	}

	get_paca()->xStab_data.next_round_robin = (castout_entry + 1) & 0xf;

	/* Modify the old entry to the new value. */

	/* Force previous translations to complete. DRENG */
	asm volatile("isync" : : : "memory");

	castout_ste->dw0.dw0.v = 0;
	asm volatile("sync" : : : "memory");    /* Order update */

	castout_ste->dw0.dword0 = 0;
	castout_ste->dw1.dword1 = 0;
	castout_ste->dw1.dw1.vsid = vsid;
	old_esid = castout_ste->dw0.dw0.esid;
	castout_ste->dw0.dw0.esid = esid;
	castout_ste->dw0.dw0.kp = 1;
	if (!kernel_segment)
		castout_ste->dw0.dw0.ks = 1;
	asm volatile("eieio" : : : "memory");   /* Order update */
	castout_ste->dw0.dw0.v  = 1;
	asm volatile("slbie  %0" : : "r" (old_esid << SID_SHIFT)); 
	/* Ensure completion of slbie */
	asm volatile("sync" : : : "memory");

	return (global_entry | (castout_entry & 0x7));
}

static inline void __ste_allocate(unsigned long esid, unsigned long vsid)
{
	unsigned char stab_entry;
	unsigned long offset;
	int region_id = REGION_ID(esid << SID_SHIFT);

	stab_entry = make_ste(get_paca()->xStab_data.virt, esid, vsid);

	if (region_id != USER_REGION_ID)
		return;

	offset = __get_cpu_var(stab_cache_ptr);
	if (offset < NR_STAB_CACHE_ENTRIES)
		__get_cpu_var(stab_cache[offset++]) = stab_entry;
	else
		offset = NR_STAB_CACHE_ENTRIES+1;
	__get_cpu_var(stab_cache_ptr) = offset;
}

/*
 * Allocate a segment table entry for the given ea.
 */
int ste_allocate(unsigned long ea)
{
	unsigned long vsid, esid;
	mm_context_t context;

	/* Check for invalid effective addresses. */
	if (!IS_VALID_EA(ea))
		return 1;

	/* Kernel or user address? */
	if (REGION_ID(ea) >= KERNEL_REGION_ID) {
		vsid = get_kernel_vsid(ea);
		context = KERNEL_CONTEXT(ea);
	} else {
		if (!current->mm)
			return 1;

		context = current->mm->context;
		vsid = get_vsid(context.id, ea);
	}

	esid = GET_ESID(ea);
	__ste_allocate(esid, vsid);
	/* Order update */
	asm volatile("sync":::"memory");

	return 0;
}

/*
 * preload some userspace segments into the segment table.
 */
static void preload_stab(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long pc = KSTK_EIP(tsk);
	unsigned long stack = KSTK_ESP(tsk);
	unsigned long unmapped_base;
	unsigned long pc_esid = GET_ESID(pc);
	unsigned long stack_esid = GET_ESID(stack);
	unsigned long unmapped_base_esid;
	unsigned long vsid;

	if (test_tsk_thread_flag(tsk, TIF_32BIT))
		unmapped_base = TASK_UNMAPPED_BASE_USER32;
	else
		unmapped_base = TASK_UNMAPPED_BASE_USER64;

	unmapped_base_esid = GET_ESID(unmapped_base);

	if (!IS_VALID_EA(pc) || (REGION_ID(pc) >= KERNEL_REGION_ID))
		return;
	vsid = get_vsid(mm->context.id, pc);
	__ste_allocate(pc_esid, vsid);

	if (pc_esid == stack_esid)
		return;

	if (!IS_VALID_EA(stack) || (REGION_ID(stack) >= KERNEL_REGION_ID))
		return;
	vsid = get_vsid(mm->context.id, stack);
	__ste_allocate(stack_esid, vsid);

	if (pc_esid == unmapped_base_esid || stack_esid == unmapped_base_esid)
		return;

	if (!IS_VALID_EA(unmapped_base) ||
	    (REGION_ID(unmapped_base) >= KERNEL_REGION_ID))
		return;
	vsid = get_vsid(mm->context.id, unmapped_base);
	__ste_allocate(unmapped_base_esid, vsid);

	/* Order update */
	asm volatile("sync" : : : "memory");
}

/* Flush all user entries from the segment table of the current processor. */
void flush_stab(struct task_struct *tsk, struct mm_struct *mm)
{
	STE *stab = (STE *) get_paca()->xStab_data.virt;
	STE *ste;
	unsigned long offset = __get_cpu_var(stab_cache_ptr);

	/* Force previous translations to complete. DRENG */
	asm volatile("isync" : : : "memory");

	if (offset <= NR_STAB_CACHE_ENTRIES) {
		int i;

		for (i = 0; i < offset; i++) {
			ste = stab + __get_cpu_var(stab_cache[i]);
			ste->dw0.dw0.v = 0;
		}
	} else {
		unsigned long entry;

		/* Invalidate all entries. */
		ste = stab;

		/* Never flush the first entry. */
		ste += 1;
		for (entry = 1;
		     entry < (PAGE_SIZE / sizeof(STE));
		     entry++, ste++) {
			unsigned long ea;
			ea = ste->dw0.dw0.esid << SID_SHIFT;
			if (ea < KERNELBASE) {
				ste->dw0.dw0.v = 0;
			}
		}
	}

	asm volatile("sync; slbia; sync":::"memory");

	__get_cpu_var(stab_cache_ptr) = 0;

	preload_stab(tsk, mm);
}

/*
 * SLB stuff
 */

/*
 * Create a segment buffer entry for the given esid/vsid pair.
 *
 * NOTE: A context syncronising instruction is required before and after
 * this, in the common case we use exception entry and rfid.
 */
static void make_slbe(unsigned long esid, unsigned long vsid, int large,
		      int kernel_segment)
{
	unsigned long entry, castout_entry;
	union {
		unsigned long word0;
		slb_dword0    data;
	} esid_data;
	union {
		unsigned long word0;
		slb_dword1    data;
	} vsid_data;
	struct paca_struct *lpaca = get_paca();

	/*
	 * We take the next entry, round robin. Previously we tried
	 * to find a free slot first but that took too long. Unfortunately
	 * we dont have any LRU information to help us choose a slot.
	 */

	/* 
	 * Never cast out the segment for our kernel stack. Since we
	 * dont invalidate the ERAT we could have a valid translation
	 * for the kernel stack during the first part of exception exit 
	 * which gets invalidated due to a tlbie from another cpu at a
	 * non recoverable point (after setting srr0/1) - Anton
	 *
	 * paca Ksave is always valid (even when on the interrupt stack)
	 * so we use that.
	 */
	castout_entry = lpaca->xStab_data.next_round_robin;
	do {
		entry = castout_entry;
		castout_entry++; 
		/*
		 * We bolt in the first kernel segment and the first
		 * vmalloc segment.
		 */
		if (castout_entry >= SLB_NUM_ENTRIES)
			castout_entry = 2;
		asm volatile("slbmfee  %0,%1" : "=r" (esid_data) : "r" (entry));
	} while (esid_data.data.v &&
		 esid_data.data.esid == GET_ESID(lpaca->xKsave));

	lpaca->xStab_data.next_round_robin = castout_entry;

	/* slbie not needed as the previous mapping is still valid. */

	/* 
	 * Write the new SLB entry.
	 */
	vsid_data.word0 = 0;
	vsid_data.data.vsid = vsid;
	vsid_data.data.kp = 1;
	if (large)
		vsid_data.data.l = 1;
	if (kernel_segment)
		vsid_data.data.c = 1;
	else
		vsid_data.data.ks = 1;

	esid_data.word0 = 0;
	esid_data.data.esid = esid;
	esid_data.data.v = 1;
	esid_data.data.index = entry;

	/*
	 * No need for an isync before or after this slbmte. The exception
	 * we enter with and the rfid we exit with are context synchronizing.
	 */
	asm volatile("slbmte  %0,%1" : : "r" (vsid_data), "r" (esid_data)); 
}

static inline void __slb_allocate(unsigned long esid, unsigned long vsid,
				  mm_context_t context)
{
	int large = 0;
	int region_id = REGION_ID(esid << SID_SHIFT);
	unsigned long offset;

	if (cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE) {
		if (region_id == KERNEL_REGION_ID)
			large = 1;
		else if (region_id == USER_REGION_ID)
			large = in_hugepage_area(context, esid << SID_SHIFT);
	}

	make_slbe(esid, vsid, large, region_id != USER_REGION_ID);

	if (region_id != USER_REGION_ID)
		return;

	offset = __get_cpu_var(stab_cache_ptr);
	if (offset < NR_STAB_CACHE_ENTRIES)
		__get_cpu_var(stab_cache[offset++]) = esid;
	else
		offset = NR_STAB_CACHE_ENTRIES+1;
	__get_cpu_var(stab_cache_ptr) = offset;
}

/*
 * Allocate a segment table entry for the given ea.
 */
int slb_allocate(unsigned long ea)
{
	unsigned long vsid, esid;
	mm_context_t context;

	/* Check for invalid effective addresses. */
	if (unlikely(!IS_VALID_EA(ea)))
		return 1;

	/* Kernel or user address? */
	if (REGION_ID(ea) >= KERNEL_REGION_ID) {
		context = KERNEL_CONTEXT(ea);
		vsid = get_kernel_vsid(ea);
	} else {
		if (unlikely(!current->mm))
			return 1;

		context = current->mm->context;
		vsid = get_vsid(context.id, ea);
	}

	esid = GET_ESID(ea);
#ifndef CONFIG_PPC_ISERIES
	BUG_ON((esid << SID_SHIFT) == VMALLOCBASE);
#endif
	__slb_allocate(esid, vsid, context);

	return 0;
}

/*
 * preload some userspace segments into the SLB.
 */
static void preload_slb(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long pc = KSTK_EIP(tsk);
	unsigned long stack = KSTK_ESP(tsk);
	unsigned long unmapped_base;
	unsigned long pc_esid = GET_ESID(pc);
	unsigned long stack_esid = GET_ESID(stack);
	unsigned long unmapped_base_esid;
	unsigned long vsid;

	if (test_tsk_thread_flag(tsk, TIF_32BIT))
		unmapped_base = TASK_UNMAPPED_BASE_USER32;
	else
		unmapped_base = TASK_UNMAPPED_BASE_USER64;

	unmapped_base_esid = GET_ESID(unmapped_base);

	if (!IS_VALID_EA(pc) || (REGION_ID(pc) >= KERNEL_REGION_ID))
		return;
	vsid = get_vsid(mm->context.id, pc);
	__slb_allocate(pc_esid, vsid, mm->context);

	if (pc_esid == stack_esid)
		return;

	if (!IS_VALID_EA(stack) || (REGION_ID(stack) >= KERNEL_REGION_ID))
		return;
	vsid = get_vsid(mm->context.id, stack);
	__slb_allocate(stack_esid, vsid, mm->context);

	if (pc_esid == unmapped_base_esid || stack_esid == unmapped_base_esid)
		return;

	if (!IS_VALID_EA(unmapped_base) ||
	    (REGION_ID(unmapped_base) >= KERNEL_REGION_ID))
		return;
	vsid = get_vsid(mm->context.id, unmapped_base);
	__slb_allocate(unmapped_base_esid, vsid, mm->context);
}

/* Flush all user entries from the segment table of the current processor. */
void flush_slb(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long offset = __get_cpu_var(stab_cache_ptr);
	union {
		unsigned long word0;
		slb_dword0 data;
	} esid_data;

	if (offset <= NR_STAB_CACHE_ENTRIES) {
		int i;
		asm volatile("isync" : : : "memory");
		for (i = 0; i < offset; i++) {
			esid_data.word0 = 0;
			esid_data.data.esid = __get_cpu_var(stab_cache[i]);
			BUG_ON(esid_data.data.esid == GET_ESID(VMALLOCBASE));
			asm volatile("slbie %0" : : "r" (esid_data));
		}
		asm volatile("isync" : : : "memory");
	} else {
		asm volatile("isync; slbia; isync" : : : "memory");
		slb_add_bolted();
	}

	/* Workaround POWER5 < DD2.1 issue */
	if (offset == 1 || offset > NR_STAB_CACHE_ENTRIES) {
		/* 
		 * flush segment in EEH region, we dont normally access
		 * addresses in this region.
		 */
		esid_data.word0 = 0;
		esid_data.data.esid = EEH_REGION_ID;
		asm volatile("slbie %0" : : "r" (esid_data));
	}

	__get_cpu_var(stab_cache_ptr) = 0;

	preload_slb(tsk, mm);
}
