/*
 * PowerPC64 port by Mike Corrigan and Dave Engebretsen
 *   {mikejc|engebret}@us.ibm.com
 *
 *    Copyright (c) 2000 Mike Corrigan <mikejc@us.ibm.com>
 *
 * SMP scalability work:
 *    Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * 
 *    Module name: htab.c
 *
 *    Description:
 *      PowerPC Hashed Page Table functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/cache.h>

#include <asm/ppcdebug.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/naca.h>
#include <asm/system.h>
#include <asm/pmc.h>
#include <asm/machdep.h>
#include <asm/lmb.h>
#include <asm/abs_addr.h>
#include <asm/tlbflush.h>
#include <asm/eeh.h>
#include <asm/tlb.h>

/*
 * Note:  pte   --> Linux PTE
 *        HPTE  --> PowerPC Hashed Page Table Entry
 */

HTAB htab_data = {NULL, 0, 0, 0, 0};

extern unsigned long _SDR1;
extern unsigned long klimit;

extern unsigned long reloc_offset(void);
#define PTRRELOC(x)	((typeof(x))((unsigned long)(x) - offset))
#define PTRUNRELOC(x)	((typeof(x))((unsigned long)(x) + offset))
#define RELOC(x)	(*PTRRELOC(&(x)))

#define KB (1024)
#define MB (1024*KB)
static inline void
create_pte_mapping(unsigned long start, unsigned long end,
		   unsigned long mode, unsigned long mask, int large)
{
	unsigned long addr, offset = reloc_offset();
	HTAB *_htab_data = PTRRELOC(&htab_data);
	HPTE *htab = (HPTE *)__v2a(_htab_data->htab);
	unsigned int step;

	if (large)
		step = 16*MB;
	else
		step = 4*KB;

	for (addr = start; addr < end; addr += step) {
		unsigned long vsid = get_kernel_vsid(addr);
		unsigned long va = (vsid << 28) | (addr & 0xfffffff);
		if (naca->platform == PLATFORM_PSERIES_LPAR)
			pSeries_lpar_make_pte(htab, va,
				(unsigned long)__v2a(addr), mode, mask, large);
		else
			pSeries_make_pte(htab, va,
				(unsigned long)__v2a(addr), mode, mask, large);
	}
}

void
htab_initialize(void)
{
	unsigned long table, htab_size_bytes;
	unsigned long pteg_count;
	unsigned long mode_rw, mask;
	unsigned long offset = reloc_offset();
	struct naca_struct *_naca = RELOC(naca);
	HTAB *_htab_data = PTRRELOC(&htab_data);

	/*
	 * Calculate the required size of the htab.  We want the number of
	 * PTEGs to equal one half the number of real pages.
	 */ 
	htab_size_bytes = 1UL << _naca->pftSize;
	pteg_count = htab_size_bytes >> 7;

	/* For debug, make the HTAB 1/8 as big as it normally would be. */
	ifppcdebug(PPCDBG_HTABSIZE) {
		pteg_count >>= 3;
		htab_size_bytes = pteg_count << 7;
	}

	_htab_data->htab_num_ptegs = pteg_count;
	_htab_data->htab_hash_mask = pteg_count - 1;

	if (naca->platform == PLATFORM_PSERIES) {
		/* Find storage for the HPT.  Must be contiguous in
		 * the absolute address space.
		 */
		table = lmb_alloc(htab_size_bytes, htab_size_bytes);
		if ( !table )
			panic("ERROR, cannot find space for HPTE\n");
		_htab_data->htab = (HPTE *)__a2v(table);

		/* htab absolute addr + encoded htabsize */
		RELOC(_SDR1) = table + __ilog2(pteg_count) - 11;

		/* Initialize the HPT with no entries */
		memset((void *)table, 0, htab_size_bytes);
	} else {
		_htab_data->htab = NULL;
		RELOC(_SDR1) = 0; 
	}

	mode_rw = _PAGE_ACCESSED | _PAGE_COHERENT | PP_RWXX;
	mask = pteg_count-1;

	/* XXX we currently map kernel text rw, should fix this */
	if (__is_processor(PV_POWER4) && _naca->physicalMemorySize > 256*MB) {
		create_pte_mapping((unsigned long)KERNELBASE, 
				   KERNELBASE + 256*MB, mode_rw, mask, 0);
		create_pte_mapping((unsigned long)KERNELBASE + 256*MB, 
				   KERNELBASE + (_naca->physicalMemorySize), 
				   mode_rw, mask, 1);
	} else {
		create_pte_mapping((unsigned long)KERNELBASE, 
				   KERNELBASE+(_naca->physicalMemorySize), 
				   mode_rw, mask, 0);
	}
}
#undef KB
#undef MB

