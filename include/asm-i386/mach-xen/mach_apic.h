#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#include <mach_apicdef.h>
#include <asm/smp.h>

static inline cpumask_t target_cpus(void)
{
#ifdef CONFIG_SMP
	return cpu_online_map;
#else
	return cpumask_of_cpu(0);
#endif
}
#define TARGET_CPUS (target_cpus())

#define INT_DELIVERY_MODE dest_LowestPrio
#define INT_DEST_MODE 1     /* logical delivery broadcast to all procs */

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

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

#endif /* __ASM_MACH_APIC_H */
