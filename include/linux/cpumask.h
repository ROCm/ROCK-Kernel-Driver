#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/types.h>
#include <linux/bitmap.h>

#if NR_CPUS > BITS_PER_LONG && NR_CPUS != 1
#define CPU_ARRAY_SIZE		BITS_TO_LONGS(NR_CPUS)

struct cpumask
{
	unsigned long mask[CPU_ARRAY_SIZE];
};

typedef struct cpumask cpumask_t;

#else
typedef unsigned long cpumask_t;
#endif

#ifdef CONFIG_SMP
#if NR_CPUS > BITS_PER_LONG
#include <asm-generic/cpumask_array.h>
#else
#include <asm-generic/cpumask_arith.h>
#endif
#else
#include <asm-generic/cpumask_up.h>
#endif

#if NR_CPUS <= 4*BITS_PER_LONG
#include <asm-generic/cpumask_const_value.h>
#else
#include <asm-generic/cpumask_const_reference.h>
#endif


#ifdef CONFIG_SMP

extern cpumask_t cpu_online_map;

#define num_online_cpus()		cpus_weight(cpu_online_map)
#define cpu_online(cpu)			cpu_isset(cpu, cpu_online_map)
#else
#define	cpu_online_map			cpumask_of_cpu(0)
#define num_online_cpus()		1
#define cpu_online(cpu)			({ BUG_ON((cpu) != 0); 1; })
#endif

static inline int next_online_cpu(int cpu, cpumask_t map)
{
	do
		cpu = next_cpu_const(cpu, map);
	while (cpu < NR_CPUS && !cpu_online(cpu));
	return cpu;
}

#define for_each_cpu(cpu, map)						\
	for (cpu = first_cpu_const(map);				\
		cpu < NR_CPUS;						\
		cpu = next_cpu_const(cpu,map))

#define for_each_online_cpu(cpu, map)					\
	for (cpu = first_cpu_const(map);				\
		cpu < NR_CPUS;						\
		cpu = next_online_cpu(cpu,map))

#endif /* __LINUX_CPUMASK_H */
