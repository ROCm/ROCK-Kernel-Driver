#ifndef __UM_MMU_CONTEXT_H
#define __UM_MMU_CONTEXT_H

#include "linux/sched.h"

#define init_new_context(task, mm) (0)
#define get_mmu_context(task) do ; while(0)
#define activate_context(tsk) do ; while(0)
#define destroy_context(mm) do ; while(0)

static inline void activate_mm(struct mm_struct *old, struct mm_struct *new)
{
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk, unsigned cpu)
{
}

static inline void enter_lazy_tlb(struct mm_struct *mm, 
				  struct task_struct *tsk, unsigned cpu)
{
}

#endif
