/*
 *  include/asm-s390/mmu_context.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/mmu_context.h"
 */

#ifndef __S390_MMU_CONTEXT_H
#define __S390_MMU_CONTEXT_H

/*
 * get a new mmu context.. S390 don't know about contexts.
 */
#define init_new_context(tsk,mm)        0

#define destroy_context(mm)             flush_tlb_mm(mm)

static inline void enter_lazy_tlb(struct mm_struct *mm,
                                  struct task_struct *tsk)
{
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
                             struct task_struct *tsk)
{
        unsigned long pgd;

        if (prev != next) {
#ifndef __s390x__
	        pgd = (__pa(next->pgd)&PAGE_MASK) | 
                      (_SEGMENT_TABLE|USER_STD_MASK);
                /* Load page tables */
                asm volatile("    lctl  7,7,%0\n"   /* secondary space */
                             "    lctl  13,13,%0\n" /* home space */
                             : : "m" (pgd) );
#else /* __s390x__ */
                pgd = (__pa(next->pgd)&PAGE_MASK) | (_REGION_TABLE|USER_STD_MASK);
                /* Load page tables */
                asm volatile("    lctlg 7,7,%0\n"   /* secondary space */
                             "    lctlg 13,13,%0\n" /* home space */
                             : : "m" (pgd) );
#endif /* __s390x__ */
        }
	cpu_set(smp_processor_id(), next->cpu_vm_mask);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

extern inline void activate_mm(struct mm_struct *prev,
                               struct mm_struct *next)
{
        switch_mm(prev, next, current);
}

#endif
