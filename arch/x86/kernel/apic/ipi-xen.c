#include <linux/cpumask.h>
#include <linux/interrupt.h>

#include <asm/smp.h>
#include <asm/ipi.h>

#include <xen/evtchn.h>

void xen_send_IPI_mask_allbutself(const struct cpumask *cpumask, int vector)
{
	unsigned int cpu, this_cpu = smp_processor_id();

	WARN_ON(!cpumask_subset(cpumask, cpu_online_mask));
	for_each_cpu_and(cpu, cpumask, cpu_online_mask)
		if (cpu != this_cpu)
			notify_remote_via_ipi(vector, cpu);
}

void xen_send_IPI_mask(const struct cpumask *cpumask, int vector)
{
	unsigned int cpu;

	WARN_ON(!cpumask_subset(cpumask, cpu_online_mask));
	for_each_cpu_and(cpu, cpumask, cpu_online_mask)
		notify_remote_via_ipi(vector, cpu);
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
