#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

#include <linux/threads.h>
#include <linux/bitmap.h>
#include <asm/cpumask.h>
#include <asm/bug.h>

#ifdef CONFIG_SMP

extern cpumask_t cpu_online_map;
extern cpumask_t cpu_possible_map;
extern cpumask_t cpu_present_map;

#define num_online_cpus()		cpus_weight(cpu_online_map)
#define num_possible_cpus()		cpus_weight(cpu_possible_map)
#define num_present_cpus()		cpus_weight(cpu_present_map)

#define cpu_online(cpu)			cpu_isset(cpu, cpu_online_map)
#define cpu_possible(cpu)		cpu_isset(cpu, cpu_possible_map)
#define cpu_present(cpu)		cpu_isset(cpu, cpu_present_map)

#define for_each_cpu_mask(cpu, mask)					\
	for (cpu = first_cpu_const(mk_cpumask_const(mask));		\
		cpu < NR_CPUS;						\
		cpu = next_cpu_const(cpu, mk_cpumask_const(mask)))

#define for_each_cpu(cpu) for_each_cpu_mask(cpu, cpu_possible_map)
#define for_each_online_cpu(cpu) for_each_cpu_mask(cpu, cpu_online_map)
#define for_each_present_cpu(cpu) for_each_cpu_mask(cpu, cpu_present_map)
#else
#define	cpu_online_map			cpumask_of_cpu(0)
#define	cpu_possible_map		cpumask_of_cpu(0)
#define	cpu_present_map			cpumask_of_cpu(0)

#define num_online_cpus()		1
#define num_possible_cpus()		1
#define num_present_cpus()		1

#define cpu_online(cpu)			({ BUG_ON((cpu) != 0); 1; })
#define cpu_possible(cpu)		({ BUG_ON((cpu) != 0); 1; })
#define cpu_present(cpu)		({ BUG_ON((cpu) != 0); 1; })

#define for_each_cpu(cpu) for (cpu = 0; cpu < 1; cpu++)
#define for_each_online_cpu(cpu) for (cpu = 0; cpu < 1; cpu++)
#define for_each_present_cpu(cpu) for (cpu = 0; cpu < 1; cpu++)
#endif

#define cpumask_scnprintf(buf, buflen, map)				\
	bitmap_scnprintf(buf, buflen, cpus_addr(map), NR_CPUS)

#define cpumask_parse(buf, buflen, map)					\
	bitmap_parse(buf, buflen, cpus_addr(map), NR_CPUS)

#endif /* __LINUX_CPUMASK_H */
