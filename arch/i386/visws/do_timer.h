/* defines for inline arch setup functions */

#include <asm/fixmap.h>
#include <asm/cobalt.h>

static inline void do_timer_interrupt_hook(struct pt_regs *regs)
{
	/* Clear the interrupt */
	co_cpu_write(CO_CPU_STAT,co_cpu_read(CO_CPU_STAT) & ~CO_STAT_TIMEINTR);

	do_timer(regs);
/*
 * In the SMP case we use the local APIC timer interrupt to do the
 * profiling, except when we simulate SMP mode on a uniprocessor
 * system, in that case we have to call the local interrupt handler.
 */
#ifndef CONFIG_X86_LOCAL_APIC
	if (!user_mode(regs))
		x86_do_profile(regs->eip);
#else
	if (!using_apic_timer)
		smp_local_timer_interrupt(regs);
#endif
}
