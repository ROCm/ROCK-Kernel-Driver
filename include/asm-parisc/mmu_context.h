#ifndef __PARISC_MMU_CONTEXT_H
#define __PARISC_MMU_CONTEXT_H

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}

/* on PA-RISC, we actually have enough contexts to justify an allocator
 * for them.  prumpf */

extern unsigned long alloc_sid(void);
extern void free_sid(unsigned long);

static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	/*
	 * Init_new_context can be called for a cloned mm, so we
	 * only allocate a space id if one hasn't been allocated
	 * yet AND mm != &init_mm (cloned kernel thread which
	 * will run in the kernel space with spaceid 0).
	 */

	if ((mm != &init_mm) && (mm->context == 0)) {
		mm->context = alloc_sid();
	}

	return 0;
}

static inline void
destroy_context(struct mm_struct *mm)
{
	free_sid(mm->context);
	mm->context = 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk, unsigned cpu)
{

	if (prev != next) {
		/* Re-load page tables */
		tsk->thread.pg_tables = __pa(next->pgd);

		mtctl(tsk->thread.pg_tables, 25);
		mtsp(next->context,3);
	}
}

static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	/*
	 * Activate_mm is our one chance to allocate a space id
	 * for a new mm created in the exec path. There's also
	 * some lazy tlb stuff, which is currently dead code, but
	 * we only allocate a space id if one hasn't been allocated
	 * already, so we should be OK.
	 */

	if (next == &init_mm) BUG(); /* Should never happen */

	if (next->context == 0)
	    next->context = alloc_sid();

	switch_mm(prev,next,current,0);
}
#endif
