/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *      (c) 2002,2003 Andi Kleen, SuSE Labs.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/interrupt.h>

#include <asm/mtrr.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */

static inline unsigned int __prepare_ICR (unsigned int shortcut, int vector)
{
	unsigned int icr =  APIC_DM_FIXED | shortcut | vector | APIC_DEST_LOGICAL;
	if (vector == KDB_VECTOR) 
		icr = (icr & (~APIC_VECTOR_MASK)) | APIC_DM_NMI; 		
	return icr;
}

static inline int __prepare_ICR2 (unsigned int mask)
{
	return SET_APIC_DEST_FIELD(mask);
}

static inline void __send_IPI_shortcut(unsigned int shortcut, int vector)
{
	/*
	 * Subtle. In the case of the 'never do double writes' workaround
	 * we have to lock out interrupts to be safe.  As we don't care
	 * of the value read we use an atomic rmw access to avoid costly
	 * cli/sti.  Otherwise we use an even cheaper single atomic write
	 * to the APIC.
	 */
	unsigned int cfg;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	/*
	 * No need to touch the target chip field
	 */
	cfg = __prepare_ICR(shortcut, vector);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
}

static inline void send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then
	 * we get an APIC send error if we try to broadcast.
	 * thus we have to avoid sending IPIs in this case.
	 */
	if (num_online_cpus() > 1)
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

static inline void send_IPI_all(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

void send_IPI_self(int vector)
{
	__send_IPI_shortcut(APIC_DEST_SELF, vector);
}

static inline void send_IPI_mask(cpumask_t cpumask, int vector)
{
	unsigned long mask = cpus_coerce(cpumask);
	unsigned long cfg;
	unsigned long flags;

	local_save_flags(flags);
	local_irq_disable();

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	/*
	 * prepare target chip field
	 */
	cfg = __prepare_ICR2(mask);
	apic_write_around(APIC_ICR2, cfg);

	/*
	 * program the ICR 
	 */
	cfg = __prepare_ICR(0, vector);
	
	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
	local_irq_restore(flags);
}

/*
 *	Smarter SMP flushing macros. 
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 */

static cpumask_t flush_cpumask;
static struct mm_struct * flush_mm;
static unsigned long flush_va;
static spinlock_t tlbstate_lock = SPIN_LOCK_UNLOCKED;
#define FLUSH_ALL	0xffffffff

/*
 * We cannot call mmdrop() because we are in interrupt context, 
 * instead update mm->cpu_vm_mask.
 */
static inline void leave_mm (unsigned long cpu)
{
	if (read_pda(mmu_state) == TLBSTATE_OK)
		BUG();
	clear_bit(cpu, &read_pda(active_mm)->cpu_vm_mask);
	__flush_tlb();
}

/*
 *
 * The flush IPI assumes that a thread switch happens in this order:
 * [cpu0: the cpu that switches]
 * 1) switch_mm() either 1a) or 1b)
 * 1a) thread switch to a different mm
 * 1a1) clear_bit(cpu, &old_mm->cpu_vm_mask);
 * 	Stop ipi delivery for the old mm. This is not synchronized with
 * 	the other cpus, but smp_invalidate_interrupt ignore flush ipis
 * 	for the wrong mm, and in the worst case we perform a superfluous
 * 	tlb flush.
 * 1a2) set cpu mmu_state to TLBSTATE_OK
 * 	Now the smp_invalidate_interrupt won't call leave_mm if cpu0
 *	was in lazy tlb mode.
 * 1a3) update cpu active_mm
 * 	Now cpu0 accepts tlb flushes for the new mm.
 * 1a4) set_bit(cpu, &new_mm->cpu_vm_mask);
 * 	Now the other cpus will send tlb flush ipis.
 * 1a4) change cr3.
 * 1b) thread switch without mm change
 *	cpu active_mm is correct, cpu0 already handles
 *	flush ipis.
 * 1b1) set cpu mmu_state to TLBSTATE_OK
 * 1b2) test_and_set the cpu bit in cpu_vm_mask.
 * 	Atomically set the bit [other cpus will start sending flush ipis],
 * 	and test the bit.
 * 1b3) if the bit was 0: leave_mm was called, flush the tlb.
 * 2) switch %%esp, ie current
 *
 * The interrupt must handle 2 special cases:
 * - cr3 is changed before %%esp, ie. it cannot use current->{active_,}mm.
 * - the cpu performs speculative tlb reads, i.e. even if the cpu only
 *   runs in kernel space, the cpu could load tlb entries for user space
 *   pages.
 *
 * The good news is that cpu mmu_state is local to each cpu, no
 * write/read ordering problems.
 */

/*
 * TLB flush IPI:
 *
 * 1) Flush the tlb entries if the cpu uses the mm that's being flushed.
 * 2) Leave the mm if we are in the lazy tlb mode.
 */

asmlinkage void smp_invalidate_interrupt (void)
{
	unsigned long cpu;

	cpu = get_cpu();

	if (!cpu_isset(cpu, flush_cpumask))
		goto out;
		/* 
		 * This was a BUG() but until someone can quote me the
		 * line from the intel manual that guarantees an IPI to
		 * multiple CPUs is retried _only_ on the erroring CPUs
		 * its staying as a return
		 *
		 * BUG();
		 */
		 
	if (flush_mm == read_pda(active_mm)) {
		if (read_pda(mmu_state) == TLBSTATE_OK) {
			if (flush_va == FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(flush_va);
		} else
			leave_mm(cpu);
	}
	ack_APIC_irq();
	cpu_clear(cpu, flush_cpumask);

out:
	put_cpu_no_resched();
}

static void flush_tlb_others(cpumask_t cpumask, struct mm_struct *mm,
						unsigned long va)
{
	cpumask_t tmp;
	/*
	 * A couple of (to be removed) sanity checks:
	 *
	 * - we do not send IPIs to not-yet booted CPUs.
	 * - current CPU must not be in mask
	 * - mask must exist :)
	 */
	BUG_ON(cpus_empty(cpumask));
	cpus_and(tmp, cpumask, cpu_online_map);
	BUG_ON(!cpus_equal(tmp, cpumask));
	BUG_ON(cpu_isset(smp_processor_id(), cpumask));
	if (!mm)
		BUG();

	/*
	 * I'm not happy about this global shared spinlock in the
	 * MM hot path, but we'll see how contended it is.
	 * Temporarily this turns IRQs off, so that lockups are
	 * detected by the NMI watchdog.
	 */
	spin_lock(&tlbstate_lock);
	
	flush_mm = mm;
	flush_va = va;
	cpus_or(flush_cpumask, cpumask, flush_cpumask);

	/*
	 * We have to send the IPI only to
	 * CPUs affected.
	 */
	send_IPI_mask(cpumask, INVALIDATE_TLB_VECTOR);

	while (!cpus_empty(flush_cpumask))
		mb();	/* nothing. lockup detection does not belong here */;

	flush_mm = NULL;
	flush_va = 0;
	spin_unlock(&tlbstate_lock);
}
	
void flush_tlb_current_task(void)
{
	struct mm_struct *mm = current->mm;
	cpumask_t cpu_mask;

	preempt_disable();
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);

	local_flush_tlb();
	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);
	preempt_enable();
}

