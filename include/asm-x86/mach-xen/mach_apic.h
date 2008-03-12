#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#include <linux/cpumask.h>

#ifdef CONFIG_SMP
#define TARGET_CPUS cpu_online_map
#else
#define TARGET_CPUS cpumask_of_cpu(0)
#endif

#define INT_DELIVERY_MODE dest_LowestPrio
#define INT_DEST_MODE 1

static inline void setup_apic_routing(void)
{
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}

static inline unsigned int cpu_mask_to_apicid(cpumask_t cpumask)
{
	return cpus_addr(cpumask)[0];
}

#endif /* __ASM_MACH_APIC_H */
