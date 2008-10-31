#ifndef __ASM_MACH_IPI_H
#define __ASM_MACH_IPI_H

void send_IPI_mask_sequence(const cpumask_t *mask, int vector);

static inline void send_IPI_mask(const cpumask_t *mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static inline void send_IPI_mask_allbutself(const cpumask_t *mask, int vector)
{
	cpumask_t allbutself = *mask;
	cpu_clear(smp_processor_id(), allbutself);

	if (!cpus_empty(allbutself))
		send_IPI_mask_sequence(&allbutself, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	send_IPI_mask_allbutself(&cpu_online_map, vector);
}

static inline void send_IPI_all(int vector)
{
	send_IPI_mask(&cpu_online_map, vector);
}

#endif /* __ASM_MACH_IPI_H */
