#ifndef _ASM_X86_MMU_CONTEXT_H
#define _ASM_X86_MMU_CONTEXT_H

#include <asm/desc.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>

#include <trace/events/tlb.h>

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/mpx.h>

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

#if defined(CONFIG_PERF_EVENTS) && !defined(CONFIG_XEN)
extern struct static_key rdpmc_always_available;

static inline void load_mm_cr4(struct mm_struct *mm)
{
	if (static_key_false(&rdpmc_always_available) ||
	    atomic_read(&mm->context.perf_rdpmc_allowed))
		cr4_set_bits(X86_CR4_PCE);
	else
		cr4_clear_bits(X86_CR4_PCE);
}
#else
static inline void load_mm_cr4(struct mm_struct *mm) {}
#endif

/*
 * ldt_structs can be allocated, used, and freed, but they are never
 * modified while live.
 */
struct ldt_struct {
	/*
	 * Xen requires page-aligned LDTs with special permissions.  This is
	 * needed to prevent us from installing evil descriptors such as
	 * call gates.  On native, we could merge the ldt_struct and LDT
	 * allocations, but it's not worth trying to optimize.
	 */
	struct desc_struct *entries;
	int size;
};

static inline void load_mm_ldt(struct mm_struct *mm)
{
	struct ldt_struct *ldt;

	/* lockless_dereference synchronizes with smp_store_release */
	ldt = lockless_dereference(mm->context.ldt);

	/*
	 * Any change to mm->context.ldt is followed by an IPI to all
	 * CPUs with the mm active.  The LDT will not be freed until
	 * after the IPI is handled by all such CPUs.  This means that,
	 * if the ldt_struct changes before we return, the values we see
	 * will be safe, and the new values will be loaded before we run
	 * any user code.
	 *
	 * NB: don't try to convert this to use RCU without extreme care.
	 * We would still need IRQs off, because we don't want to change
	 * the local LDT after an IPI loaded a newer value than the one
	 * that we can see.
	 */

	if (unlikely(ldt))
		set_ldt(ldt->entries, ldt->size);
	else
		clear_LDT();

	DEBUG_LOCKS_WARN_ON(preemptible());
}

/*
 * Used for LDT copy/destruction.
 */
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);


static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN) /* XEN: no lazy tlb */
	if (this_cpu_read(cpu_tlbstate.state) == TLBSTATE_OK)
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_LAZY);
#endif
}

#define prepare_arch_switch(next)	__prepare_arch_switch()

static inline void __prepare_arch_switch(void)
{
#ifdef CONFIG_X86_32
	/*
	 * Save away %gs. No need to save %fs, as it was saved on the
	 * stack on entry.  No need to save %es and %ds, as those are
	 * always kernel segments while inside the kernel.
	 */
	lazy_save_gs(current->thread.gs);
	lazy_load_gs(__KERNEL_STACK_CANARY);
#else
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
#endif
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();
	struct mmuext_op _op[2 + (sizeof(long) > 4)], *op = _op;
#ifdef CONFIG_X86_64
	pgd_t *upgd;
#endif

	if (likely(prev != next)) {
		BUG_ON(!xen_feature(XENFEAT_writable_page_tables) &&
		       !PagePinned(virt_to_page(next->pgd)));

#if defined(CONFIG_SMP) && !defined(CONFIG_XEN) /* XEN: no lazy tlb */
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_OK);
		this_cpu_write(cpu_tlbstate.active_mm, next);
#endif
		cpumask_set_cpu(cpu, mm_cpumask(next));

		/* Re-load page tables: load_cr3(next->pgd) */
		op->cmd = MMUEXT_NEW_BASEPTR;
		op->arg1.mfn = virt_to_mfn(next->pgd);
		op++;
		trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);

		/* xen_new_user_pt(next->pgd) */
#ifdef CONFIG_X86_64
		op->cmd = MMUEXT_NEW_USER_BASEPTR;
		upgd = __user_pgd(next->pgd);
		op->arg1.mfn = likely(upgd) ? virt_to_mfn(upgd) : 0;
		op++;
