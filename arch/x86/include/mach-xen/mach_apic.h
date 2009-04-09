#ifndef _ASM_X86_MACH_XEN_MACH_APIC_H
#define _ASM_X86_MACH_XEN_MACH_APIC_H

#include <asm/smp.h>

#ifdef CONFIG_X86_64

#include <asm/genapic.h>
#define INT_DELIVERY_MODE (genapic->int_delivery_mode)
#define INT_DEST_MODE (genapic->int_dest_mode)
#define TARGET_CPUS	  (genapic->target_cpus())
#define phys_pkg_id	(genapic->phys_pkg_id)
#define send_IPI_self (genapic->send_IPI_self)
extern void setup_apic_routing(void);

#else

#ifdef CONFIG_SMP
#define TARGET_CPUS cpu_online_mask
#else
#define TARGET_CPUS cpumask_of(0)
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

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}

#endif /* CONFIG_X86_64 */

#endif /* _ASM_X86_MACH_XEN_MACH_APIC_H */
