/*  $Id: init.c,v 1.161 2000/12/09 20:16:58 davem Exp $
 *  arch/sparc64/mm/init.c
 *
 *  Copyright (C) 1996-1999 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997-1999 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/blk.h>
#include <linux/swap.h>
#include <linux/swapctl.h>

#include <asm/head.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/iommu.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/vaddrs.h>
#include <asm/dma.h>
#include <asm/starfire.h>

extern void device_scan(void);

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

unsigned long *sparc64_valid_addr_bitmap;

/* Ugly, but necessary... -DaveM */
unsigned long phys_base;

/* get_new_mmu_context() uses "cache + 1".  */
spinlock_t ctx_alloc_lock = SPIN_LOCK_UNLOCKED;
unsigned long tlb_context_cache = CTX_FIRST_VERSION - 1;
#define CTX_BMAP_SLOTS (1UL << (CTX_VERSION_SHIFT - 6))
unsigned long mmu_context_bmap[CTX_BMAP_SLOTS];

/* References to section boundaries */
extern char __init_begin, __init_end, _start, _end, etext, edata;

/* Initial ramdisk setup */
extern unsigned int sparc_ramdisk_image;
extern unsigned int sparc_ramdisk_size;

int do_check_pgt_cache(int low, int high)
{
        int freed = 0;

	if(pgtable_cache_size > high) {
		do {
#ifdef CONFIG_SMP
			if(pgd_quicklist)
				free_pgd_slow(get_pgd_fast()), freed++;
#endif
			if(pte_quicklist[0])
				free_pte_slow(get_pte_fast(0)), freed++;
			if(pte_quicklist[1])
				free_pte_slow(get_pte_fast(1)), freed++;
		} while(pgtable_cache_size > low);
	}
#ifndef CONFIG_SMP 
        if (pgd_cache_size > high / 4) {
		struct page *page, *page2;
                for (page2 = NULL, page = (struct page *)pgd_quicklist; page;) {
                        if ((unsigned long)page->pprev_hash == 3) {
                                if (page2)
                                        page2->next_hash = page->next_hash;
                                else
                                        (struct page *)pgd_quicklist = page->next_hash;
                                page->next_hash = NULL;
                                page->pprev_hash = NULL;
                                pgd_cache_size -= 2;
                                __free_page(page);
                                freed++;
                                if (page2)
                                        page = page2->next_hash;
                                else
                                        page = (struct page *)pgd_quicklist;
                                if (pgd_cache_size <= low / 4)
                                        break;
                                continue;
                        }
                        page2 = page;
                        page = page->next_hash;
                }
        }
#endif
        return freed;
}

extern void __update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

void update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	struct page *page = pte_page(pte);

	if (VALID_PAGE(page) && page->mapping &&
	    test_bit(PG_dcache_dirty, &page->flags)) {
		__flush_dcache_page(page->virtual, 1);
		clear_bit(PG_dcache_dirty, &page->flags);
	}
	__update_mmu_cache(vma, address, pte);
}

/* In arch/sparc64/mm/ultra.S */
extern void __flush_icache_page(unsigned long);

void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long kaddr;

	for (kaddr = start; kaddr < end; kaddr += PAGE_SIZE)
		__flush_icache_page(__get_phys(kaddr));
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t __bad_page(void)
{
	memset((void *) &empty_bad_page, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte_phys((((unsigned long) &empty_bad_page) 
					- ((unsigned long)&empty_zero_page)
					+ phys_base),
				       PAGE_SHARED));
}

void show_mem(void)
{
	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",
	       nr_swap_pages << (PAGE_SHIFT-10));
	printk("%ld pages of RAM\n", num_physpages);
	printk("%d free pages\n", nr_free_pages());
	printk("%d pages in page table cache\n",pgtable_cache_size);
#ifndef CONFIG_SMP
	printk("%d entries in page dir cache\n",pgd_cache_size);
#endif	
	show_buffers();
}

int mmu_info(char *buf)
{
	/* We'll do the rest later to make it nice... -DaveM */
#if 0
	if (this_is_cheetah)
		sprintf(buf, "MMU Type\t: One bad ass cpu\n");
	else
#endif
	return sprintf(buf, "MMU Type\t: Spitfire\n");
}

struct linux_prom_translation {
	unsigned long virt;
	unsigned long size;
	unsigned long data;
};

extern unsigned long prom_boot_page;
extern void prom_remap(unsigned long physpage, unsigned long virtpage, int mmu_ihandle);
extern int prom_get_mmu_ihandle(void);
extern void register_prom_callbacks(void);

/* Exported for SMP bootup purposes. */
unsigned long kern_locked_tte_data;

void __init early_pgtable_allocfail(char *type)
{
	prom_printf("inherit_prom_mappings: Cannot alloc kernel %s.\n", type);
	prom_halt();
}

