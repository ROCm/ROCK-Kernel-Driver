#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#include <mach_apicdef.h>

#define APIC_DFR_VALUE	(APIC_DFR_FLAT)

static inline unsigned long target_cpus(void)
{ 
#ifdef CONFIG_SMP
	return cpu_online_map;
#else
	return 1; 
#endif
} 
#define TARGET_CPUS (target_cpus())

#define NO_BALANCE_IRQ (0)
#define esr_disable (0)

#define INT_DELIVERY_MODE dest_LowestPrio
#define INT_DEST_MODE 1     /* logical delivery broadcast to all procs */

#define APIC_BROADCAST_ID      0x0F

static inline unsigned long check_apicid_used(unsigned long bitmap, int apicid) 
{ 
	return (bitmap & (1UL << apicid)); 
} 

static inline unsigned long check_apicid_present(int bit) 
{
	return (phys_cpu_present_map & (1UL << bit));
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void init_apic_ldr(void)
{
	unsigned long val;

	apic_write_around(APIC_DFR, APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(1UL << smp_processor_id());
	apic_write_around(APIC_LDR, val);
}

static inline unsigned long ioapic_phys_id_map(unsigned long phys_map)
{
	return phys_map;
}

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
					"Flat", nr_ioapics);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}

/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
	return 1 << cpu;
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	return  mps_cpu;
}

static inline unsigned long apicid_to_cpu_present(int phys_apicid)
{
	return (1ul << phys_apicid);
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
	return test_bit(boot_cpu_physical_apicid, &phys_cpu_present_map);
}

static inline int apic_id_registered(void)
{
	return (test_bit(GET_APIC_ID(apic_read(APIC_ID)), 
						&phys_cpu_present_map));
}

static inline unsigned int cpu_mask_to_apicid (unsigned long cpumask)
{
	return cpumask;
}

#endif /* __ASM_MACH_APIC_H */
