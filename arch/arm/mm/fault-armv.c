/*
 *  linux/arch/arm/mm/fault-armv.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/bitops.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include "fault.h"

/*
 * Some section permission faults need to be handled gracefully.
 * They can happen due to a __{get,put}_user during an oops.
 */
static int
do_sect_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	do_bad_area(tsk, tsk->active_mm, addr, fsr, regs);
	return 0;
}

/*
 * This abort handler always returns "fault".
 */
static int
do_bad(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	return 1;
}

static struct fsr_info {
	int	(*fn)(unsigned long addr, unsigned int fsr, struct pt_regs *regs);
	int	sig;
	const char *name;
} fsr_info[] = {
	/*
	 * The following are the standard ARMv3 and ARMv4 aborts.  ARMv5
	 * defines these to be "precise" aborts.
	 */
	{ do_bad,		SIGSEGV, "vector exception"		   },
	{ do_bad,		SIGILL,	 "alignment exception"		   },
	{ do_bad,		SIGKILL, "terminal exception"		   },
	{ do_bad,		SIGILL,	 "alignment exception"		   },
	{ do_bad,		SIGBUS,	 "external abort on linefetch"	   },
	{ do_translation_fault,	SIGSEGV, "section translation fault"	   },
	{ do_bad,		SIGBUS,	 "external abort on linefetch"	   },
	{ do_page_fault,	SIGSEGV, "page translation fault"	   },
	{ do_bad,		SIGBUS,	 "external abort on non-linefetch" },
	{ do_bad,		SIGSEGV, "section domain fault"		   },
	{ do_bad,		SIGBUS,	 "external abort on non-linefetch" },
	{ do_bad,		SIGSEGV, "page domain fault"		   },
	{ do_bad,		SIGBUS,	 "external abort on translation"   },
	{ do_sect_fault,	SIGSEGV, "section permission fault"	   },
	{ do_bad,		SIGBUS,	 "external abort on translation"   },
	{ do_page_fault,	SIGSEGV, "page permission fault"	   },
	/*
	 * The following are "imprecise" aborts, which are signalled by bit
	 * 10 of the FSR, and may not be recoverable.  These are only
	 * supported if the CPU abort handler supports bit 10.
	 */
	{ do_bad,		SIGBUS,  "unknown 16"			   },
	{ do_bad,		SIGBUS,  "unknown 17"			   },
	{ do_bad,		SIGBUS,  "unknown 18"			   },
	{ do_bad,		SIGBUS,  "unknown 19"			   },
	{ do_bad,		SIGBUS,  "lock abort"			   }, /* xscale */
	{ do_bad,		SIGBUS,  "unknown 21"			   },
	{ do_bad,		SIGBUS,  "imprecise external abort"	   }, /* xscale */
	{ do_bad,		SIGBUS,  "unknown 23"			   },
	{ do_bad,		SIGBUS,  "dcache parity error"		   }, /* xscale */
	{ do_bad,		SIGBUS,  "unknown 25"			   },
	{ do_bad,		SIGBUS,  "unknown 26"			   },
	{ do_bad,		SIGBUS,  "unknown 27"			   },
	{ do_bad,		SIGBUS,  "unknown 28"			   },
	{ do_bad,		SIGBUS,  "unknown 29"			   },
	{ do_bad,		SIGBUS,  "unknown 30"			   },
	{ do_bad,		SIGBUS,  "unknown 31"			   }
};

void __init
hook_fault_code(int nr, int (*fn)(unsigned long, unsigned int, struct pt_regs *),
		int sig, const char *name)
{
	if (nr >= 0 && nr < ARRAY_SIZE(fsr_info)) {
		fsr_info[nr].fn   = fn;
		fsr_info[nr].sig  = sig;
		fsr_info[nr].name = name;
	}
}

/*
 * Dispatch a data abort to the relevant handler.
 */
asmlinkage void
do_DataAbort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	const struct fsr_info *inf = fsr_info + (fsr & 15) + ((fsr & (1 << 10)) >> 6);

	if (!inf->fn(addr, fsr, regs))
		return;

	printk(KERN_ALERT "Unhandled fault: %s (0x%03x) at 0x%08lx\n",
		inf->name, fsr, addr);
	force_sig(inf->sig, current);
	show_pte(current->mm, addr);
	die_if_kernel("Oops", regs, 0);
}

asmlinkage void
do_PrefetchAbort(unsigned long addr, struct pt_regs *regs)
{
	do_translation_fault(addr, 0, regs);
}

static unsigned long shared_pte_mask = L_PTE_CACHEABLE;

/*
 * We take the easy way out of this problem - we make the
 * PTE uncacheable.  However, we leave the write buffer on.
 */
static int adjust_pte(struct vm_area_struct *vma, unsigned long address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte, entry;
	int ret = 0;

	pgd = pgd_offset(vma->vm_mm, address);
	if (pgd_none(*pgd))
		goto no_pgd;
	if (pgd_bad(*pgd))
		goto bad_pgd;

	pmd = pmd_offset(pgd, address);
	if (pmd_none(*pmd))
		goto no_pmd;
	if (pmd_bad(*pmd))
		goto bad_pmd;

	pte = pte_offset_map(pmd, address);
	entry = *pte;

	/*
	 * If this page isn't present, or is already setup to
	 * fault (ie, is old), we can safely ignore any issues.
	 */
	if (pte_present(entry) && pte_val(entry) & shared_pte_mask) {
		flush_cache_page(vma, address);
		pte_val(entry) &= ~shared_pte_mask;
		set_pte(pte, entry);
		flush_tlb_page(vma, address);
		ret = 1;
	}
	pte_unmap(pte);
	return ret;

bad_pgd:
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
no_pgd:
	return 0;

bad_pmd:
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
no_pmd:
	return 0;
}

