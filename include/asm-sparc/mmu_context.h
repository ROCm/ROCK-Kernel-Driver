#ifndef __SPARC_MMU_CONTEXT_H
#define __SPARC_MMU_CONTEXT_H

#include <asm/btfixup.h>

#ifndef __ASSEMBLY__

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 168-bit bitmap where the first 128 bits are
 * unlikely to be clear. It's guaranteed that at least one of the 168
 * bits is cleared.
 */
#if MAX_RT_PRIO != 128 || MAX_PRIO != 168
# error update this function.
#endif

static inline int sched_find_first_zero_bit(unsigned long *b)
{
	unsigned int rt;

	rt = b[0] & b[1] & b[2] & b[3];
	if (unlikely(rt != 0xffffffff))
		return find_first_zero_bit(b, MAX_RT_PRIO);

	if (b[4] != ~0)
		return ffz(b[4]) + MAX_RT_PRIO;
	return ffz(b[5]) + 32 + MAX_RT_PRIO;
}

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}

/*
 * Initialize a new mmu context.  This is invoked when a new
 * address space instance (unique or shared) is instantiated.
 */
#define init_new_context(tsk, mm) (((mm)->context = NO_CONTEXT), 0)

/*
 * Destroy a dead context.  This occurs when mmput drops the
 * mm_users count to zero, the mmaps have been released, and
 * all the page tables have been flushed.  Our job is to destroy
 * any remaining processor-specific state.
 */
BTFIXUPDEF_CALL(void, destroy_context, struct mm_struct *)

#define destroy_context(mm) BTFIXUP_CALL(destroy_context)(mm)

/* Switch the current MM context. */
BTFIXUPDEF_CALL(void, switch_mm, struct mm_struct *, struct mm_struct *, struct task_struct *, int)

#define switch_mm(old_mm, mm, tsk, cpu) BTFIXUP_CALL(switch_mm)(old_mm, mm, tsk, cpu)

/* Activate a new MM instance for the current task. */
#define activate_mm(active_mm, mm) switch_mm((active_mm), (mm), NULL, smp_processor_id())

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC_MMU_CONTEXT_H) */