static void inherit_prom_mappings(void)
{
	struct linux_prom_translation *trans;
	unsigned long phys_page, tte_vaddr, tte_data;
	void (*remap_func)(unsigned long, unsigned long, int);
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int node, n, i, tsz;

	node = prom_finddevice("/virtual-memory");
	n = prom_getproplen(node, "translations");
	if (n == 0 || n == -1) {
		prom_printf("Couldn't get translation property\n");
		prom_halt();
	}
	n += 5 * sizeof(struct linux_prom_translation);
	for (tsz = 1; tsz < n; tsz <<= 1)
		/* empty */;
	trans = __alloc_bootmem(tsz, SMP_CACHE_BYTES, 0UL);
	if (trans == NULL) {
		prom_printf("inherit_prom_mappings: Cannot alloc translations.\n");
		prom_halt();
	}
	memset(trans, 0, tsz);

	if ((n = prom_getproperty(node, "translations", (char *)trans, tsz)) == -1) {
		prom_printf("Couldn't get translation property\n");
		prom_halt();
	}
	n = n / sizeof(*trans);

	for (i = 0; i < n; i++) {
		unsigned long vaddr;

		if (trans[i].virt >= 0xf0000000 && trans[i].virt < 0x100000000) {
			for (vaddr = trans[i].virt;
			     vaddr < trans[i].virt + trans[i].size;
			     vaddr += PAGE_SIZE) {
				pgdp = pgd_offset(&init_mm, vaddr);
				if (pgd_none(*pgdp)) {
					pmdp = __alloc_bootmem(PMD_TABLE_SIZE,
							       PMD_TABLE_SIZE,
							       0UL);
					if (pmdp == NULL)
						early_pgtable_allocfail("pmd");
					memset(pmdp, 0, PMD_TABLE_SIZE);
					pgd_set(pgdp, pmdp);
				}
				pmdp = pmd_offset(pgdp, vaddr);
				if (pmd_none(*pmdp)) {
					ptep = __alloc_bootmem(PTE_TABLE_SIZE,
							       PTE_TABLE_SIZE,
							       0UL);
					if (ptep == NULL)
						early_pgtable_allocfail("pte");
					memset(ptep, 0, PTE_TABLE_SIZE);
					pmd_set(pmdp, ptep);
				}
				ptep = pte_offset(pmdp, vaddr);
				set_pte (ptep, __pte(trans[i].data | _PAGE_MODIFIED));
				trans[i].data += PAGE_SIZE;
			}
		}
	}

	/* Now fixup OBP's idea about where we really are mapped. */
	prom_printf("Remapping the kernel... ");

	/* Spitfire Errata #32 workaround */
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "flush	%%g6"
			     : /* No outputs */
			     : "r" (0),
			     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

	phys_page = spitfire_get_dtlb_data(63) & _PAGE_PADDR;
	phys_page += ((unsigned long)&prom_boot_page -
		      (unsigned long)&empty_zero_page);

	/* Lock this into i/d tlb entry 59 */
	__asm__ __volatile__(
		"stxa	%%g0, [%2] %3\n\t"
		"stxa	%0, [%1] %4\n\t"
		"membar	#Sync\n\t"
		"flush	%%g6\n\t"
		"stxa	%%g0, [%2] %5\n\t"
		"stxa	%0, [%1] %6\n\t"
		"membar	#Sync\n\t"
		"flush	%%g6"
		: : "r" (phys_page | _PAGE_VALID | _PAGE_SZ8K | _PAGE_CP |
			 _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W),
		    "r" (59 << 3), "r" (TLB_TAG_ACCESS),
		    "i" (ASI_DMMU), "i" (ASI_DTLB_DATA_ACCESS),
		    "i" (ASI_IMMU), "i" (ASI_ITLB_DATA_ACCESS)
		: "memory");

	tte_vaddr = (unsigned long) &empty_zero_page;

	/* Spitfire Errata #32 workaround */
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "flush	%%g6"
			     : /* No outputs */
			     : "r" (0),
			     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

	kern_locked_tte_data = tte_data = spitfire_get_dtlb_data(63);

	remap_func = (void *)  ((unsigned long) &prom_remap -
				(unsigned long) &prom_boot_page);


	/* Spitfire Errata #32 workaround */
	__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
			     "flush	%%g6"
			     : /* No outputs */
			     : "r" (0),
			     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

	remap_func(spitfire_get_dtlb_data(63) & _PAGE_PADDR,
		   (unsigned long) &empty_zero_page,
		   prom_get_mmu_ihandle());

	/* Flush out that temporary mapping. */
	spitfire_flush_dtlb_nucleus_page(0x0);
	spitfire_flush_itlb_nucleus_page(0x0);

	/* Now lock us back into the TLBs via OBP. */
	prom_dtlb_load(63, tte_data, tte_vaddr);
	prom_itlb_load(63, tte_data, tte_vaddr);

	/* Re-read translations property. */
	if ((n = prom_getproperty(node, "translations", (char *)trans, tsz)) == -1) {
		prom_printf("Couldn't get translation property\n");
		prom_halt();
	}
	n = n / sizeof(*trans);

	for (i = 0; i < n; i++) {
		unsigned long vaddr = trans[i].virt;
		unsigned long size = trans[i].size;

		if (vaddr < 0xf0000000UL) {
			unsigned long avoid_start = (unsigned long) &empty_zero_page;
			unsigned long avoid_end = avoid_start + (4 * 1024 * 1024);

			if (vaddr < avoid_start) {
				unsigned long top = vaddr + size;

				if (top > avoid_start)
					top = avoid_start;
				prom_unmap(top - vaddr, vaddr);
			}
			if ((vaddr + size) > avoid_end) {
				unsigned long bottom = vaddr;

				if (bottom < avoid_end)
					bottom = avoid_end;
				prom_unmap((vaddr + size) - bottom, bottom);
			}
		}
	}

	prom_printf("done.\n");

	register_prom_callbacks();
}

