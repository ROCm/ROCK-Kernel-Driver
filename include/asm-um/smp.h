#ifndef __UM_SMP_H
#define __UM_SMP_H

#ifdef CONFIG_SMP

#include "linux/config.h"
#include "linux/bitops.h"
#include "asm/current.h"
#include "linux/cpumask.h"

extern cpumask_t cpu_online_map;

#define smp_processor_id() (current->thread_info->cpu)
#define cpu_logical_map(n) (n)
#define cpu_number_map(n) (n)
#define PROC_CHANGE_PENALTY	15 /* Pick a number, any number */
extern int hard_smp_processor_id(void);
#define NO_PROC_ID -1

#define cpu_online(cpu) cpu_isset(cpu, cpu_online_map)

extern int ncpus;


extern inline void smp_cpus_done(unsigned int maxcpus)
{
}

#endif

#endif
