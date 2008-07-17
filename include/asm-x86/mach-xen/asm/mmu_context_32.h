#ifndef __I386_SCHED_H
#define __I386_SCHED_H

#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

void arch_exit_mmap(struct mm_struct *mm);
void arch_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm);

void mm_pin(struct mm_struct *mm);
void mm_unpin(struct mm_struct *mm);
void mm_pin_all(void);

static inline void xen_activate_mm(struct mm_struct *prev,
				   struct mm_struct *next)
{
	if (!PagePinned(virt_to_page(next->pgd)))
		mm_pin(next);
}

/*
 * Used for LDT copy/destruction.
 */
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);


static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
#if 0 /* XEN: no lazy tlb */
	unsigned cpu = smp_processor_id();
	if (per_cpu(cpu_tlbstate, cpu).state == TLBSTATE_OK)
		per_cpu(cpu_tlbstate, cpu).state = TLBSTATE_LAZY;
#endif
}

#define prepare_arch_switch(next)	__prepare_arch_switch()

static inline void __prepare_arch_switch(void)
{
	/*
	 * Save away %gs. No need to save %fs, as it was saved on the
	 * stack on entry.  No need to save %es and %ds, as those are
	 * always kernel segments while inside the kernel.
	 */
	asm volatile ( "mov %%gs,%0"
		: "=m" (current->thread.gs));
	asm volatile ( "movl %0,%%gs"
		: : "r" (0) );
}

static inline void switch_mm(struct mm_struct *prev,
			     struct mm_struct *next,
			     struct task_struct *tsk)
{
	int cpu = smp_processor_id();
	struct mmuext_op _op[2], *op = _op;

	if (likely(prev != next)) {
		BUG_ON(!xen_feature(XENFEAT_writable_page_tables) &&
		       !PagePinned(virt_to_page(next->pgd)));

		/* stop flush ipis for the previous mm */
		cpu_clear(cpu, prev->cpu_vm_mask);
#if 0 /* XEN: no lazy tlb */
		per_cpu(cpu_tlbstate, cpu).state = TLBSTATE_OK;
		per_cpu(cpu_tlbstate, cpu).active_mm = next;
#endif
		cpu_set(cpu, next->cpu_vm_mask);

		/* Re-load page tables: load_cr3(next->pgd) */
		op->cmd = MMUEXT_NEW_BASEPTR;
		op->arg1.mfn = pfn_to_mfn(__pa(next->pgd) >> PAGE_SHIFT);
		op++;

		/*
		 * load the LDT, if the LDT is different:
		 */
		if (unlikely(prev->context.ldt != next->context.ldt)) {
			/* load_LDT_nolock(&next->context, cpu) */
			op->cmd = MMUEXT_SET_LDT;
			op->arg1.linear_addr = (unsigned long)next->context.ldt;
			op->arg2.nr_ents     = next->context.size;
			op++;
		}

		BUG_ON(HYPERVISOR_mmuext_op(_op, op-_op, NULL, DOMID_SELF));
	}
#if 0 /* XEN: no lazy tlb */
	else {
		per_cpu(cpu_tlbstate, cpu).state = TLBSTATE_OK;
		BUG_ON(per_cpu(cpu_tlbstate, cpu).active_mm != next);

		if (!cpu_test_and_set(cpu, next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled
			 * tlb flush IPI delivery. We must reload %cr3.
			 */
			load_cr3(next->pgd);
			load_LDT_nolock(&next->context);
		}
	}
#endif
}

#define deactivate_mm(tsk, mm)			\
	asm("movl %0,%%gs": :"r" (0));

#define activate_mm(prev, next)			\
do {						\
	xen_activate_mm(prev, next);		\
	switch_mm((prev), (next), NULL);	\
} while (0)

#endif
