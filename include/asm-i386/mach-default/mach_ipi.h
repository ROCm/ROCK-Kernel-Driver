#ifndef __ASM_MACH_IPI_H
#define __ASM_MACH_IPI_H

inline void send_IPI_mask_bitmask(int mask, int vector);
inline void __send_IPI_shortcut(unsigned int shortcut, int vector);

static inline void send_IPI_mask(int mask, int vector)
{
	send_IPI_mask_bitmask(mask, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then we get an APIC send 
	 * error if we try to broadcast, thus avoid sending IPIs in this case.
	 */
	if (!(num_online_cpus() > 1))
		return;

	__send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
	return;
}

static inline void send_IPI_all(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

#endif /* __ASM_MACH_IPI_H */
