#ifndef __ASM_GENERIC_CPUMASK_CONST_VALUE_H
#define __ASM_GENERIC_CPUMASK_CONST_VALUE_H

typedef const cpumask_t cpumask_const_t;

#define mk_cpumask_const(map)		((cpumask_const_t)(map))
#define cpu_isset_const(cpu, map)	cpu_isset(cpu, map)
#define cpus_and_const(dst,src1,src2)	cpus_and(dst, src1, src2)
#define cpus_or_const(dst,src1,src2)	cpus_or(dst, src1, src2)
#define cpus_equal_const(map1, map2)	cpus_equal(map1, map2)
#define cpus_empty_const(map)		cpus_empty(map)
#define cpus_copy_const(map1, map2)	do { map1 = (cpumask_t)map2; } while (0)
#define cpus_weight_const(map)		cpus_weight(map)
#define first_cpu_const(map)		first_cpu(map)
#define next_cpu_const(cpu, map)	next_cpu(cpu, map)

/* only ever use this for things that are _never_ used on large boxen */
#define cpus_coerce_const(map)		cpus_coerce(map)
#define any_online_cpu_const(map)	any_online_cpu(map)

#endif /* __ASM_GENERIC_CPUMASK_CONST_VALUE_H */
