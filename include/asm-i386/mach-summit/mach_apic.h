#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#include <linux/config.h>
#include <asm/smp.h>

#ifdef CONFIG_X86_GENERICARCH
#define x86_summit 1	/* must be an constant expressiona for generic arch */
#else
extern int x86_summit;
#endif

#define esr_disable (x86_summit ? 1 : 0)
#define NO_BALANCE_IRQ (0)

#define XAPIC_DEST_CPUS_MASK    0x0Fu
#define XAPIC_DEST_CLUSTER_MASK 0xF0u

static inline unsigned long xapic_phys_to_log_apicid(int phys_apic) 
{
	return ( (1ul << ((phys_apic) & 0x3)) |
		 ((phys_apic) & XAPIC_DEST_CLUSTER_MASK) );
}

#define APIC_DFR_VALUE	(x86_summit ? APIC_DFR_CLUSTER : APIC_DFR_FLAT)

static inline unsigned long target_cpus(void)
{
	return (x86_summit ? XAPIC_DEST_CPUS_MASK : cpu_online_map);
} 
#define TARGET_CPUS	(target_cpus())

#define INT_DELIVERY_MODE (x86_summit ? dest_Fixed : dest_LowestPrio)
#define INT_DEST_MODE 1     /* logical delivery broadcast to all procs */

#define APIC_BROADCAST_ID     (0x0F)
static inline unsigned long check_apicid_used(unsigned long bitmap, int apicid) 
{
	return (x86_summit ? 0 : (bitmap & (1 << apicid)));
} 

/* we don't use the phys_cpu_present_map to indicate apicid presence */
static inline unsigned long check_apicid_present(int bit) 
{
	return (x86_summit ? 1 : (phys_cpu_present_map & (1 << bit)));
}

#define apicid_cluster(apicid) (apicid & 0xF0)

extern u8 bios_cpu_apicid[];

static inline void init_apic_ldr(void)
{
	unsigned long val, id;

	if (x86_summit)
		id = xapic_phys_to_log_apicid(hard_smp_processor_id());
	else
		id = 1UL << smp_processor_id();
	apic_write_around(APIC_DFR, APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write_around(APIC_LDR, val);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apic_id_registered(void)
{
	return 1;
}

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		(x86_summit ? "Summit" : "Flat"), nr_ioapics);
}

static inline int apicid_to_node(int logical_apicid)
{
	return (logical_apicid >> 5);          /* 2 clusterids per CEC */
}

/* Mapping from cpu number to logical apicid */
extern volatile u8 cpu_2_logical_apicid[];
static inline int cpu_to_logical_apicid(int cpu)
{
	return (int)cpu_2_logical_apicid[cpu];
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (x86_summit)
		return (int) bios_cpu_apicid[mps_cpu];
	else
		return mps_cpu;
}

static inline ulong ioapic_phys_id_map(ulong phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return (x86_summit ? 0x0F : phys_map);
}

static inline unsigned long apicid_to_cpu_present(int apicid)
{
	if (x86_summit)
		return 1;
	else
		return (1ul << apicid);
}

static inline int mpc_apic_id(struct mpc_config_processor *m, 
			struct mpc_config_translation *translation_record)
{
	printk("Processor #%d %ld:%ld APIC version %d\n",
			m->mpc_apicid,
			(m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
			(m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
			m->mpc_apicver);
	return (m->mpc_apicid);
}

static inline void setup_portio_remap(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	if (x86_summit)
		return (1);
	else
		return test_bit(boot_cpu_physical_apicid, &phys_cpu_present_map);
}

static inline unsigned int cpu_mask_to_apicid (unsigned long cpumask)
{
	int num_bits_set;
	int cpus_found = 0;
	int cpu;
	int apicid;	

	num_bits_set = hweight32(cpumask); 
	/* Return id to all */
	if (num_bits_set == 32)
		return (int) 0xFF;
	/* 
	 * The cpus in the mask must all be on the apic cluster.  If are not 
	 * on the same apicid cluster return default value of TARGET_CPUS. 
	 */
	cpu = ffs(cpumask)-1;
	apicid = cpu_to_logical_apicid(cpu);
	while (cpus_found < num_bits_set) {
		if (cpumask & (1 << cpu)) {
			int new_apicid = cpu_to_logical_apicid(cpu);
			if (apicid_cluster(apicid) != 
					apicid_cluster(new_apicid)){
				printk ("%s: Not a valid mask!\n",__FUNCTION__);
				return TARGET_CPUS;
			}
			apicid = apicid | new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

#endif /* __ASM_MACH_APIC_H */