/* The OBP specifications for sun4u mark 0xfffffffc00000000 and
 * upwards as reserved for use by the firmware (I wonder if this
 * will be the same on Cheetah...).  We use this virtual address
 * range for the VPTE table mappings of the nucleus so we need
 * to zap them when we enter the PROM.  -DaveM
 */
static void __flush_nucleus_vptes(void)
{
	unsigned long prom_reserved_base = 0xfffffffc00000000UL;
	int i;

	/* Only DTLB must be checked for VPTE entries. */
	for(i = 0; i < 63; i++) {
		unsigned long tag;

		/* Spitfire Errata #32 workaround */
		__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
				     "flush	%%g6"
				     : /* No outputs */
				     : "r" (0),
				     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

		tag = spitfire_get_dtlb_tag(i);
		if(((tag & ~(PAGE_MASK)) == 0) &&
		   ((tag &  (PAGE_MASK)) >= prom_reserved_base)) {
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : /* no outputs */
					     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");
		}
	}
}

static int prom_ditlb_set = 0;
struct prom_tlb_entry {
	int		tlb_ent;
	unsigned long	tlb_tag;
	unsigned long	tlb_data;
};
struct prom_tlb_entry prom_itlb[8], prom_dtlb[8];

void prom_world(int enter)
{
	unsigned long pstate;
	int i;

	if (!enter)
		set_fs(current->thread.current_ds);

	if (!prom_ditlb_set)
		return;

	/* Make sure the following runs atomically. */
	__asm__ __volatile__("flushw\n\t"
			     "rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	if (enter) {
		/* Kick out nucleus VPTEs. */
		__flush_nucleus_vptes();

		/* Install PROM world. */
		for (i = 0; i < 8; i++) {
			if (prom_dtlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %0, [%1] %2"
					: : "r" (prom_dtlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
					"i" (ASI_DMMU));
				membar("#Sync");
				spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent,
						       prom_dtlb[i].tlb_data);
				membar("#Sync");
			}

			if (prom_itlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %0, [%1] %2"
					: : "r" (prom_itlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
					"i" (ASI_IMMU));
				membar("#Sync");
				spitfire_put_itlb_data(prom_itlb[i].tlb_ent,
						       prom_itlb[i].tlb_data);
				membar("#Sync");
			}
		}
	} else {
		for (i = 0; i < 8; i++) {
			if (prom_dtlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %%g0, [%0] %1"
					: : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
				membar("#Sync");
				spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent, 0x0UL);
				membar("#Sync");
			}
			if (prom_itlb[i].tlb_ent != -1) {
				__asm__ __volatile__("stxa %%g0, [%0] %1"
					: : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
				membar("#Sync");
				spitfire_put_itlb_data(prom_itlb[i].tlb_ent, 0x0UL);
				membar("#Sync");
			}
		}
	}
	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (pstate));
}

void inherit_locked_prom_mappings(int save_p)
{
	int i;
	int dtlb_seen = 0;
	int itlb_seen = 0;

	/* Fucking losing PROM has more mappings in the TLB, but
	 * it (conveniently) fails to mention any of these in the
	 * translations property.  The only ones that matter are
	 * the locked PROM tlb entries, so we impose the following
	 * irrecovable rule on the PROM, it is allowed 8 locked
	 * entries in the ITLB and 8 in the DTLB.
	 *
	 * Supposedly the upper 16GB of the address space is
	 * reserved for OBP, BUT I WISH THIS WAS DOCUMENTED
	 * SOMEWHERE!!!!!!!!!!!!!!!!!  Furthermore the entire interface
	 * used between the client program and the firmware on sun5
	 * systems to coordinate mmu mappings is also COMPLETELY
	 * UNDOCUMENTED!!!!!! Thanks S(t)un!
	 */
	if (save_p) {
		for(i = 0; i < 8; i++) {
			prom_dtlb[i].tlb_ent = -1;
			prom_itlb[i].tlb_ent = -1;
		}
	}
	for(i = 0; i < 63; i++) {
		unsigned long data;


		/* Spitfire Errata #32 workaround */
		__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
				     "flush	%%g6"
				     : /* No outputs */
				     : "r" (0),
				     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

		data = spitfire_get_dtlb_data(i);
		if((data & (_PAGE_L|_PAGE_VALID)) == (_PAGE_L|_PAGE_VALID)) {
			unsigned long tag;

			/* Spitfire Errata #32 workaround */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (0),
					     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

			tag = spitfire_get_dtlb_tag(i);
			if(save_p) {
				prom_dtlb[dtlb_seen].tlb_ent = i;
				prom_dtlb[dtlb_seen].tlb_tag = tag;
				prom_dtlb[dtlb_seen].tlb_data = data;
			}
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");

			dtlb_seen++;
			if(dtlb_seen > 7)
				break;
		}
	}
	for(i = 0; i < 63; i++) {
		unsigned long data;

		/* Spitfire Errata #32 workaround */
		__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
				     "flush	%%g6"
				     : /* No outputs */
				     : "r" (0),
				     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

		data = spitfire_get_itlb_data(i);
		if((data & (_PAGE_L|_PAGE_VALID)) == (_PAGE_L|_PAGE_VALID)) {
			unsigned long tag;

			/* Spitfire Errata #32 workaround */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (0),
					     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

			tag = spitfire_get_itlb_tag(i);
			if(save_p) {
				prom_itlb[itlb_seen].tlb_ent = i;
				prom_itlb[itlb_seen].tlb_tag = tag;
				prom_itlb[itlb_seen].tlb_data = data;
			}
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(i, 0x0UL);
			membar("#Sync");

			itlb_seen++;
			if(itlb_seen > 7)
				break;
		}
	}
	if (save_p)
		prom_ditlb_set = 1;
}

