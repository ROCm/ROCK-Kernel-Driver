#ifndef _ASM_X86_MMU_CONTEXT_32_H
#define _ASM_X86_MMU_CONTEXT_32_H

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN) /* XEN: no lazy tlb */
	if (x86_read_percpu(cpu_tlbstate.state) == TLBSTATE_OK)
		x86_write_percpu(cpu_tlbstate.state, TLBSTATE_LAZY);
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
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN) /* XEN: no lazy tlb */
		x86_write_percpu(cpu_tlbstate.state, TLBSTATE_OK);
		x86_write_percpu(cpu_tlbstate.active_mm, next);
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
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN) /* XEN: no lazy tlb */
	else {
		x86_write_percpu(cpu_tlbstate.state, TLBSTATE_OK);
		BUG_ON(x86_read_percpu(cpu_tlbstate.active_mm) != next);

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

#endif /* _ASM_X86_MMU_CONTEXT_32_H */