void __flush_dcache_page(struct page *page)
{
	struct mm_struct *mm = current->active_mm;
	struct list_head *l;

	__cpuc_flush_dcache_page(page_address(page));

	if (!page_mapping(page))
		return;

	/*
	 * With a VIVT cache, we need to also write back
	 * and invalidate any user data.
	 */
	list_for_each(l, &page->mapping->i_mmap_shared) {
		struct vm_area_struct *mpnt;
		unsigned long off;

		mpnt = list_entry(l, struct vm_area_struct, shared);

		/*
		 * If this VMA is not in our MM, we can ignore it.
		 */
		if (mpnt->vm_mm != mm)
			continue;

		if (page->index < mpnt->vm_pgoff)
			continue;

		off = page->index - mpnt->vm_pgoff;
		if (off >= (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT)
			continue;

		flush_cache_page(mpnt, mpnt->vm_start + (off << PAGE_SHIFT));
	}
}

static void
make_coherent(struct vm_area_struct *vma, unsigned long addr, struct page *page, int dirty)
{
	struct list_head *l;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long pgoff;
	int aliases = 0;

	pgoff = vma->vm_pgoff + ((addr - vma->vm_start) >> PAGE_SHIFT);

	/*
	 * If we have any shared mappings that are in the same mm
	 * space, then we need to handle them specially to maintain
	 * cache coherency.
	 */
	list_for_each(l, &page->mapping->i_mmap_shared) {
		struct vm_area_struct *mpnt;
		unsigned long off;

		mpnt = list_entry(l, struct vm_area_struct, shared);

		/*
		 * If this VMA is not in our MM, we can ignore it.
		 * Note that we intentionally mask out the VMA
		 * that we are fixing up.
		 */
		if (mpnt->vm_mm != mm || mpnt == vma)
			continue;

		/*
		 * If the page isn't in this VMA, we can also ignore it.
		 */
		if (pgoff < mpnt->vm_pgoff)
			continue;

		off = pgoff - mpnt->vm_pgoff;
		if (off >= (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT)
			continue;

		off = mpnt->vm_start + (off << PAGE_SHIFT);

		/*
		 * Ok, it is within mpnt.  Fix it up.
		 */
		aliases += adjust_pte(mpnt, off);
	}
	if (aliases)
		adjust_pte(vma, addr);
	else
		flush_cache_page(vma, addr);
}

/*
 * Take care of architecture specific things when placing a new PTE into
 * a page table, or changing an existing PTE.  Basically, there are two
 * things that we need to take care of:
 *
 *  1. If PG_dcache_dirty is set for the page, we need to ensure
 *     that any cache entries for the kernels virtual memory
 *     range are written back to the page.
 *  2. If we have multiple shared mappings of the same space in
 *     an object, we need to deal with the cache aliasing issues.
 *
 * Note that the page_table_lock will be held.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	unsigned long pfn = pte_pfn(pte);
	struct page *page;

	if (!pfn_valid(pfn))
		return;
	page = pfn_to_page(pfn);
	if (page_mapping(page)) {
		int dirty = test_and_clear_bit(PG_dcache_dirty, &page->flags);

		if (dirty)
			__cpuc_flush_dcache_page(page_address(page));

		make_coherent(vma, addr, page, dirty);
	}
}

/*
 * Check whether the write buffer has physical address aliasing
 * issues.  If it has, we need to avoid them for the case where
 * we have several shared mappings of the same object in user
 * space.
 */
static int __init check_writebuffer(unsigned long *p1, unsigned long *p2)
{
	register unsigned long zero = 0, one = 1, val;

	local_irq_disable();
	mb();
	*p1 = one;
	mb();
	*p2 = zero;
	mb();
	val = *p1;
	mb();
	local_irq_enable();
	return val != zero;
}

void __init check_writebuffer_bugs(void)
{
	struct page *page;
	const char *reason;
	unsigned long v = 1;

	printk(KERN_INFO "CPU: Testing write buffer coherency: ");

	page = alloc_page(GFP_KERNEL);
	if (page) {
		unsigned long *p1, *p2;
		pgprot_t prot = __pgprot(L_PTE_PRESENT|L_PTE_YOUNG|
					 L_PTE_DIRTY|L_PTE_WRITE|
					 L_PTE_BUFFERABLE);

		p1 = vmap(&page, 1, VM_IOREMAP, prot);
		p2 = vmap(&page, 1, VM_IOREMAP, prot);

		if (p1 && p2) {
			v = check_writebuffer(p1, p2);
			reason = "enabling work-around";
		} else {
			reason = "unable to map memory\n";
		}

		vunmap(p1);
		vunmap(p2);
		put_page(page);
	} else {
		reason = "unable to grab page\n";
	}

	if (v) {
		printk("failed, %s\n", reason);
		shared_pte_mask |= L_PTE_BUFFERABLE;
	} else {
		printk("ok\n");
	}
}
