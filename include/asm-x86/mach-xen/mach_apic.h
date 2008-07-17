#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#include <linux/cpumask.h>

#ifdef CONFIG_X86_64

#include <asm/genapic.h>
#define INT_DELIVERY_MODE (genapic->int_delivery_mode)
#define INT_DEST_MODE (genapic->int_dest_mode)
#define TARGET_CPUS	  (genapic->target_cpus())
#define apic_id_registered (genapic->apic_id_registered)
#define init_apic_ldr (genapic->init_apic_ldr)
#define cpu_mask_to_apicid (genapic->cpu_mask_to_apicid)
#define phys_pkg_id	(genapic->phys_pkg_id)
#define vector_allocation_domain    (genapic->vector_allocation_domain)
extern void setup_apic_routing(void);

#else

#ifdef CONFIG_SMP
#define TARGET_CPUS cpu_online_map
#else
#define TARGET_CPUS cpumask_of_cpu(0)
#endif

#define INT_DELIVERY_MODE dest_LowestPrio
#define INT_DEST_MODE 1

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic;
}

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

#endif /* CONFIG_X86_64 */

#endif /* __ASM_MACH_APIC_H */
