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

#define APIC_BROADCAST_ID      0x0F
#define check_apicid_used(bitmap, apicid) (bitmap & (1 << apicid))

static inline void summit_check(char *oem, char *productid) 
{
}

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		(clustered_apic_mode ? "NUMA-Q" : "Flat"), nr_ioapics);
}

#endif /* __ASM_MACH_APIC_H */
