#ifndef __ASM_GENERIC_CPUMASK_UP_H
#define __ASM_GENERIC_CPUMASK_UP_H

#define cpus_coerce(map)	(map)

#define cpu_set(cpu, map)		do { (void)(cpu); cpus_coerce(map) = 1UL; } while (0)
#define cpu_clear(cpu, map)		do { (void)(cpu); cpus_coerce(map) = 0UL; } while (0)
#define cpu_isset(cpu, map)		((void)(cpu), cpus_coerce(map) != 0UL)
#define cpu_test_and_set(cpu, map)	((void)(cpu), test_and_set_bit(0, &(map)))

#define cpus_and(dst, src1, src2)					\
	do {								\
		if (cpus_coerce(src1) && cpus_coerce(src2))		\
			cpus_coerce(dst) = 1UL;				\
		else							\
			cpus_coerce(dst) = 0UL;				\
	} while (0)

#define cpus_or(dst, src1, src2)					\
	do {								\
		if (cpus_coerce(src1) || cpus_coerce(src2))		\
			cpus_coerce(dst) = 1UL;				\
		else							\
			cpus_coerce(dst) = 0UL;				\
	} while (0)

#define cpus_clear(map)			do { cpus_coerce(map) = 0UL; } while (0)

#define cpus_complement(map)						\
	do {								\
		cpus_coerce(map) = !cpus_coerce(map);			\
	} while (0)

#define cpus_equal(map1, map2)		(cpus_coerce(map1) == cpus_coerce(map2))
#define cpus_empty(map)			(cpus_coerce(map) == 0UL)
#define cpus_weight(map)		(cpus_coerce(map) ? 1UL : 0UL)
#define cpus_shift_right(d, s, n)	do { cpus_coerce(d) = 0UL; } while (0)
#define cpus_shift_left(d, s, n)	do { cpus_coerce(d) = 0UL; } while (0)
#define first_cpu(map)			(cpus_coerce(map) ? 0 : 1)
#define next_cpu(cpu, map)		1

/* only ever use this for things that are _never_ used on large boxen */
#define cpus_promote(map)						\
	({								\
		cpumask_t __tmp__;					\
		cpus_coerce(__tmp__) = map;				\
		__tmp__;						\
	})
#define cpumask_of_cpu(cpu)		((void)(cpu), cpus_promote(1))
#define any_online_cpu(map)		(cpus_coerce(map) ? 0 : 1)

/*
 * um, these need to be usable as static initializers
 */
#define CPU_MASK_ALL	1UL
#define CPU_MASK_NONE	0UL

#endif /* __ASM_GENERIC_CPUMASK_UP_H */
