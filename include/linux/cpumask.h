#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

#include <linux/threads.h>
#include <asm/cpumask.h>
#include <asm/bug.h>

#ifdef CONFIG_SMP

extern cpumask_t cpu_online_map;
extern cpumask_t cpu_possible_map;

#define num_online_cpus()		cpus_weight(cpu_online_map)
#define cpu_online(cpu)			cpu_isset(cpu, cpu_online_map)
#define cpu_possible(cpu)		cpu_isset(cpu, cpu_possible_map)

#define for_each_cpu_mask(cpu, mask)					\
	for (cpu = first_cpu_const(mk_cpumask_const(mask));		\
		cpu < NR_CPUS;						\
		cpu = next_cpu_const(cpu, mk_cpumask_const(mask)))

#define for_each_cpu(cpu) for_each_cpu_mask(cpu, cpu_possible_map)
#define for_each_online_cpu(cpu) for_each_cpu_mask(cpu, cpu_online_map)
#else
#define	cpu_online_map			cpumask_of_cpu(0)
#define num_online_cpus()		1
#define cpu_online(cpu)			({ BUG_ON((cpu) != 0); 1; })
#define cpu_possible(cpu)		({ BUG_ON((cpu) != 0); 1; })

#define for_each_cpu(cpu) for (cpu = 0; cpu < 1; cpu++)
#define for_each_online_cpu(cpu) for (cpu = 0; cpu < 1; cpu++)
#endif

extern int __mask_snprintf_len(char *buf, unsigned int buflen,
		const unsigned long *maskp, unsigned int maskbytes);

#define cpumask_snprintf(buf, buflen, map)				\
	__mask_snprintf_len(buf, buflen, cpus_addr(map), sizeof(map))

extern int __mask_parse_len(const char __user *ubuf, unsigned int ubuflen,
	unsigned long *maskp, unsigned int maskbytes);

#define cpumask_parse(buf, buflen, map)					\
	__mask_parse_len(buf, buflen, cpus_addr(map), sizeof(map))

#endif /* __LINUX_CPUMASK_H */