/* Give PROM back his world, done during reboots... */
void prom_reload_locked(void)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (prom_dtlb[i].tlb_ent != -1) {
			__asm__ __volatile__("stxa %0, [%1] %2"
				: : "r" (prom_dtlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
				"i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(prom_dtlb[i].tlb_ent,
					       prom_dtlb[i].tlb_data);
			membar("#Sync");
		}

		if (prom_itlb[i].tlb_ent != -1) {
			__asm__ __volatile__("stxa %0, [%1] %2"
				: : "r" (prom_itlb[i].tlb_tag), "r" (TLB_TAG_ACCESS),
				"i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(prom_itlb[i].tlb_ent,
					       prom_itlb[i].tlb_data);
			membar("#Sync");
		}
	}
}

void __flush_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long va;
	int n = 0;

	for (va = start; va < end; va += 32) {
		spitfire_put_dcache_tag(va & 0x3fe0, 0x0);
		if (++n >= 512)
			break;
	}
}

void __flush_cache_all(void)
{
	unsigned long va;

	flushw_all();
	for(va =  0; va < (PAGE_SIZE << 1); va += 32)
		spitfire_put_icache_tag(va, 0x0);
}

/* If not locked, zap it. */
void __flush_tlb_all(void)
{
	unsigned long pstate;
	int i;

	__asm__ __volatile__("flushw\n\t"
			     "rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));
	for(i = 0; i < 64; i++) {
		/* Spitfire Errata #32 workaround */
		__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
				     "flush	%%g6"
				     : /* No outputs */
				     : "r" (0),
				     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

		if(!(spitfire_get_dtlb_data(i) & _PAGE_L)) {
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : /* no outputs */
					     : "r" (TLB_TAG_ACCESS), "i" (ASI_DMMU));
			membar("#Sync");
			spitfire_put_dtlb_data(i, 0x0UL);
			membar("#Sync");
		}

		/* Spitfire Errata #32 workaround */
		__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
				     "flush	%%g6"
				     : /* No outputs */
				     : "r" (0),
				     "r" (PRIMARY_CONTEXT), "i" (ASI_DMMU));

		if(!(spitfire_get_itlb_data(i) & _PAGE_L)) {
			__asm__ __volatile__("stxa %%g0, [%0] %1"
					     : /* no outputs */
					     : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU));
			membar("#Sync");
			spitfire_put_itlb_data(i, 0x0UL);
			membar("#Sync");
		}
	}
	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (pstate));
}

/* Caller does TLB context flushing on local CPU if necessary.
 *
 * We must be careful about boundary cases so that we never
 * let the user have CTX 0 (nucleus) or we ever use a CTX
 * version of zero (and thus NO_CONTEXT would not be caught
 * by version mis-match tests in mmu_context.h).
 */
void get_new_mmu_context(struct mm_struct *mm)
{
	unsigned long ctx, new_ctx;
	
	spin_lock(&ctx_alloc_lock);
	ctx = CTX_HWBITS(tlb_context_cache + 1);
	if (ctx == 0)
		ctx = 1;
	if (CTX_VALID(mm->context)) {
		unsigned long nr = CTX_HWBITS(mm->context);
		mmu_context_bmap[nr>>6] &= ~(1UL << (nr & 63));
	}
	new_ctx = find_next_zero_bit(mmu_context_bmap, 1UL << CTX_VERSION_SHIFT, ctx);
	if (new_ctx >= (1UL << CTX_VERSION_SHIFT)) {
		new_ctx = find_next_zero_bit(mmu_context_bmap, ctx, 1);
		if (new_ctx >= ctx) {
			int i;
			new_ctx = (tlb_context_cache & CTX_VERSION_MASK) +
				CTX_FIRST_VERSION;
			if (new_ctx == 1)
				new_ctx = CTX_FIRST_VERSION;

			/* Don't call memset, for 16 entries that's just
			 * plain silly...
			 */
			mmu_context_bmap[0] = 3;
			mmu_context_bmap[1] = 0;
			mmu_context_bmap[2] = 0;
			mmu_context_bmap[3] = 0;
			for(i = 4; i < CTX_BMAP_SLOTS; i += 4) {
				mmu_context_bmap[i + 0] = 0;
				mmu_context_bmap[i + 1] = 0;
				mmu_context_bmap[i + 2] = 0;
				mmu_context_bmap[i + 3] = 0;
			}
			goto out;
		}
	}
	mmu_context_bmap[new_ctx>>6] |= (1UL << (new_ctx & 63));
	new_ctx |= (tlb_context_cache & CTX_VERSION_MASK);
out:
	tlb_context_cache = new_ctx;
	spin_unlock(&ctx_alloc_lock);

	mm->context = new_ctx;
}

