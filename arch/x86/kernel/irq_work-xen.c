/*
 * x86/Xen specific code for irq_work
 */

#include <linux/kernel.h>
#include <linux/irq_work.h>
#include <linux/hardirq.h>
#include <asm/ipi.h>

#ifdef CONFIG_SMP
void smp_irq_work_interrupt(struct pt_regs *regs)
{
	inc_irq_stat(apic_irq_work_irqs);
	irq_work_run();
}

void arch_irq_work_raise(void)
{
	xen_send_IPI_self(IRQ_WORK_VECTOR);
}
#endif
