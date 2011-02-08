#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/cpu.h>
#include <linux/module.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/apic.h>
#include <asm/proto.h>
#include <asm/ipi.h>

#include <xen/evtchn.h>

void xen_send_IPI_mask_allbutself(const struct cpumask *cpumask, int vector)
{
	unsigned int cpu;
	unsigned long flags;

	local_irq_save(flags);
	WARN_ON(!cpumask_subset(cpumask, cpu_online_mask));
	for_each_cpu_and(cpu, cpumask, cpu_online_mask)
		if (cpu != smp_processor_id())
			notify_remote_via_ipi(vector, cpu);
	local_irq_restore(flags);
}

void xen_send_IPI_mask(const struct cpumask *cpumask, int vector)
{
	unsigned int cpu;
	unsigned long flags;

	local_irq_save(flags);
	WARN_ON(!cpumask_subset(cpumask, cpu_online_mask));
	for_each_cpu_and(cpu, cpumask, cpu_online_mask)
		notify_remote_via_ipi(vector, cpu);
	local_irq_restore(flags);
}

void xen_send_IPI_allbutself(int vector)
{
	xen_send_IPI_mask_allbutself(cpu_online_mask, vector);
}

void xen_send_IPI_all(int vector)
{
	xen_send_IPI_mask(cpu_online_mask, vector);
}

void xen_send_IPI_self(int vector)
{
	notify_remote_via_ipi(vector, smp_processor_id());
}
