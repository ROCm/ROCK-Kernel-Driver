#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

#include <linux/threads.h>
#include <asm/cpumask.h>
#include <asm/bug.h>

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
		cpu = next_cpu_const(cpu, mk_cpumask_const(map));
	while (cpu < NR_CPUS && !cpu_online(cpu));
	return cpu;
}

#define for_each_cpu(cpu, map)						\
	for (cpu = first_cpu_const(mk_cpumask_const(map));		\
		cpu < NR_CPUS;						\
		cpu = next_cpu_const(cpu,mk_cpumask_const(map)))

#define for_each_online_cpu(cpu, map)					\
	for (cpu = first_cpu_const(mk_cpumask_const(map));		\
		cpu < NR_CPUS;						\
		cpu = next_online_cpu(cpu,map))

extern int __mask_snprintf_len(char *buf, unsigned int buflen,
		const unsigned long *maskp, unsigned int maskbytes);

#define cpumask_snprintf(buf, buflen, map)				\
	__mask_snprintf_len(buf, buflen, cpus_addr(map), sizeof(map))

extern int __mask_parse_len(const char __user *ubuf, unsigned int ubuflen,
	unsigned long *maskp, unsigned int maskbytes);

#define cpumask_parse(buf, buflen, map)					\
	__mask_parse_len(buf, buflen, cpus_addr(map), sizeof(map))

#endif /* __LINUX_CPUMASK_H */
