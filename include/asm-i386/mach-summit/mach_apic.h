#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

extern int x86_summit;

#define XAPIC_DEST_CPUS_MASK    0x0Fu
#define XAPIC_DEST_CLUSTER_MASK 0xF0u

#define xapic_phys_to_log_apicid(phys_apic) ( (1ul << ((phys_apic) & 0x3)) |\
		((phys_apic) & XAPIC_DEST_CLUSTER_MASK) )

static inline unsigned long calculate_ldr(unsigned long old)
{
	unsigned long id;

	if (x86_summit)
		id = xapic_phys_to_log_apicid(hard_smp_processor_id());
	else
		id = 1UL << smp_processor_id();
	return ((old & ~APIC_LDR_MASK) | SET_APIC_LOGICAL_ID(id));
}

#define APIC_DFR_VALUE	(x86_summit ? APIC_DFR_CLUSTER : APIC_DFR_FLAT)
#define TARGET_CPUS	(x86_summit ? XAPIC_DEST_CPUS_MASK : cpu_online_map)

#define APIC_BROADCAST_ID     (x86_summit ? 0xFF : 0x0F)
#define check_apicid_used(bitmap, apicid) (0)

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		(x86_summit ? "Summit" : "Flat"), nr_ioapics);
}

static inline int apicid_to_node(int logical_apicid)
{
	return (logical_apicid >> 5);          /* 2 clusterids per CEC */
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (x86_summit)
		return (int) raw_phys_apicid[mps_cpu];
	else
		return mps_cpu;
}

static inline ulong ioapic_phys_id_map(ulong phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return (x86_summit ? 0x0F : phys_map);
}

static inline unsigned long apicid_to_phys_cpu_present(int apicid)
{
	if (x86_summit)
		return (1ul << (((apicid >> 4) << 2) | (apicid & 0x3)));
	else
		return (1ul << apicid);
}

static inline void setup_portio_remap(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return (1);
}

#endif /* __ASM_MACH_APIC_H */
