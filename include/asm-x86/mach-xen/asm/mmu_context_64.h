#ifndef __X86_64_MMU_CONTEXT_H
#define __X86_64_MMU_CONTEXT_H

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN)
	if (read_pda(mmu_state) == TLBSTATE_OK)
		write_pda(mmu_state, TLBSTATE_LAZY);
#endif
}

#define prepare_arch_switch(next)	__prepare_arch_switch()

static inline void __prepare_arch_switch(void)
{
	/*
	 * Save away %es, %ds, %fs and %gs. Must happen before reload
	 * of cr3/ldt (i.e., not in __switch_to).
	 */
	__asm__ __volatile__ (
		"mov %%es,%0 ; mov %%ds,%1 ; mov %%fs,%2 ; mov %%gs,%3"
		: "=m" (current->thread.es),
		  "=m" (current->thread.ds),
		  "=m" (current->thread.fsindex),
		  "=m" (current->thread.gsindex) );

	if (current->thread.ds)
		__asm__ __volatile__ ( "movl %0,%%ds" : : "r" (0) );

	if (current->thread.es)
		__asm__ __volatile__ ( "movl %0,%%es" : : "r" (0) );

	if (current->thread.fsindex) {
		__asm__ __volatile__ ( "movl %0,%%fs" : : "r" (0) );
		current->thread.fs = 0;
	}

	if (current->thread.gsindex) {
		load_gs_index(0);
		current->thread.gs = 0;
	}
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();
	struct mmuext_op _op[3], *op = _op;
	pgd_t *upgd;

	if (likely(prev != next)) {
		BUG_ON(!xen_feature(XENFEAT_writable_page_tables) &&
		       !PagePinned(virt_to_page(next->pgd)));

		/* stop flush ipis for the previous mm */
		cpu_clear(cpu, prev->cpu_vm_mask);
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN)
		write_pda(mmu_state, TLBSTATE_OK);
		write_pda(active_mm, next);
#endif
		cpu_set(cpu, next->cpu_vm_mask);

		/* load_cr3(next->pgd) */
		op->cmd = MMUEXT_NEW_BASEPTR;
		op->arg1.mfn = pfn_to_mfn(__pa(next->pgd) >> PAGE_SHIFT);
		op++;

		/* xen_new_user_pt(next->pgd) */
		op->cmd = MMUEXT_NEW_USER_BASEPTR;
		upgd = __user_pgd(next->pgd);
		op->arg1.mfn = likely(upgd)
			       ? pfn_to_mfn(__pa(upgd) >> PAGE_SHIFT) : 0;
		op++;
		
		if (unlikely(next->context.ldt != prev->context.ldt)) {
			/* load_LDT_nolock(&next->context) */
			op->cmd = MMUEXT_SET_LDT;
			op->arg1.linear_addr = (unsigned long)next->context.ldt;
			op->arg2.nr_ents     = next->context.size;
			op++;
		}

		BUG_ON(HYPERVISOR_mmuext_op(_op, op-_op, NULL, DOMID_SELF));
	}
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN)
	else {
		write_pda(mmu_state, TLBSTATE_OK);
		if (read_pda(active_mm) != next)
			BUG();
		if (!cpu_test_and_set(cpu, next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled
			 * tlb flush IPI delivery. We must reload CR3
			 * to make sure to use no freed page tables.
			 */
                        load_cr3(next->pgd);
			xen_new_user_pt(next->pgd);
			load_LDT_nolock(&next->context);
		}
	}
#endif
}

#define deactivate_mm(tsk, mm)			\
do {						\
	load_gs_index(0);			\
	asm volatile("movl %0,%%fs"::"r"(0));	\
} while (0)

#endif
