#ifndef __ASM_GENERIC_CPUMASK_CONST_REFERENCE_H
#define __ASM_GENERIC_CPUMASK_CONST_REFERENCE_H

struct cpumask_ref {
	const cpumask_t *val;
};

typedef const struct cpumask_ref cpumask_const_t;

#define mk_cpumask_const(map)		((cpumask_const_t){ &(map) })
#define cpu_isset_const(cpu, map)	cpu_isset(cpu, *(map).val)

#define cpus_and_const(dst,src1,src2)	cpus_and(dst,*(src1).val,*(src2).val)
#define cpus_or_const(dst,src1,src2)	cpus_or(dst,*(src1).val,*(src2).val)

#define cpus_equal_const(map1, map2)	cpus_equal(*(map1).val, *(map2).val)

#define cpus_copy_const(map1, map2)	bitmap_copy((map1).mask, (map2).val->mask, NR_CPUS)

#define cpus_empty_const(map)		cpus_empty(*(map).val)
#define cpus_weight_const(map)		cpus_weight(*(map).val)
#define first_cpu_const(map)		first_cpu(*(map).val)
#define next_cpu_const(cpu, map)	next_cpu(cpu, *(map).val)

/* only ever use this for things that are _never_ used on large boxen */
#define cpus_coerce_const(map)		cpus_coerce(*(map).val)
#define any_online_cpu_const(map)	any_online_cpu(*(map).val)

#endif /* __ASM_GENERIC_CPUMASK_CONST_REFERENCE_H */