#ifndef CONFIG_SMP
struct pgtable_cache_struct pgt_quicklists;
#endif

/* For PMDs we don't care about the color, writes are
 * only done via Dcache which is write-thru, so non-Dcache
 * reads will always see correct data.
 */
pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset)
{
	pmd_t *pmd;

	pmd = (pmd_t *) __get_free_page(GFP_KERNEL);
	if(pmd) {
		memset(pmd, 0, PAGE_SIZE);
		pgd_set(pgd, pmd);
		return pmd + offset;
	}
	return NULL;
}

/* OK, we have to color these pages because during DTLB
 * protection faults we set the dirty bit via a non-Dcache
 * enabled mapping in the VPTE area.  The kernel can end
 * up missing the dirty bit resulting in processes crashing
 * _iff_ the VPTE mapping of the ptes have a virtual address
 * bit 13 which is different from bit 13 of the physical address.
 *
 * The sequence is:
 *	1) DTLB protection fault, write dirty bit into pte via VPTE
 *	   mappings.
 *	2) Swapper checks pte, does not see dirty bit, frees page.
 *	3) Process faults back in the page, the old pre-dirtied copy
 *	   is provided and here is the corruption.
 */
pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset, unsigned long color)
{
	struct page *page = alloc_pages(GFP_KERNEL, 1);

	if (page) {
		unsigned long *to_free;
		unsigned long paddr;
		pte_t *pte;

		set_page_count((page + 1), 1);
		paddr = (unsigned long) page_address(page);
		memset((char *)paddr, 0, (PAGE_SIZE << 1));

		if (!color) {
			pte = (pte_t *) paddr;
			to_free = (unsigned long *) (paddr + PAGE_SIZE);
		} else {
			pte = (pte_t *) (paddr + PAGE_SIZE);
			to_free = (unsigned long *) paddr;
		}

		/* Now free the other one up, adjust cache size. */
		*to_free = (unsigned long) pte_quicklist[color ^ 0x1];
		pte_quicklist[color ^ 0x1] = to_free;
		pgtable_cache_size++;

		pmd_set(pmd, pte);
		return pte + offset;
	}
	return NULL;
}

void sparc_ultra_dump_itlb(void)
{
        int slot;

        printk ("Contents of itlb: ");
	for (slot = 0; slot < 14; slot++) printk ("    ");
	printk ("%2x:%016lx,%016lx\n", 0, spitfire_get_itlb_tag(0), spitfire_get_itlb_data(0));
        for (slot = 1; slot < 64; slot+=3) {
        	printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx %2x:%016lx,%016lx\n", 
        		slot, spitfire_get_itlb_tag(slot), spitfire_get_itlb_data(slot),
        		slot+1, spitfire_get_itlb_tag(slot+1), spitfire_get_itlb_data(slot+1),
        		slot+2, spitfire_get_itlb_tag(slot+2), spitfire_get_itlb_data(slot+2));
        }
}

void sparc_ultra_dump_dtlb(void)
{
        int slot;

        printk ("Contents of dtlb: ");
	for (slot = 0; slot < 14; slot++) printk ("    ");
	printk ("%2x:%016lx,%016lx\n", 0, spitfire_get_dtlb_tag(0),
		spitfire_get_dtlb_data(0));
        for (slot = 1; slot < 64; slot+=3) {
        	printk ("%2x:%016lx,%016lx %2x:%016lx,%016lx %2x:%016lx,%016lx\n", 
        		slot, spitfire_get_dtlb_tag(slot), spitfire_get_dtlb_data(slot),
        		slot+1, spitfire_get_dtlb_tag(slot+1), spitfire_get_dtlb_data(slot+1),
        		slot+2, spitfire_get_dtlb_tag(slot+2), spitfire_get_dtlb_data(slot+2));
        }
}

extern unsigned long cmdline_memory_size;

