#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

static inline unsigned long calculate_ldr(unsigned long old)
{
	unsigned long id;

	id = 1UL << smp_processor_id();
	return ((old & ~APIC_LDR_MASK) | SET_APIC_LOGICAL_ID(id));
}

#define APIC_DFR_VALUE	(APIC_DFR_FLAT)

#define TARGET_CPUS (0xf)

#define APIC_BROADCAST_ID      0x0F
#define check_apicid_used(bitmap, apicid) (bitmap & (1 << apicid))

static inline void summit_check(char *oem, char *productid) 
{
}

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		"NUMA-Q", nr_ioapics);
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	return ( ((mps_cpu/4)*16) + (1<<(mps_cpu%4)) );
}

static inline unsigned long apicid_to_cpu_present(int apicid)
{
	return (1ul << apicid);
}

#endif /* __ASM_MACH_APIC_H */
