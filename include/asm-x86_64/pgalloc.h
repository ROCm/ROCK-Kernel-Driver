#ifndef _X86_64_PGALLOC_H
#define _X86_64_PGALLOC_H

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <asm/pda.h>
#include <linux/threads.h>
#include <linux/mm.h>

#define pmd_populate_kernel(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE | __pa(pte)))
#define pgd_populate(mm, pgd, pmd) \
		set_pgd(pgd, __pgd(_PAGE_TABLE | __pa(pmd)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE | 
			   ((u64)(pte - mem_map) << PAGE_SHIFT))); 
}

extern __inline__ pmd_t *get_pmd(void)
{
	return (pmd_t *)get_zeroed_page(GFP_KERNEL);
}

extern __inline__ void pmd_free(pmd_t *pmd)
{
	if ((unsigned long)pmd & (PAGE_SIZE-1)) 
		BUG(); 
	free_page((unsigned long)pmd);
}

static inline pmd_t *pmd_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	return (pmd_t *) get_zeroed_page(GFP_KERNEL); 
}

static inline pgd_t *pgd_alloc (struct mm_struct *mm)
{
	return (pgd_t *)get_zeroed_page(GFP_KERNEL);
}

static inline void pgd_free (pgd_t *pgd)
{
	if ((unsigned long)pgd & (PAGE_SIZE-1)) 
		BUG(); 
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return (pte_t *) get_zeroed_page(GFP_KERNEL);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	void *p = (void *)get_zeroed_page(GFP_KERNEL); 
	if (!p)
		return NULL;
	return virt_to_page(p);
}

/* Should really implement gc for free page table pages. This could be
   done with a reference count in struct page. */

extern __inline__ void pte_free_kernel(pte_t *pte)
{
	if ((unsigned long)pte & (PAGE_SIZE-1))
		BUG();
	free_page((unsigned long)pte); 
}

extern inline void pte_free(struct page *pte)
{
	__free_page(pte);
} 


/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 */

#ifndef CONFIG_SMP

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() __flush_tlb_all()
#define local_flush_tlb() __flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb();
}

#else

#include <asm/smp.h>

#define local_flush_tlb() \
	__flush_tlb()

extern void flush_tlb_all(void);
extern void flush_tlb_current_task(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);

#define flush_tlb()	flush_tlb_current_task()

static inline void flush_tlb_range(struct vm_area_struct * vma, unsigned long start, unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

#define TLBSTATE_OK	1
#define TLBSTATE_LAZY	2

struct tlb_state
{
	struct mm_struct *active_mm;
	int state;
	char __cacheline_padding[24];
};
extern struct tlb_state cpu_tlbstate[NR_CPUS];


#endif

extern inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
	/* i386 does not keep any page table caches in TLB */
}

#endif /* _X86_64_PGALLOC_H */
