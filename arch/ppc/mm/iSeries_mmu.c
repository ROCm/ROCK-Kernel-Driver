/*
 * Procedures for MMU handling on iSeries systems, where we
 * have to call the hypervisor to change things in the hash
 * table.
 *
 * Copyright (C) 2001 IBM Corp.
 * updated by Dave Boutcher (boutcher@us.ibm.com)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/bootx.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCallHpt.h>
#include <linux/pci.h>
#include <asm/iSeries/iSeries_dma.h>
#include "mmu_decl.h"

int iSeries_max_kernel_hpt_slot = 0;
extern unsigned maxPacas;
extern int iSeries_hpt_loaded;
PTE *Hash = 0;

#ifdef CONFIG_PCI
extern  int  iSeries_Is_IoMmAddress(unsigned long address);

/*********************************************************************/
/* iSeries maps I/O space to device, just leave the address where is.*/
/*********************************************************************/
void* ioremap(unsigned long addr, unsigned long size)
{
	return (void*)addr;
}

void* __ioremap(unsigned long addr, unsigned long size, unsigned long flags)
{
	return (void*)addr;
}

/********************************************************************/
/* iSeries did not remapped the space.                              */
/********************************************************************/
void iounmap(void *addr)
{
	return;
}
#endif /* CONFIG_PCI */

/* 
 * Map as much of memory as will fit into the first entry of each
 * PPC HPTE Group.  (These are the "bolted" entries which will
 * never be cast out).  The iSeries Hypervisor has already mapped
 * the first 32 MB (specified in LparMap.h).  Here we map as
 * much more as we can.
 */

void __init MMU_init_hw(void)
{
	PTE hpte;
	u64 *hpte0Ptr, *hpte1Ptr;
	u32 HptSizeGroups, msPages, rpn, vsid, ea;
	u64 rtnIndex;
	u32 hpteIndex;
	u32 group;
	unsigned long numAdded;
	
	if ( ppc_md.progress ) ppc_md.progress("hash:enter", 0x105);

	hpte0Ptr = (u64 *)&hpte;
	hpte1Ptr = hpte0Ptr + 1;
	
	/* Get the number of Hpt groups */
	HptSizeGroups = (u32)HvCallHpt_getHptPages() * 32;
	Hash_mask = HptSizeGroups - 1;
	
	/* Number of pages in memory */
	msPages = totalLpChunks << 6;

	/* For each virtual page in kernel space, add a hpte if there
	   isn't one already in slot 0 of the primary pteg.  */

	numAdded = 0;
	
	for ( ea = (u32)KERNELBASE; ea < (u32)high_memory; ea+= PAGE_SIZE) {
  	        rpn = ea >> 12;

		vsid = ((ea >> 28) * 0x111);
		
		rtnIndex = HvCallHpt_findValid( &hpte, 
						(rpn & 0xffff) | (vsid << 16));
		hpteIndex = (u32)rtnIndex;
		if ( hpte.v ) 		/* If valid entry found */
		        continue;	/* Already mapped, nothing to do */
		if ( rtnIndex == ~0 )	/* If no free entry found */
			BUG();	        /* Can't map this page bolted */
		if ( rtnIndex >> 63 )	/* If first free slot is secondary */
			BUG();   	/* Can't map this page bolted */
		if ( (hpteIndex & 7) > 2) /* Not in first 3 slots  */
			BUG();   	
		/*
		 * If returned index is the first in the primary group
		 * then build an hpt entry for this page.  
		 */
		*hpte0Ptr = *hpte1Ptr = 0;
		hpte.vsid = vsid;
		hpte.api  = (rpn >> 11) & 0x1f;
		hpte.h    = 0;
		hpte.v    = 1;
		hpte.rpn  = physRpn_to_absRpn( rpn ); 
		hpte.r    = 1;
		hpte.c    = 1;
		hpte.m    = 1;
		hpte.w    = 0;
		hpte.i    = 0;
		hpte.g    = 0;
		hpte.pp   = 0;
		HvCallHpt_addValidate( hpteIndex, 0, &hpte );
		++numAdded;
		group = rtnIndex & 0x07;
		if (group > iSeries_max_kernel_hpt_slot)
			iSeries_max_kernel_hpt_slot = group;
	}

	printk( "iSeries_hashinit: added %ld hptes to existing mapping. Max group %x\n",
			numAdded, iSeries_max_kernel_hpt_slot );
	
	if ( ppc_md.progress ) ppc_md.progress("hash:done", 0x205);

	iSeries_hpt_loaded = 1;
	Hash = (void *)0xFFFFFFFF;
}

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux PTE.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t pte)
{
	struct mm_struct *mm;
	pmd_t *pmd;
	pte_t *ptep;
	static int nopreload;

	if (nopreload || address >= TASK_SIZE)
		return;
	mm = vma->vm_mm;
	pmd = pmd_offset(pgd_offset(mm, address), address);
	if (!pmd_none(*pmd)) {
		ptep = pte_offset_map(pmd, address);
		add_hash_page(mm->context, address, ptep);
		pte_unmap(ptep);
	}
}