unsigned long __init bootmem_init(unsigned long *pages_avail)
{
	unsigned long bootmap_size, start_pfn, end_pfn;
	unsigned long end_of_phys_memory = 0UL;
	unsigned long bootmap_pfn, bytes_avail, size;
	int i;

	bytes_avail = 0UL;
	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		end_of_phys_memory = sp_banks[i].base_addr +
			sp_banks[i].num_bytes;
		bytes_avail += sp_banks[i].num_bytes;
		if (cmdline_memory_size) {
			if (bytes_avail > cmdline_memory_size) {
				unsigned long slack = bytes_avail - cmdline_memory_size;

				bytes_avail -= slack;
				end_of_phys_memory -= slack;

				sp_banks[i].num_bytes -= slack;
				if (sp_banks[i].num_bytes == 0) {
					sp_banks[i].base_addr = 0xdeadbeef;
				} else {
					sp_banks[i+1].num_bytes = 0;
					sp_banks[i+1].base_addr = 0xdeadbeef;
				}
				break;
			}
		}
	}

	*pages_avail = bytes_avail >> PAGE_SHIFT;

	/* Start with page aligned address of last symbol in kernel
	 * image.  The kernel is hard mapped below PAGE_OFFSET in a
	 * 4MB locked TLB translation.
	 */
	start_pfn  = PAGE_ALIGN((unsigned long) &_end) -
		((unsigned long) &empty_zero_page);

	/* Adjust up to the physical address where the kernel begins. */
	start_pfn += phys_base;

	/* Now shift down to get the real physical page frame number. */
	start_pfn >>= PAGE_SHIFT;
	
	bootmap_pfn = start_pfn;

	end_pfn = end_of_phys_memory >> PAGE_SHIFT;

#ifdef CONFIG_BLK_DEV_INITRD
	/* Now have to check initial ramdisk, so that bootmap does not overwrite it */
	if (sparc_ramdisk_image) {
		if (sparc_ramdisk_image >= (unsigned long)&_end - 2 * PAGE_SIZE)
			sparc_ramdisk_image -= KERNBASE;
		initrd_start = sparc_ramdisk_image + phys_base;
		initrd_end = initrd_start + sparc_ramdisk_size;
		if (initrd_end > end_of_phys_memory) {
			printk(KERN_CRIT "initrd extends beyond end of memory "
		                 	 "(0x%016lx > 0x%016lx)\ndisabling initrd\n",
			       initrd_end, end_of_phys_memory);
			initrd_start = 0;
		}
		if (initrd_start) {
			if (initrd_start >= (start_pfn << PAGE_SHIFT) &&
			    initrd_start < (start_pfn << PAGE_SHIFT) + 2 * PAGE_SIZE)
				bootmap_pfn = PAGE_ALIGN (initrd_end) >> PAGE_SHIFT;
		}
	}
#endif	
	/* Initialize the boot-time allocator. */
	bootmap_size = init_bootmem_node(NODE_DATA(0), bootmap_pfn, phys_base>>PAGE_SHIFT, end_pfn);

	/* Now register the available physical memory with the
	 * allocator.
	 */
	for (i = 0; sp_banks[i].num_bytes != 0; i++)
		free_bootmem(sp_banks[i].base_addr,
			     sp_banks[i].num_bytes);

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		size = initrd_end - initrd_start;

		/* Resert the initrd image area. */
		reserve_bootmem(initrd_start, size);
		*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

		initrd_start += PAGE_OFFSET;
		initrd_end += PAGE_OFFSET;
	}
#endif
	/* Reserve the kernel text/data/bss. */
	size = (start_pfn << PAGE_SHIFT) - phys_base;
	reserve_bootmem(phys_base, size);
	*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

	/* Reserve the bootmem map.   We do not account for it
	 * in pages_avail because we will release that memory
	 * in free_all_bootmem.
	 */
	size = bootmap_size;
	reserve_bootmem((bootmap_pfn << PAGE_SHIFT), size);
	*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

	return end_pfn;
}

/* paging_init() sets up the page tables */

extern void sun_serial_setup(void);

static unsigned long last_valid_pfn;

