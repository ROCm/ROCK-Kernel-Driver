/*
 *  include/asm-s390/smp.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/bitops.h>

#if defined(__KERNEL__) && defined(CONFIG_SMP) && !defined(__ASSEMBLY__)

#include <asm/lowcore.h>

/*
  s390 specific smp.c headers
 */
typedef struct
{
	int        intresting;
	sigp_ccode ccode; 
	__u32      status;
	__u16      cpu;
} sigp_info;

extern volatile unsigned long cpu_online_map;
extern volatile unsigned long cpu_possible_map;

#define NO_PROC_ID		0xFF		/* No processor magic marker */

/*
 *	This magic constant controls our willingness to transfer
 *	a process across CPUs. Such a transfer incurs misses on the L1
 *	cache, and on a P6 or P5 with multiple L2 caches L2 hits. My
 *	gut feeling is this will vary by board in value. For a board
 *	with separate L2 cache it probably depends also on the RSS, and
 *	for a board with shared L2 cache it ought to decay fast as other
 *	processes are run.
 */
 
#define PROC_CHANGE_PENALTY	20		/* Schedule penalty */

#define smp_processor_id() (current_thread_info()->cpu)

#define cpu_online(cpu) (cpu_online_map & (1<<(cpu)))
#define cpu_possible(cpu) (cpu_possible_map & (1<<(cpu)))

extern inline unsigned int num_online_cpus(void)
{
#ifndef __s390x__
	return hweight32(cpu_online_map);
#else /* __s390x__ */
	return hweight64(cpu_online_map);
#endif /* __s390x__ */
}

extern inline int any_online_cpu(unsigned int mask)
{
	if (mask & cpu_online_map)
		return __ffs(mask & cpu_online_map);

	return -1;
}

extern __inline__ __u16 hard_smp_processor_id(void)
{
        __u16 cpu_address;
 
        __asm__ ("stap %0\n" : "=m" (cpu_address));
        return cpu_address;
}

#define cpu_logical_map(cpu) (cpu)

#endif
#endif
