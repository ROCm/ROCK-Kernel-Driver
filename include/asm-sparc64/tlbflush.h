#ifndef _SPARC64_TLBFLUSH_H
#define _SPARC64_TLBFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>

/* TLB flush operations. */

extern void __flush_tlb_all(void);
extern void __flush_tlb_mm(unsigned long context, unsigned long r);
extern void __flush_tlb_range(unsigned long context, unsigned long start,
			      unsigned long r, unsigned long end,
			      unsigned long pgsz, unsigned long size);
extern void __flush_tlb_page(unsigned long context, unsigned long page, unsigned long r);

extern void __flush_tlb_kernel_range(unsigned long start, unsigned long end);

#ifndef CONFIG_SMP

#define flush_tlb_all()		__flush_tlb_all()
#define flush_tlb_kernel_range(start,end) \
	__flush_tlb_kernel_range(start,end)

#define flush_tlb_mm(__mm) \
do { if (CTX_VALID((__mm)->context)) \
	__flush_tlb_mm(CTX_HWBITS((__mm)->context), SECONDARY_CONTEXT); \
} while (0)

#define flush_tlb_range(__vma, start, end) \
do { if (CTX_VALID((__vma)->vm_mm->context)) { \
	unsigned long __start = (start)&PAGE_MASK; \
	unsigned long __end = PAGE_ALIGN(end); \
	__flush_tlb_range(CTX_HWBITS((__vma)->vm_mm->context), __start, \
			  SECONDARY_CONTEXT, __end, PAGE_SIZE, \
			  (__end - __start)); \
     } \
} while (0)

#define flush_tlb_vpte_range(__mm, start, end) \
do { if (CTX_VALID((__mm)->context)) { \
	unsigned long __start = (start)&PAGE_MASK; \
	unsigned long __end = PAGE_ALIGN(end); \
	__flush_tlb_range(CTX_HWBITS((__mm)->context), __start, \
			  SECONDARY_CONTEXT, __end, PAGE_SIZE, \
			  (__end - __start)); \
     } \
} while (0)

#define flush_tlb_page(vma, page) \
do { struct mm_struct *__mm = (vma)->vm_mm; \
     if (CTX_VALID(__mm->context)) \
	__flush_tlb_page(CTX_HWBITS(__mm->context), (page)&PAGE_MASK, \
			 SECONDARY_CONTEXT); \
} while (0)

#define flush_tlb_vpte_page(mm, addr) \
do { struct mm_struct *__mm = (mm); \
     if (CTX_VALID(__mm->context)) \
	__flush_tlb_page(CTX_HWBITS(__mm->context), (addr)&PAGE_MASK, \
			 SECONDARY_CONTEXT); \
} while (0)

#else /* CONFIG_SMP */

extern void smp_flush_tlb_all(void);
extern void smp_flush_tlb_mm(struct mm_struct *mm);
extern void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				unsigned long end);
extern void smp_flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void smp_flush_tlb_page(struct mm_struct *mm, unsigned long page);

#define flush_tlb_all()		smp_flush_tlb_all()
#define flush_tlb_mm(mm)	smp_flush_tlb_mm(mm)
#define flush_tlb_range(vma, start, end) \
	smp_flush_tlb_range((vma)->vm_mm, start, end)
#define flush_tlb_vpte_range(mm, start, end) \
	smp_flush_tlb_range(mm, start, end)
#define flush_tlb_kernel_range(start, end) \
	smp_flush_tlb_kernel_range(start, end)
#define flush_tlb_page(vma, page) \
	smp_flush_tlb_page((vma)->vm_mm, page)
#define flush_tlb_vpte_page(mm, page) \
	smp_flush_tlb_page((mm), page)

#endif /* ! CONFIG_SMP */

static __inline__ void flush_tlb_pgtables(struct mm_struct *mm, unsigned long start,
					  unsigned long end)
{
	/* Note the signed type.  */
	long s = start, e = end, vpte_base;
	if (s > e)
		/* Nobody should call us with start below VM hole and end above.
		   See if it is really true.  */
		BUG();
#if 0
	/* Currently free_pgtables guarantees this.  */
	s &= PMD_MASK;
	e = (e + PMD_SIZE - 1) & PMD_MASK;
#endif
	vpte_base = (tlb_type == spitfire ?
		     VPTE_BASE_SPITFIRE :
		     VPTE_BASE_CHEETAH);

	flush_tlb_vpte_range(mm,
			     vpte_base + (s >> (PAGE_SHIFT - 3)),
			     vpte_base + (e >> (PAGE_SHIFT - 3)));
}

#endif /* _SPARC64_TLBFLUSH_H */