void __init paging_init(void)
{
	extern pmd_t swapper_pmd_dir[1024];
	extern unsigned int sparc64_vpte_patchme1[1];
	extern unsigned int sparc64_vpte_patchme2[1];
	unsigned long alias_base = phys_base + PAGE_OFFSET;
	unsigned long second_alias_page = 0;
	unsigned long pt, flags, end_pfn, pages_avail;
	unsigned long shift = alias_base - ((unsigned long)&empty_zero_page);

	set_bit(0, mmu_context_bmap);
	/* We assume physical memory starts at some 4mb multiple,
	 * if this were not true we wouldn't boot up to this point
	 * anyways.
	 */
	pt  = phys_base | _PAGE_VALID | _PAGE_SZ4MB;
	pt |= _PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W;
	__save_and_cli(flags);
	__asm__ __volatile__("
	stxa	%1, [%0] %3
	stxa	%2, [%5] %4
	membar	#Sync
	flush	%%g6
	nop
	nop
	nop"
	: /* No outputs */
	: "r" (TLB_TAG_ACCESS), "r" (alias_base), "r" (pt),
	  "i" (ASI_DMMU), "i" (ASI_DTLB_DATA_ACCESS), "r" (61 << 3)
	: "memory");
	if (((unsigned long)&_end) >= KERNBASE + 0x340000) {
		second_alias_page = alias_base + 0x400000;
		__asm__ __volatile__("
		stxa	%1, [%0] %3
		stxa	%2, [%5] %4
		membar	#Sync
		flush	%%g6
		nop
		nop
		nop"
		: /* No outputs */
		: "r" (TLB_TAG_ACCESS), "r" (second_alias_page), "r" (pt + 0x400000),
		  "i" (ASI_DMMU), "i" (ASI_DTLB_DATA_ACCESS), "r" (60 << 3)
		: "memory");
	}
	__restore_flags(flags);
	
	/* Now set kernel pgd to upper alias so physical page computations
	 * work.
	 */
	init_mm.pgd += ((shift) / (sizeof(pgd_t)));
	
	memset(swapper_pmd_dir, 0, sizeof(swapper_pmd_dir));

	/* Now can init the kernel/bad page tables. */
	pgd_set(&swapper_pg_dir[0], swapper_pmd_dir + (shift / sizeof(pgd_t)));
	
	sparc64_vpte_patchme1[0] |= (pgd_val(init_mm.pgd[0]) >> 10);
	sparc64_vpte_patchme2[0] |= (pgd_val(init_mm.pgd[0]) & 0x3ff);
	flushi((long)&sparc64_vpte_patchme1[0]);
	
	/* Setup bootmem... */
	pages_avail = 0;
	last_valid_pfn = end_pfn = bootmem_init(&pages_avail);

#ifdef CONFIG_SUN_SERIAL
	/* This does not logically belong here, but we need to
	 * call it at the moment we are able to use the bootmem
	 * allocator.
	 */
	sun_serial_setup();
#endif

	/* Inherit non-locked OBP mappings. */
	inherit_prom_mappings();
	
	/* Ok, we can use our TLB miss and window trap handlers safely.
	 * We need to do a quick peek here to see if we are on StarFire
	 * or not, so setup_tba can setup the IRQ globals correctly (it
	 * needs to get the hard smp processor id correctly).
	 */
	{
		extern void setup_tba(int);
		setup_tba(this_is_starfire);
	}

	inherit_locked_prom_mappings(1);
	
	/* We only created DTLB mapping of this stuff. */
	spitfire_flush_dtlb_nucleus_page(alias_base);
	if (second_alias_page)
		spitfire_flush_dtlb_nucleus_page(second_alias_page);

	flush_tlb_all();

	{
		unsigned long zones_size[MAX_NR_ZONES];
		unsigned long zholes_size[MAX_NR_ZONES];
		unsigned long npages;
		int znum;

		for (znum = 0; znum < MAX_NR_ZONES; znum++)
			zones_size[znum] = zholes_size[znum] = 0;

		npages = end_pfn - (phys_base >> PAGE_SHIFT);
		zones_size[ZONE_DMA] = npages;
		zholes_size[ZONE_DMA] = npages - pages_avail;

		free_area_init_node(0, NULL, NULL, zones_size,
				    phys_base, zholes_size);
	}

	device_scan();
}

/* Ok, it seems that the prom can allocate some more memory chunks
 * as a side effect of some prom calls we perform during the
 * boot sequence.  My most likely theory is that it is from the
 * prom_set_traptable() call, and OBP is allocating a scratchpad
 * for saving client program register state etc.
 */
void __init sort_memlist(struct linux_mlist_p1275 *thislist)
{
	int swapi = 0;
	int i, mitr;
	unsigned long tmpaddr, tmpsize;
	unsigned long lowest;

	for (i = 0; thislist[i].theres_more != 0; i++) {
		lowest = thislist[i].start_adr;
		for (mitr = i+1; thislist[mitr-1].theres_more != 0; mitr++)
			if (thislist[mitr].start_adr < lowest) {
				lowest = thislist[mitr].start_adr;
				swapi = mitr;
			}
		if (lowest == thislist[i].start_adr)
			continue;
		tmpaddr = thislist[swapi].start_adr;
		tmpsize = thislist[swapi].num_bytes;
		for (mitr = swapi; mitr > i; mitr--) {
			thislist[mitr].start_adr = thislist[mitr-1].start_adr;
			thislist[mitr].num_bytes = thislist[mitr-1].num_bytes;
		}
		thislist[i].start_adr = tmpaddr;
		thislist[i].num_bytes = tmpsize;
	}
}

void __init rescan_sp_banks(void)
{
	struct linux_prom64_registers memlist[64];
	struct linux_mlist_p1275 avail[64], *mlist;
	unsigned long bytes, base_paddr;
	int num_regs, node = prom_finddevice("/memory");
	int i;

	num_regs = prom_getproperty(node, "available",
				    (char *) memlist, sizeof(memlist));
	num_regs = (num_regs / sizeof(struct linux_prom64_registers));
	for (i = 0; i < num_regs; i++) {
		avail[i].start_adr = memlist[i].phys_addr;
		avail[i].num_bytes = memlist[i].reg_size;
		avail[i].theres_more = &avail[i + 1];
	}
	avail[i - 1].theres_more = NULL;
	sort_memlist(avail);

	mlist = &avail[0];
	i = 0;
	bytes = mlist->num_bytes;
	base_paddr = mlist->start_adr;
  
	sp_banks[0].base_addr = base_paddr;
	sp_banks[0].num_bytes = bytes;

	while (mlist->theres_more != NULL){
		i++;
		mlist = mlist->theres_more;
		bytes = mlist->num_bytes;
		if (i >= SPARC_PHYS_BANKS-1) {
			printk ("The machine has more banks than "
				"this kernel can support\n"
				"Increase the SPARC_PHYS_BANKS "
				"setting (currently %d)\n",
				SPARC_PHYS_BANKS);
			i = SPARC_PHYS_BANKS-1;
			break;
		}
    
		sp_banks[i].base_addr = mlist->start_adr;
		sp_banks[i].num_bytes = mlist->num_bytes;
	}

	i++;
	sp_banks[i].base_addr = 0xdeadbeefbeefdeadUL;
	sp_banks[i].num_bytes = 0;

	for (i = 0; sp_banks[i].num_bytes != 0; i++)
		sp_banks[i].num_bytes &= PAGE_MASK;
}