void flush_tlb_mm (struct mm_struct * mm)
{
	cpumask_t cpu_mask;

	preempt_disable();
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);

	if (current->active_mm == mm) {
		if (current->mm)
			local_flush_tlb();
		else
			leave_mm(smp_processor_id());
	}
	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);

	preempt_enable();
}

void flush_tlb_page(struct vm_area_struct * vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;
	cpumask_t cpu_mask;

	preempt_disable();
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);

	if (current->active_mm == mm) {
		if(current->mm)
			__flush_tlb_one(va);
		 else
		 	leave_mm(smp_processor_id());
	}

	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, va);

	preempt_enable();
}

static void do_flush_tlb_all(void* info)
{
	unsigned long cpu = smp_processor_id();

	__flush_tlb_all();
	if (read_pda(mmu_state) == TLBSTATE_LAZY)
		leave_mm(cpu);
}

void flush_tlb_all(void)
{
	on_each_cpu(do_flush_tlb_all, 0, 1, 1);
}

void smp_kdb_stop(void)
{
	send_IPI_allbutself(KDB_VECTOR);
}

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */

void smp_send_reschedule(int cpu)
{
	send_IPI_mask(cpumask_of_cpu(cpu), RESCHEDULE_VECTOR);
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 */
static spinlock_t call_lock = SPIN_LOCK_UNLOCKED;

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
};

static struct call_data_struct * call_data;

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
			int wait)
/*
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> currently unused.
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
{
	struct call_data_struct data;
	int cpus = num_online_cpus()-1;

	if (!cpus)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock(&call_lock);
	call_data = &data;
	wmb();
	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(CALL_FUNCTION_VECTOR);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		barrier();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			barrier();
	spin_unlock(&call_lock);

	return 0;
}

void smp_stop_cpu(void)
{
	/*
	 * Remove this CPU:
	 */
	cpu_clear(smp_processor_id(), cpu_online_map);
	local_irq_disable();
	disable_local_APIC();
	local_irq_enable(); 
}

static void smp_really_stop_cpu(void *dummy)
{
	smp_stop_cpu(); 
	for (;;) 
		asm("hlt"); 
} 

void smp_send_stop(void)
{
	smp_call_function(smp_really_stop_cpu, NULL, 1, 0);
	smp_stop_cpu();
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
asmlinkage void smp_reschedule_interrupt(void)
{
	ack_APIC_irq();
}

asmlinkage void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	ack_APIC_irq();
	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	mb();
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	irq_enter();
	(*func)(info);
	irq_exit();
	if (wait) {
		mb();
		atomic_inc(&call_data->finished);
	}
}
