#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

static inline unsigned long calculate_ldr(unsigned long old)
{
	unsigned long id;

	id = 1UL << smp_processor_id();
	return ((old & ~APIC_LDR_MASK) | SET_APIC_LOGICAL_ID(id));
}

#define APIC_DFR_VALUE	(APIC_DFR_FLAT)

#ifdef CONFIG_SMP
 #define TARGET_CPUS (clustered_apic_mode ? 0xf : cpu_online_map)
#else
 #define TARGET_CPUS 0x01
#endif

#endif /* __ASM_MACH_APIC_H */