#endif

		/* Load per-mm CR4 state */
		load_mm_cr4(next);

		/*
		 * Load the LDT, if the LDT is different.
		 *
		 * It's possible that prev->context.ldt doesn't match
		 * the LDT register.  This can happen if leave_mm(prev)
		 * was called and then modify_ldt changed
		 * prev->context.ldt but suppressed an IPI to this CPU.
		 * In this case, prev->context.ldt != NULL, because we
		 * never set context.ldt to NULL while the mm still
		 * exists.  That means that next->context.ldt !=
		 * prev->context.ldt, because mms never share an LDT.
		 */
		if (unlikely(prev->context.ldt != next->context.ldt)) {
			/* load_mm_ldt(next) */
			const struct ldt_struct *ldt;

			/* lockless_dereference synchronizes with smp_store_release */
			ldt = lockless_dereference(next->context.ldt);
			op->cmd = MMUEXT_SET_LDT;
			if (unlikely(ldt)) {
				op->arg1.linear_addr = (long)ldt->entries;
				op->arg2.nr_ents     = ldt->size;
			} else {
				op->arg1.linear_addr = 0;
				op->arg2.nr_ents     = 0;
			}
			op++;
		}

		BUG_ON(HYPERVISOR_mmuext_op(_op, op-_op, NULL, DOMID_SELF));

		/* Stop TLB flushes for the previous mm */
		cpumask_clear_cpu(cpu, mm_cpumask(prev));
	}
#if defined(CONFIG_SMP) && !defined(CONFIG_XEN) /* XEN: no lazy tlb */
	else {
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_OK);
		BUG_ON(this_cpu_read(cpu_tlbstate.active_mm) != next);

		if (!cpumask_test_cpu(cpu, mm_cpumask(next))) {
			/*
			 * On established mms, the mm_cpumask is only changed
			 * from irq context, from ptep_clear_flush() while in
			 * lazy tlb mode, and here. Irqs are blocked during
			 * schedule, protecting us from simultaneous changes.
			 */
			cpumask_set_cpu(cpu, mm_cpumask(next));
			/*
			 * We were in lazy tlb mode and leave_mm disabled
			 * tlb flush IPI delivery. We must reload CR3
			 * to make sure to use no freed page tables.
			 */
			load_cr3(next->pgd);
			trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);
			load_mm_cr4(next);
			xen_new_user_pt(next->pgd);
			load_mm_ldt(next);
		}
	}
#endif
}

#define activate_mm(prev, next)			\
do {						\
	xen_activate_mm(prev, next);		\
	switch_mm((prev), (next), NULL);	\
} while (0);

#ifdef CONFIG_X86_32
#define deactivate_mm(tsk, mm)			\
do {						\
	lazy_load_gs(0);			\
} while (0)
#else
#define deactivate_mm(tsk, mm)			\
do {						\
	load_gs_index(0);			\
	loadsegment(fs, 0);			\
} while (0)
#endif

#ifndef CONFIG_XEN
static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
	paravirt_arch_dup_mmap(oldmm, mm);
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
	paravirt_arch_exit_mmap(mm);
}
#endif

#ifdef CONFIG_X86_64
static inline bool is_64bit_mm(struct mm_struct *mm)
{
	return	!config_enabled(CONFIG_IA32_EMULATION) ||
		!(mm->context.ia32_compat == TIF_IA32);
}
#else
static inline bool is_64bit_mm(struct mm_struct *mm)
{
	return false;
}
#endif

static inline void arch_bprm_mm_init(struct mm_struct *mm,
		struct vm_area_struct *vma)
{
	mpx_mm_init(mm);
}

static inline void arch_unmap(struct mm_struct *mm, struct vm_area_struct *vma,
			      unsigned long start, unsigned long end)
{
	/*
	 * mpx_notify_unmap() goes and reads a rarely-hot
	 * cacheline in the mm_struct.  That can be expensive
	 * enough to be seen in profiles.
	 *
	 * The mpx_notify_unmap() call and its contents have been
	 * observed to affect munmap() performance on hardware
	 * where MPX is not present.
	 *
	 * The unlikely() optimizes for the fast case: no MPX
	 * in the CPU, or no MPX use in the process.  Even if
	 * we get this wrong (in the unlikely event that MPX
	 * is widely enabled on some system) the overhead of
	 * MPX itself (reading bounds tables) is expected to
	 * overwhelm the overhead of getting this unlikely()
	 * consistently wrong.
	 */
	if (unlikely(cpu_feature_enabled(X86_FEATURE_MPX)))
		mpx_notify_unmap(mm, vma, start, end);
}

#endif /* _ASM_X86_MMU_CONTEXT_H */
