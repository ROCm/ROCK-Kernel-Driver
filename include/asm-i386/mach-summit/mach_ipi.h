#ifndef __ASM_MACH_IPI_H
#define __ASM_MACH_IPI_H

static inline void send_IPI_mask_sequence(int mask, int vector);

static inline void send_IPI_mask(int mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	int cpu;
	/*
	 * if there are no other CPUs in the system then we get an APIC send 
	 * error if we try to broadcast, thus avoid sending IPIs in this case.
	 */
	if (!(num_online_cpus() > 1))
		return;

	/* Pointless. Use send_IPI_mask to do this instead */
	for (cpu = 0; cpu < NR_CPUS; ++cpu)
		if (cpu_online(cpu) && cpu != smp_processor_id())
			send_IPI_mask(1 << cpu, vector);

	return;
}

static inline void send_IPI_all(int vector)
{
	int cpu;

	/* Pointless. Use send_IPI_mask to do this instead */
	for (cpu = 0; cpu < NR_CPUS; ++cpu)
		if (cpu_online(cpu))
			send_IPI_mask(1 << cpu, vector);
}

#endif /* __ASM_MACH_IPI_H */