/*
 * find_linux_pte returns the address of a linux pte for a given 
 * effective address and directory.  If not found, it returns zero.
 */
pte_t *find_linux_pte(pgd_t *pgdir, unsigned long ea)
{
	pgd_t *pg;
	pmd_t *pm;
	pte_t *pt = NULL;
	pte_t pte;

	pg = pgdir + pgd_index(ea);
	if (!pgd_none(*pg)) {

		pm = pmd_offset(pg, ea);
		if (!pmd_none(*pm)) { 
			pt = pte_offset_kernel(pm, ea);
			pte = *pt;
			if (!pte_present(pte))
				pt = NULL;
		}
	}

	return pt;
}

static inline unsigned long computeHptePP(unsigned long pte)
{
	return (pte & _PAGE_USER) |
		(((pte & _PAGE_USER) >> 1) &
		 ((~((pte >> 2) &	/* _PAGE_RW */
		     (pte >> 7))) &	/* _PAGE_DIRTY */
		  1));
}

/*
 * Handle a fault by adding an HPTE. If the address can't be determined
 * to be valid via Linux page tables, return 1. If handled return 0
 */
int __hash_page(unsigned long ea, unsigned long access, unsigned long vsid,
		pte_t *ptep, unsigned long trap)
{
	unsigned long va, vpn;
	unsigned long newpp, prpn;
	unsigned long hpteflags;
	long slot;
	pte_t old_pte, new_pte;

	/* Search the Linux page table for a match with va */
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;

	/*
	 * If no pte found or not present, send the problem up to
	 * do_page_fault
	 */
	if (unlikely(!ptep || !pte_present(*ptep)))
		return 1;

	/* 
	 * Check the user's access rights to the page.  If access should be
	 * prevented then send the problem up to do_page_fault.
	 */
	access |= _PAGE_PRESENT;
	if (unlikely(access & ~(pte_val(*ptep))))
		return 1;

	/*
	 * At this point, we have a pte (old_pte) which can be used to build
	 * or update an HPTE. There are 2 cases:
	 *
	 * 1. There is a valid (present) pte with no associated HPTE (this is 
	 *	the most common case)
	 * 2. There is a valid (present) pte with an associated HPTE. The
	 *	current values of the pp bits in the HPTE prevent access
	 *	because we are doing software DIRTY bit management and the
	 *	page is currently not DIRTY. 
	 */

	old_pte = *ptep;
	new_pte = old_pte;
	/* If the attempted access was a store */
	if (access & _PAGE_RW)
		pte_val(new_pte) |= _PAGE_ACCESSED | _PAGE_DIRTY;
	else
		pte_val(new_pte) |= _PAGE_ACCESSED;

	newpp = computeHptePP(pte_val(new_pte));

#define PPC64_HWNOEXEC (1 << 2)

	/* We do lazy icache flushing on POWER4 */
	if (unlikely(__is_processor(PV_POWER4) &&
	    pfn_valid(pte_pfn(new_pte)))) {
		struct page *page = pte_page(new_pte);

		/* page is dirty */
		if (!PageReserved(page) &&
		    !test_bit(PG_arch_1, &page->flags)) {
			if (trap == 0x400) {
				__flush_dcache_icache(page_address(page));
				set_bit(PG_arch_1, &page->flags);
			} else {
				newpp |= PPC64_HWNOEXEC;
			}
		}
	}

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(pte_val(old_pte) & _PAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot, secondary;

		/* XXX fix large pte flag */
		hash = hpt_hash(vpn, 0);
		secondary = (pte_val(old_pte) & _PAGE_SECONDARY) >> 15;
		if (secondary)
			hash = ~hash;
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		slot += (pte_val(old_pte) & _PAGE_GROUP_IX) >> 12;

		/* XXX fix large pte flag */
		if (ppc_md.hpte_updatepp(slot, newpp, va, 0) == -1)
			pte_val(old_pte) &= ~_PAGE_HPTEFLAGS;
		else
			if (!pte_same(old_pte, new_pte))
				*ptep = new_pte;
	}

	if (likely(!(pte_val(old_pte) & _PAGE_HASHPTE))) {
		/* XXX fix large pte flag */
		unsigned long hash = hpt_hash(vpn, 0);
		unsigned long hpte_group;
		prpn = pte_val(old_pte) >> PTE_SHIFT;

repeat:
		hpte_group = ((hash & htab_data.htab_hash_mask) *
			      HPTES_PER_GROUP) & ~0x7UL;

		/* Update the linux pte with the HPTE slot */
		pte_val(new_pte) &= ~_PAGE_HPTEFLAGS;
		pte_val(new_pte) |= _PAGE_HASHPTE;

		/* copy appropriate flags from linux pte */
		hpteflags = (pte_val(new_pte) & 0x1f8) | newpp;

		/* XXX fix large pte flag */
		slot = ppc_md.insert_hpte(hpte_group, vpn, prpn, 0,
					  hpteflags, 0, 0);

		/* Primary is full, try the secondary */
		if (slot == -1) {
			pte_val(new_pte) |= 1 << 15;
			hpte_group = ((~hash & htab_data.htab_hash_mask) *
				      HPTES_PER_GROUP) & ~0x7UL; 
			/* XXX fix large pte flag */
			slot = ppc_md.insert_hpte(hpte_group, vpn, prpn,
						  1, hpteflags, 0, 0);
			if (slot == -1) {
				if (mftb() & 0x1)
					hpte_group = ((hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP) & ~0x7UL;

				ppc_md.remove_hpte(hpte_group);
				goto repeat;
                        }
		}

		pte_val(new_pte) |= (slot<<12) & _PAGE_GROUP_IX;

		/* 
		 * No need to use ldarx/stdcx here because all who
		 * might be updating the pte will hold the
		 * page_table_lock or the hash_table_lock
		 * (we hold both)
		 */
		*ptep = new_pte;
	}

	return 0;
}

int hash_page(unsigned long ea, unsigned long access, unsigned long trap)
{
	void *pgdir;
	unsigned long vsid;
	struct mm_struct *mm;
	pte_t *ptep;
	int ret;

	/* Check for invalid addresses. */
	if (!IS_VALID_EA(ea))
		return 1;

 	switch (REGION_ID(ea)) {
	case USER_REGION_ID:
		mm = current->mm;
		if (mm == NULL)
			return 1;

		vsid = get_vsid(mm->context, ea);
		break;
	case IO_REGION_ID:
		mm = &ioremap_mm;
		vsid = get_kernel_vsid(ea);
		break;
	case VMALLOC_REGION_ID:
		mm = &init_mm;
		vsid = get_kernel_vsid(ea);
		break;
	case IO_UNMAPPED_REGION_ID:
		udbg_printf("EEH Error ea = 0x%lx\n", ea);
		PPCDBG_ENTER_DEBUGGER();
		panic("EEH Error ea = 0x%lx\n", ea);
		break;
	case KERNEL_REGION_ID:
		/*
		 * As htab_initialize is now, we shouldn't ever get here since
		 * we're bolting the entire 0xC0... region.
		 */
		udbg_printf("Little faulted on kernel address 0x%lx\n", ea);
		PPCDBG_ENTER_DEBUGGER();
		panic("Little faulted on kernel address 0x%lx\n", ea);
		break;
	default:
		/* Not a valid range, send the problem up to do_page_fault */
		return 1;
		break;
	}

	pgdir = mm->pgd;

	if (pgdir == NULL)
		return 1;

	/*
	 * Lock the Linux page table to prevent mmap and kswapd
	 * from modifying entries while we search and update
	 */
	spin_lock(&mm->page_table_lock);
	ptep = find_linux_pte(pgdir, ea);
	ret = __hash_page(ea, access, vsid, ptep, trap);
	spin_unlock(&mm->page_table_lock);

	return ret;
}

void flush_hash_page(unsigned long context, unsigned long ea, pte_t pte,
		     int local)
{
	unsigned long vsid, vpn, va, hash, secondary, slot;

	/* XXX fix for large ptes */
	unsigned long large = 0;

	if ((ea >= USER_START) && (ea <= USER_END))
		vsid = get_vsid(context, ea);
	else
		vsid = get_kernel_vsid(ea);

	va = (vsid << 28) | (ea & 0x0fffffff);
	if (large)
		vpn = va >> LARGE_PAGE_SHIFT;
	else
		vpn = va >> PAGE_SHIFT;
	hash = hpt_hash(vpn, large);
	secondary = (pte_val(pte) & _PAGE_SECONDARY) >> 15;
	if (secondary)
		hash = ~hash;
	slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
	slot += (pte_val(pte) & _PAGE_GROUP_IX) >> 12;

	ppc_md.hpte_invalidate(slot, va, large, local);
}

void flush_hash_range(unsigned long context, unsigned long number, int local)
{
	if (ppc_md.flush_hash_range) {
		ppc_md.flush_hash_range(context, number, local);
	} else {
		int i;
		struct ppc64_tlb_batch *batch =
			&ppc64_tlb_batch[smp_processor_id()];

		for (i = 0; i < number; i++)
			flush_hash_page(context, batch->addr[i], batch->pte[i],
					local);
	}
}
