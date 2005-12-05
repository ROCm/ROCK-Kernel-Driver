#ifndef __X86_64_MMU_CONTEXT_H
#define __X86_64_MMU_CONTEXT_H

#include <linux/config.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/semaphore.h>

/*
 * possibly do the LDT unload here?
 * Used for LDT initialization/destruction. You cannot copy an LDT with
 * init_new_context, since it thinks you are passing it a new LDT and won't
 * deallocate its old content.
 */

int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);

/* LDT initialization for a clean environment - needed for SKAS.*/
static inline void init_new_empty_context(struct mm_struct *mm)
{
	init_MUTEX(&mm->context.sem);
	mm->context.size = 0;
}

/* LDT copy for SKAS - for the above problem.*/
int copy_context(struct mm_struct *mm, struct mm_struct *old_mm);

#ifdef CONFIG_SMP

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
	if (read_pda(mmu_state) == TLBSTATE_OK) 
		write_pda(mmu_state, TLBSTATE_LAZY);
}
#else
static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}
#endif

static inline void load_cr3(pgd_t *pgd)
{
	asm volatile("movq %0,%%cr3" :: "r" (__pa(pgd)) : "memory");
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();
#ifdef CONFIG_SMP
	prev = read_pda(active_mm);
#endif
	if (likely(prev != next)) {
		/* stop flush ipis for the previous mm */
		clear_bit(cpu, &prev->cpu_vm_mask);
#ifdef CONFIG_SMP
		write_pda(mmu_state, TLBSTATE_OK);
		write_pda(active_mm, next);
#endif
		set_bit(cpu, &next->cpu_vm_mask);
		load_cr3(next->pgd);

		if (unlikely(next->context.ldt != prev->context.ldt)) 
			load_LDT_nolock(&next->context, cpu);
	}
#ifdef CONFIG_SMP
	else {
		write_pda(mmu_state, TLBSTATE_OK);
		if(!test_and_set_bit(cpu, &next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled 
			 * tlb flush IPI delivery. We must reload CR3
			 * to make sure to use no freed page tables.
			 */
			load_cr3(next->pgd);
			load_LDT_nolock(&next->context, cpu);
		}
	}
#endif
}

#define deactivate_mm(tsk,mm)	do { \
	load_gs_index(0); \
	asm volatile("movl %0,%%fs"::"r"(0));  \
} while(0)

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL)


#endif
