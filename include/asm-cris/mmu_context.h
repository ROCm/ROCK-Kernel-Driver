#ifndef __CRIS_MMU_CONTEXT_H
#define __CRIS_MMU_CONTEXT_H

extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void get_mmu_context(struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);
extern void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		      struct task_struct *tsk, int cpu);

#define activate_mm(prev,next) switch_mm((prev),(next),NULL,smp_processor_id())

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}

#endif
