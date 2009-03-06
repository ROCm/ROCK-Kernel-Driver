/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@lxorguk.ukuu.org.uk>
 *	(c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *      (c) 2002,2003 Andi Kleen, SuSE Labs.
 *
 *	i386 and x86_64 integration by Glauber Costa <gcosta@redhat.com>
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>

#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <mach_ipi.h>
#include <xen/evtchn.h>
/*
 *	Some notes on x86 processor bugs affecting SMP operation:
 *
 *	Pentium, Pentium Pro, II, III (and all CPUs) have bugs.
 *	The Linux implications for SMP are handled as follows:
 *
 *	Pentium III / [Xeon]
 *		None of the E1AP-E3AP errata are visible to the user.
 *
 *	E1AP.	see PII A1AP
 *	E2AP.	see PII A2AP
 *	E3AP.	see PII A3AP
 *
 *	Pentium II / [Xeon]
 *		None of the A1AP-A3AP errata are visible to the user.
 *
 *	A1AP.	see PPro 1AP
 *	A2AP.	see PPro 2AP
 *	A3AP.	see PPro 7AP
 *
 *	Pentium Pro
 *		None of 1AP-9AP errata are visible to the normal user,
 *	except occasional delivery of 'spurious interrupt' as trap #15.
 *	This is very rare and a non-problem.
 *
 *	1AP.	Linux maps APIC as non-cacheable
 *	2AP.	worked around in hardware
 *	3AP.	fixed in C0 and above steppings microcode update.
 *		Linux does not use excessive STARTUP_IPIs.
 *	4AP.	worked around in hardware
 *	5AP.	symmetric IO mode (normal Linux operation) not affected.
 *		'noapic' mode has vector 0xf filled out properly.
 *	6AP.	'noapic' mode might be affected - fixed in later steppings
 *	7AP.	We do not assume writes to the LVT deassering IRQs
 *	8AP.	We do not enable low power mode (deep sleep) during MP bootup
 *	9AP.	We do not use mixed mode
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesn't have the work
 *	arounds. Basically that's so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	6AP.	not affected - worked around in hardware
 *	7AP.	not affected - worked around in hardware
 *	8AP.	worked around in hardware - we get explicit CS errors if not
 *	9AP.	only 'noapic' mode affected. Might generate spurious
 *		interrupts, we log only the first one and count the
 *		rest silently.
 *	10AP.	not affected - worked around in hardware
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *	12AP.	not affected - worked around in hardware
 *	13AP.	not affected - worked around in hardware
 *	14AP.	we always deassert INIT during bootup
 *	15AP.	not affected - worked around in hardware
 *	16AP.	not affected - worked around in hardware
 *	17AP.	not affected - worked around in hardware
 *	18AP.	not affected - worked around in hardware
 *	19AP.	not affected - worked around in BIOS
 *
 *	If this sounds worrying believe me these bugs are either ___RARE___,
 *	or are signal timing bugs worked around in hardware and there's
 *	about nothing of note with C stepping upwards.
 */

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
void xen_smp_send_reschedule(int cpu)
{
	if (unlikely(cpu_is_offline(cpu))) {
		WARN_ON(1);
		return;
	}
	send_IPI_mask(cpumask_of(cpu), RESCHEDULE_VECTOR);
}

void xen_send_call_func_single_ipi(int cpu)
{
	send_IPI_mask(cpumask_of(cpu), CALL_FUNC_SINGLE_VECTOR);
}

void xen_send_call_func_ipi(const struct cpumask *mask)
{
	send_IPI_mask_allbutself(mask, CALL_FUNCTION_VECTOR);
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */

void xen_smp_send_stop(void)
{
	unsigned long flags;

	smp_call_function(stop_this_cpu, NULL, 0);
	local_irq_save(flags);
	disable_all_local_evtchn();
	local_irq_restore(flags);
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
irqreturn_t smp_reschedule_interrupt(int irq, void *dev_id)
{
	inc_irq_stat(irq_resched_count);
	return IRQ_HANDLED;
}

irqreturn_t smp_call_function_interrupt(int irq, void *dev_id)
{
	irq_enter();
	generic_smp_call_function_interrupt();
	inc_irq_stat(irq_call_count);
	irq_exit();

	return IRQ_HANDLED;
}

irqreturn_t smp_call_function_single_interrupt(int irq, void *dev_id)
{
	irq_enter();
	generic_smp_call_function_single_interrupt();
	inc_irq_stat(irq_call_count);
	irq_exit();

	return IRQ_HANDLED;
}
