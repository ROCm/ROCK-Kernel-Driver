#ifndef __ASM_MACH_IPI_H
#define __ASM_MACH_IPI_H

inline void send_IPI_mask_sequence(cpumask_t mask, int vector);

static inline void send_IPI_mask(cpumask_t mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	cpumask_t mask = cpumask_of_cpu(smp_processor_id());
	cpus_complement(mask);
	cpus_and(mask, mask, cpu_online_map);
	if (!cpus_empty(mask))
		send_IPI_mask(mask, vector);
}

static inline void send_IPI_all(int vector)
{
	send_IPI_mask(cpu_online_map, vector);
}

#endif /* __ASM_MACH_IPI_H */
