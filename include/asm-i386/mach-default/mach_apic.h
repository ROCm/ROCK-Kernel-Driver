#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#define APIC_DFR_VALUE	(APIC_DFR_FLAT)

#ifdef CONFIG_SMP
 #define TARGET_CPUS (cpu_online_map)
#else
 #define TARGET_CPUS 0x01
#endif

#define no_balance_irq (0)

#define APIC_BROADCAST_ID      0x0F
#define check_apicid_used(bitmap, apicid) (bitmap & (1 << apicid))

static inline void summit_check(char *oem, char *productid) 
{
}

static inline int apic_id_registered(void)
{
	return (test_bit(GET_APIC_ID(apic_read(APIC_ID)), 
						&phys_cpu_present_map));
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

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
					"Flat", nr_ioapics);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	return  mps_cpu;
}

static inline unsigned long apicid_to_cpu_present(int apicid)
{
	return (1ul << apicid);
}

#endif /* __ASM_MACH_APIC_H */
