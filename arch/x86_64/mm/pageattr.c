/* 
 * Copyright 2002 Andi Kleen, SuSE Labs. 
 * Thanks to Ben LaHaise for precious feedback.
 */ 

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

static inline pte_t *lookup_address(unsigned long address) 
{ 
	pgd_t *pgd = pgd_offset_k(address); 
	pmd_t *pmd;
	pte_t *pte;
	if (!pgd || !pgd_present(*pgd))
		return NULL; 
	pmd = pmd_offset(pgd, address); 	       
	if (!pmd_present(*pmd))
		return NULL; 
	if (pmd_large(*pmd))
		return (pte_t *)pmd;
	pte = pte_offset_kernel(pmd, address);
	if (pte && !pte_present(*pte))
		pte = NULL; 
	return pte;
} 

static struct page *split_large_page(unsigned long address, pgprot_t prot,
				     pgprot_t ref_prot)
{ 
	int i; 
	unsigned long addr;
	struct page *base = alloc_pages(GFP_KERNEL, 0);
	pte_t *pbase;
	if (!base) 
		return NULL;
	address = __pa(address);
	addr = address & LARGE_PAGE_MASK; 
	pbase = (pte_t *)page_address(base);
	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE) {
		pbase[i] = pfn_pte(addr >> PAGE_SHIFT, 
				   addr == address ? prot : ref_prot);
	}
	return base;
} 


static void flush_kernel_map(void *address) 
{
	if (0 && address && cpu_has_clflush) {
		/* is this worth it? */ 
		int i;
		for (i = 0; i < PAGE_SIZE; i += boot_cpu_data.x86_clflush_size) 
			asm volatile("clflush (%0)" :: "r" (address + i)); 
	} else
		asm volatile("wbinvd":::"memory"); 
	__flush_tlb_one(address);
}


static inline void flush_map(unsigned long address)
{	
	on_each_cpu(flush_kernel_map, (void *)address, 1, 1);
}

struct deferred_page { 
	struct deferred_page *next; 
	struct page *fpage;
	unsigned long address;
}; 
static struct deferred_page *df_list; /* protected by init_mm.mmap_sem */

static inline void save_page(unsigned long address, struct page *fpage)
{
	struct deferred_page *df;
	df = kmalloc(sizeof(struct deferred_page), GFP_KERNEL); 
	if (!df) {
		flush_map(address);
		__free_page(fpage);
	} else { 
		df->next = df_list;
		df->fpage = fpage;
		df->address = address;
		df_list = df;
	} 			
}

/* 
 * No more special protections in this 2/4MB area - revert to a
 * large page again. 
 */
static void revert_page(unsigned long address, pgprot_t ref_prot)
{
       pgd_t *pgd;
       pmd_t *pmd; 
       pte_t large_pte; 
       
       pgd = pgd_offset_k(address); 
       pmd = pmd_offset(pgd, address);
       BUG_ON(pmd_val(*pmd) & _PAGE_PSE); 
       pgprot_val(ref_prot) |= _PAGE_PSE;
       large_pte = mk_pte_phys(__pa(address) & LARGE_PAGE_MASK, ref_prot);
       set_pte((pte_t *)pmd, large_pte);
}      

static int
__change_page_attr(unsigned long address, struct page *page, pgprot_t prot, 
		   pgprot_t ref_prot)
{ 
	pte_t *kpte; 
	struct page *kpte_page;
	unsigned kpte_flags;

	kpte = lookup_address(address);
	if (!kpte) return 0;
	kpte_page = virt_to_page(((unsigned long)kpte) & PAGE_MASK);
	kpte_flags = pte_val(*kpte); 
	if (pgprot_val(prot) != pgprot_val(ref_prot)) { 
		if ((kpte_flags & _PAGE_PSE) == 0) { 
			pte_t old = *kpte;
			pte_t standard = mk_pte(page, ref_prot); 

			set_pte(kpte, mk_pte(page, prot)); 
			if (pte_same(old,standard))
				get_page(kpte_page);
		} else {
			struct page *split = split_large_page(address, prot, ref_prot); 
			if (!split)
				return -ENOMEM;
			get_page(kpte_page);
			set_pte(kpte,mk_pte(split, ref_prot));
		}	
	} else if ((kpte_flags & _PAGE_PSE) == 0) { 
		set_pte(kpte, mk_pte(page, ref_prot));
		__put_page(kpte_page);
	}

	if (page_count(kpte_page) == 1) {
		save_page(address, kpte_page); 		     
		revert_page(address, ref_prot);
	} 
	return 0;
} 

/*
 * Change the page attributes of an page in the linear mapping.
 *
 * This should be used when a page is mapped with a different caching policy
 * than write-back somewhere - some CPUs do not like it when mappings with
 * different caching policies exist. This changes the page attributes of the
 * in kernel linear mapping too.
 * 
 * The caller needs to ensure that there are no conflicting mappings elsewhere.
 * This function only deals with the kernel linear map.
 * 
 * Caller must call global_flush_tlb() after this.
 */
int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	int err = 0; 
	int i; 

	down_write(&init_mm.mmap_sem);
	for (i = 0; i < numpages; !err && i++, page++) { 
		unsigned long address = (unsigned long)page_address(page); 
		err = __change_page_attr(address, page, prot, PAGE_KERNEL); 
		if (err) 
			break; 
		/* Handle kernel mapping too which aliases part of the
		 * lowmem */
		/* Disabled right now. Fixme */ 
		if (0 && page_to_phys(page) < KERNEL_TEXT_SIZE) {		
			unsigned long addr2;
			addr2 = __START_KERNEL_map + page_to_phys(page);
			err = __change_page_attr(addr2, page, prot, 
						 PAGE_KERNEL_EXECUTABLE);
		} 
	} 	
	up_write(&init_mm.mmap_sem); 
	return err;
}

void global_flush_tlb(void)
{ 
	struct deferred_page *df, *next_df;

	down_read(&init_mm.mmap_sem);
	df = xchg(&df_list, NULL);
	up_read(&init_mm.mmap_sem);
	flush_map((df && !df->next) ? df->address : 0);
	for (; df; df = next_df) { 
		next_df = df->next;
		if (df->fpage) 
			__free_page(df->fpage);
		kfree(df);
	} 
} 

EXPORT_SYMBOL(change_page_attr);
EXPORT_SYMBOL(global_flush_tlb);
