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

static inline pte_t *lookup_address(unsigned long address) 
{ 
	pgd_t *pgd = pgd_offset_k(address); 
	pmd_t *pmd;
	if (pgd_none(*pgd))
		return NULL;
	pmd = pmd_offset(pgd, address); 	       
	if (pmd_none(*pmd))
		return NULL;
	if (pmd_large(*pmd))
		return (pte_t *)pmd;
        return pte_offset_kernel(pmd, address);
} 

static struct page *split_large_page(unsigned long address, pgprot_t prot)
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
				   addr == address ? prot : PAGE_KERNEL);
	}
	return base;
} 

static void flush_kernel_map(void *dummy) 
{ 
	/* Could use CLFLUSH here if the CPU supports it (Hammer,P4) */
	if (boot_cpu_data.x86_model >= 4) 
		asm volatile("wbinvd":::"memory"); 
	/* Flush all to work around Errata in early athlons regarding 
	 * large page flushing. 
	 */
	__flush_tlb_all(); 	
}

static void set_pmd_pte(pte_t *kpte, unsigned long address, pte_t pte) 
{ 
	set_pte_atomic(kpte, pte); 	/* change init_mm */
#ifndef CONFIG_X86_PAE
	{
		struct list_head *l;
		spin_lock(&mmlist_lock);
		list_for_each(l, &init_mm.mmlist) { 
			struct mm_struct *mm = list_entry(l, struct mm_struct, mmlist);
			pmd_t *pmd = pmd_offset(pgd_offset(mm, address), address);
			set_pte_atomic((pte_t *)pmd, pte);
		} 
		spin_unlock(&mmlist_lock);
	}
#endif
}

/* 
 * No more special protections in this 2/4MB area - revert to a
 * large page again. 
 */
static inline void revert_page(struct page *kpte_page, unsigned long address)
{
	pte_t *linear = (pte_t *) 
		pmd_offset(pgd_offset(&init_mm, address), address);
	set_pmd_pte(linear,  address,
		    pfn_pte((__pa(address) & LARGE_PAGE_MASK) >> PAGE_SHIFT,
			    PAGE_KERNEL_LARGE));
}

static int
__change_page_attr(struct page *page, pgprot_t prot, struct page **oldpage) 
{ 
	pte_t *kpte; 
	unsigned long address;
	struct page *kpte_page;

#ifdef CONFIG_HIGHMEM
	if (page >= highmem_start_page) 
		BUG(); 
#endif
	address = (unsigned long)page_address(page);

	kpte = lookup_address(address);
	if (!kpte)
		return -EINVAL;
	kpte_page = virt_to_page(((unsigned long)kpte) & PAGE_MASK);
	if (pgprot_val(prot) != pgprot_val(PAGE_KERNEL)) { 
		if ((pte_val(*kpte) & _PAGE_PSE) == 0) { 
			pte_t old = *kpte;
			pte_t standard = mk_pte(page, PAGE_KERNEL); 
			set_pte_atomic(kpte, mk_pte(page, prot)); 
			if (pte_same(old,standard))
				atomic_inc(&kpte_page->count);
		} else {
			struct page *split = split_large_page(address, prot); 
			if (!split)
				return -ENOMEM;
			atomic_inc(&kpte_page->count);
			set_pmd_pte(kpte,address,mk_pte(split, PAGE_KERNEL));
		}	
	} else if ((pte_val(*kpte) & _PAGE_PSE) == 0) { 
		set_pte_atomic(kpte, mk_pte(page, PAGE_KERNEL));
		atomic_dec(&kpte_page->count); 
	}

	if (cpu_has_pse && (atomic_read(&kpte_page->count) == 1)) { 
		*oldpage = kpte_page;
		revert_page(kpte_page, address);
	} 
	return 0;
} 

static inline void flush_map(void)
{	
#ifdef CONFIG_SMP 
	smp_call_function(flush_kernel_map, NULL, 1, 1);
#endif	
	flush_kernel_map(NULL);
}

struct deferred_page { 
	struct deferred_page *next; 
	struct page *fpage;
}; 
static struct deferred_page *df_list; /* protected by init_mm.mmap_sem */

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
	struct page *fpage; 
	int i; 

	down_write(&init_mm.mmap_sem);
	for (i = 0; i < numpages; i++, page++) { 
		fpage = NULL;
		err = __change_page_attr(page, prot, &fpage); 
		if (err) 
			break; 
		if (fpage) { 
			struct deferred_page *df;
			df = kmalloc(sizeof(struct deferred_page), GFP_KERNEL); 
			if (!df) {
				flush_map();
				__free_page(fpage);
			} else { 
				df->next = df_list;
				df->fpage = fpage;				
				df_list = df;
			} 			
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
	flush_map();
	for (; df; df = next_df) { 
		next_df = df->next;
		if (df->fpage) 
			__free_page(df->fpage);
		kfree(df);
	} 
} 

EXPORT_SYMBOL(change_page_attr);
EXPORT_SYMBOL(global_flush_tlb);
