#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

extern u8 bios_cpu_apicid[];

#define xapic_phys_to_log_apicid(cpu) (bios_cpu_apicid[cpu])
#define esr_disable (1)

static inline int apic_id_registered(void)
{
	        return (1);
}

static inline unsigned long target_cpus(void)
{ 
#if defined CONFIG_ES7000_CLUSTERED_APIC
	return (0xff);
#else
	return (bios_cpu_apicid[smp_processor_id()]);
#endif
}
#define TARGET_CPUS	(target_cpus())

#if defined CONFIG_ES7000_CLUSTERED_APIC
#define APIC_DFR_VALUE		(APIC_DFR_CLUSTER)
#define INT_DELIVERY_MODE	(dest_LowestPrio)
#define INT_DEST_MODE		(1)    /* logical delivery broadcast to all procs */
#define NO_BALANCE_IRQ 		(1)
#undef  WAKE_SECONDARY_VIA_INIT
#define WAKE_SECONDARY_VIA_MIP
#else
#define APIC_DFR_VALUE		(APIC_DFR_FLAT)
#define INT_DELIVERY_MODE	(dest_Fixed)
#define INT_DEST_MODE		(0)    /* phys delivery to target procs */
#define NO_BALANCE_IRQ 		(0)
#undef  APIC_DEST_LOGICAL
#define APIC_DEST_LOGICAL	0x0
#define WAKE_SECONDARY_VIA_INIT
#endif

#define APIC_BROADCAST_ID	(0xff)

static inline unsigned long check_apicid_used(unsigned long bitmap, int apicid) 
{ 
	return 0;
} 
static inline unsigned long check_apicid_present(int bit) 
{
	return (phys_cpu_present_map & (1 << bit));
}

#define apicid_cluster(apicid) (apicid & 0xF0)

static inline unsigned long calculate_ldr(int cpu)
{
	unsigned long id;
	id = xapic_phys_to_log_apicid(cpu);
	return (SET_APIC_LOGICAL_ID(id));
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LdR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write_around(APIC_DFR, APIC_DFR_VALUE);
	val = calculate_ldr(cpu);
	apic_write_around(APIC_LDR, val);
}

extern void es7000_sw_apic(void);
static inline void enable_apic_mode(void)
{
	es7000_sw_apic();
	return;
}

extern int apic_version [MAX_APICS];
static inline void clustered_apic_check(void)
{
	int apic = bios_cpu_apicid[smp_processor_id()];
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs, target cpus %lx\n",
		(apic_version[apic] == 0x14) ? 
		"Physical Cluster" : "Logical Cluster", nr_ioapics, TARGET_CPUS);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}


static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (!mps_cpu)
		return boot_cpu_physical_apicid;
	else
		return (int) bios_cpu_apicid[mps_cpu];
}

static inline unsigned long apicid_to_cpu_present(int phys_apicid)
{
	static int cpu = 0;
	return (1ul << cpu++);
}

extern volatile u8 cpu_2_logical_apicid[];
/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
       return (int)cpu_2_logical_apicid[cpu];
}

static inline int mpc_apic_id(struct mpc_config_processor *m, int quad)
{
	printk("Processor #%d %ld:%ld APIC version %d\n",
	        m->mpc_apicid,
	        (m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
	        (m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
	        m->mpc_apicver);
	return (m->mpc_apicid);
}

static inline ulong ioapic_phys_id_map(ulong phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return (0xff);
}


static inline void setup_portio_remap(void)
{
}

extern unsigned int boot_cpu_physical_apicid;
static inline int check_phys_apicid_present(int cpu_physical_apicid)
{
	boot_cpu_physical_apicid = GET_APIC_ID(apic_read(APIC_ID));
	return (1);
}

static inline unsigned int cpu_mask_to_apicid (unsigned long cpumask)
{
	int num_bits_set;
	int cpus_found = 0;
	int cpu;
	int apicid;	

	if (cpumask == TARGET_CPUS)
		return cpumask;
	num_bits_set = hweight32(cpumask); 
	/* Return id to all */
	if (num_bits_set == 32)
		return TARGET_CPUS;
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
			apicid = new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

#endif /* __ASM_MACH_APIC_H */
