#ifndef __UM_SMP_H
#define __UM_SMP_H

extern unsigned long cpu_online_map;

#ifdef CONFIG_SMP

#include "linux/config.h"
#include "linux/bitops.h"
#include "linux/threads.h"
#include "asm/current.h"

#define smp_processor_id() (current_thread->cpu)
#define cpu_logical_map(n) (n)
#define cpu_number_map(n) (n)
#define PROC_CHANGE_PENALTY	15 /* Pick a number, any number */
extern int hard_smp_processor_id(void);
#define NO_PROC_ID -1

#define cpu_online(cpu) (cpu_online_map & (1<<(cpu)))

extern int ncpus;
#define cpu_possible(cpu) (cpu < ncpus)

extern inline unsigned int num_online_cpus(void)
{
	return(hweight32(cpu_online_map));
}

extern inline void smp_cpus_done(unsigned int maxcpus)
{
}

extern inline int any_online_cpu(unsigned int mask)
{
        if (mask & cpu_online_map)
                return __ffs(mask & cpu_online_map);

        return -1;
}
#endif

#endif
