#ifndef __X86_64_MMU_CONTEXT_H
#define __X86_64_MMU_CONTEXT_H

#include <linux/config.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 168-bit bitmap where the first 128 bits are
 * unlikely to be set. It's guaranteed that at least one of the 168
 * bits is cleared.
 */
#if MAX_RT_PRIO != 128 || MAX_PRIO != 168
# error update this function.
#endif

static inline int __sched_find_first_bit(unsigned long *b)
{
	if (b[0])
		return __ffs(b[0]);
	if (b[1])
		return __ffs(b[1]) + 64;
	if (b[2])
		return __ffs(b[2]) + 128;
}

static inline int sched_find_first_bit(unsigned long *b)
{ 
	int n = __sched_find_first_bit(b);
	BUG_ON((unsigned)n > 167);
	return n; 
} 

/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)		do { } while(0)
#define init_new_context(tsk,mm)	0

#ifdef CONFIG_SMP

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
	if(cpu_tlbstate[cpu].state == TLBSTATE_OK)
		cpu_tlbstate[cpu].state = TLBSTATE_LAZY;	
}
#else
static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}
#endif

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk, unsigned cpu)
{
	if (likely(prev != next)) {
		/* stop flush ipis for the previous mm */
		clear_bit(cpu, &prev->cpu_vm_mask);
		/*
		 * Re-load LDT if necessary
		 */
		if (unlikely(prev->context.segments != next->context.segments))
			load_LDT(next);
#ifdef CONFIG_SMP
		cpu_tlbstate[cpu].state = TLBSTATE_OK;
		cpu_tlbstate[cpu].active_mm = next;
#endif
		set_bit(cpu, &next->cpu_vm_mask);
		set_bit(cpu, &next->context.cpuvalid);
		/* Re-load page tables */
		asm volatile("movq %0,level4_pgt": :"r" (__pa(next->pgd) | 7));
		__flush_tlb();
	}
#ifdef CONFIG_SMP
	else {
		cpu_tlbstate[cpu].state = TLBSTATE_OK;
		if(cpu_tlbstate[cpu].active_mm != next)
			BUG();
		if(!test_and_set_bit(cpu, &next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled 
			 * tlb flush IPI delivery. We must flush our tlb.
			 */
			local_flush_tlb();
		}
		if (!test_and_set_bit(cpu, &next->context.cpuvalid))
			load_LDT(next);
	}
#endif
}

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL,smp_processor_id())

#endif