static void __init taint_real_pages(void)
{
	struct sparc_phys_banks saved_sp_banks[SPARC_PHYS_BANKS];
	int i;

	for (i = 0; i < SPARC_PHYS_BANKS; i++) {
		saved_sp_banks[i].base_addr =
			sp_banks[i].base_addr;
		saved_sp_banks[i].num_bytes =
			sp_banks[i].num_bytes;
	}

	rescan_sp_banks();

	/* Find changes discovered in the sp_bank rescan and
	 * reserve the lost portions in the bootmem maps.
	 */
	for (i = 0; saved_sp_banks[i].num_bytes; i++) {
		unsigned long old_start, old_end;

		old_start = saved_sp_banks[i].base_addr;
		old_end = old_start +
			saved_sp_banks[i].num_bytes;
		while (old_start < old_end) {
			int n;

			for (n = 0; sp_banks[n].num_bytes; n++) {
				unsigned long new_start, new_end;

				new_start = sp_banks[n].base_addr;
				new_end = new_start + sp_banks[n].num_bytes;

				if (new_start <= old_start &&
				    new_end >= (old_start + PAGE_SIZE)) {
					set_bit (old_start >> 22,
						 sparc64_valid_addr_bitmap);
					goto do_next_page;
				}
			}
			reserve_bootmem(old_start, PAGE_SIZE);

		do_next_page:
			old_start += PAGE_SIZE;
		}
	}
}

void __init mem_init(void)
{
	unsigned long codepages, datapages, initpages;
	unsigned long addr, last;
	int i;

	i = last_valid_pfn >> ((22 - PAGE_SHIFT) + 6);
	i += 1;
	sparc64_valid_addr_bitmap = (unsigned long *)
		__alloc_bootmem(i << 3, SMP_CACHE_BYTES, 0UL);
	if (sparc64_valid_addr_bitmap == NULL) {
		prom_printf("mem_init: Cannot alloc valid_addr_bitmap.\n");
		prom_halt();
	}
	memset(sparc64_valid_addr_bitmap, 0, i << 3);

	addr = PAGE_OFFSET + phys_base;
	last = PAGE_ALIGN((unsigned long)&_end) -
		((unsigned long) &empty_zero_page);
	last += PAGE_OFFSET + phys_base;
	while (addr < last) {
		set_bit(__pa(addr) >> 22, sparc64_valid_addr_bitmap);
		addr += PAGE_SIZE;
	}

	taint_real_pages();

	max_mapnr = last_valid_pfn - (phys_base >> PAGE_SHIFT);
	high_memory = __va(last_valid_pfn << PAGE_SHIFT);

	num_physpages = free_all_bootmem();
	codepages = (((unsigned long) &etext) - ((unsigned long)&_start));
	codepages = PAGE_ALIGN(codepages) >> PAGE_SHIFT;
	datapages = (((unsigned long) &edata) - ((unsigned long)&etext));
	datapages = PAGE_ALIGN(datapages) >> PAGE_SHIFT;
	initpages = (((unsigned long) &__init_end) - ((unsigned long) &__init_begin));
	initpages = PAGE_ALIGN(initpages) >> PAGE_SHIFT;

#ifndef CONFIG_SMP
	{
		/* Put empty_pg_dir on pgd_quicklist */
		extern pgd_t empty_pg_dir[1024];
		unsigned long addr = (unsigned long)empty_pg_dir;
		unsigned long alias_base = phys_base + PAGE_OFFSET -
			(long)(&empty_zero_page);
		
		memset(empty_pg_dir, 0, sizeof(empty_pg_dir));
		addr += alias_base;
		free_pgd_fast((pgd_t *)addr);
		num_physpages++;
	}
#endif

	printk("Memory: %uk available (%ldk kernel code, %ldk data, %ldk init) [%016lx,%016lx]\n",
	       nr_free_pages() << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10), 
	       PAGE_OFFSET, (last_valid_pfn << PAGE_SHIFT));
}

void free_initmem (void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		unsigned long page;
		struct page *p;

		page = (addr +
			((unsigned long) __va(phys_base)) -
			((unsigned long) &empty_zero_page));
		p = virt_to_page(page);

		ClearPageReserved(p);
		set_page_count(p, 1);
		__free_page(p);
		num_physpages++;
	}
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		struct page *p = virt_to_page(start);

		ClearPageReserved(p);
		set_page_count(p, 1);
		__free_page(p);
		num_physpages++;
	}
}
#endif

void si_meminfo(struct sysinfo *val)
{
	val->totalram = num_physpages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);

	/* These are always zero on Sparc64. */
	val->totalhigh = 0;
	val->freehigh = 0;

	val->mem_unit = PAGE_SIZE;
}
