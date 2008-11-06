/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Xen APIC subarch code.  Maximum 8 CPUs, logical delivery.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 *
 * Hacked to pieces for Xen by Chris Wright.
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#ifdef CONFIG_XEN_PRIVILEGED_GUEST
#include <asm/smp.h>
#else
#include <asm/apic.h>
#endif
#include <asm/genapic.h>
#include <xen/evtchn.h>

static inline void __send_IPI_one(unsigned int cpu, int vector)
{
	notify_remote_via_ipi(vector, cpu);
}

static void xen_send_IPI_shortcut(unsigned int shortcut,
				  const cpumask_t *cpumask, int vector)
{
	unsigned long flags;
	int cpu;

	switch (shortcut) {
	case APIC_DEST_SELF:
		__send_IPI_one(smp_processor_id(), vector);
		break;
	case APIC_DEST_ALLBUT:
		local_irq_save(flags);
		WARN_ON(!cpus_subset(*cpumask, cpu_online_map));
		for_each_possible_cpu(cpu) {
			if (cpu == smp_processor_id())
				continue;
			if (cpu_isset(cpu, *cpumask)) {
				__send_IPI_one(cpu, vector);
			}
		}
		local_irq_restore(flags);
		break;
	case APIC_DEST_ALLINC:
		local_irq_save(flags);
		WARN_ON(!cpus_subset(*cpumask, cpu_online_map));
		for_each_possible_cpu(cpu) {
			if (cpu_isset(cpu, *cpumask)) {
				__send_IPI_one(cpu, vector);
			}
		}
		local_irq_restore(flags);
		break;
	default:
		printk("XXXXXX __send_IPI_shortcut %08x vector %d\n", shortcut,
		       vector);
		break;
	}
}

static const cpumask_t *xen_target_cpus(void)
{
	return &cpu_online_map;
}

static void xen_send_IPI_mask(const cpumask_t *cpumask, int vector)
{
	xen_send_IPI_shortcut(APIC_DEST_ALLINC, cpumask, vector);
}

static void xen_send_IPI_mask_allbutself(const cpumask_t *cpumask,
					 int vector)
{
	xen_send_IPI_shortcut(APIC_DEST_ALLBUT, cpumask, vector);
}

static void xen_send_IPI_allbutself(int vector)
{
	xen_send_IPI_shortcut(APIC_DEST_ALLBUT, &cpu_online_map, vector);
}

static void xen_send_IPI_all(int vector)
{
	xen_send_IPI_shortcut(APIC_DEST_ALLINC, &cpu_online_map, vector);
}

static void xen_send_IPI_self(int vector)
{
	xen_send_IPI_shortcut(APIC_DEST_SELF, NULL, vector);
}

static unsigned int xen_cpu_mask_to_apicid(const cpumask_t *cpumask)
{
	return cpus_addr(*cpumask)[0];
}

static unsigned int phys_pkg_id(int index_msb)
{
	u32 ebx;

	ebx = cpuid_ebx(1);
	return ((ebx >> 24) & 0xFF) >> index_msb;
}

struct genapic apic_xen =  {
	.name = "xen",
#ifdef CONFIG_XEN_PRIVILEGED_GUEST
	.int_delivery_mode = dest_LowestPrio,
#endif
	.int_dest_mode = 1,
	.target_cpus = xen_target_cpus,
	.send_IPI_all = xen_send_IPI_all,
	.send_IPI_allbutself = xen_send_IPI_allbutself,
	.send_IPI_mask = xen_send_IPI_mask,
	.send_IPI_mask_allbutself = xen_send_IPI_mask_allbutself,
	.send_IPI_self = xen_send_IPI_self,
	.cpu_mask_to_apicid = xen_cpu_mask_to_apicid,
	.phys_pkg_id = phys_pkg_id,
};
