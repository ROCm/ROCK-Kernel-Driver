#ifndef __ASM_GENERIC_CPUMASK_ARITH_H
#define __ASM_GENERIC_CPUMASK_ARITH_H

/*
 * Arithmetic type -based cpu bitmaps. A single unsigned long is used
 * to contain the whole cpu bitmap.
 */

#define cpu_set(cpu, map)		set_bit(cpu, &(map))
#define cpu_clear(cpu, map)		clear_bit(cpu, &(map))
#define cpu_isset(cpu, map)		test_bit(cpu, &(map))
#define cpu_test_and_set(cpu, map)	test_and_set_bit(cpu, &(map))

#define cpus_and(dst,src1,src2)		do { dst = (src1) & (src2); } while (0)
#define cpus_or(dst,src1,src2)		do { dst = (src1) | (src2); } while (0)
#define cpus_clear(map)			do { map = 0; } while (0)
#define cpus_complement(map)		do { map = ~(map); } while (0)
#define cpus_equal(map1, map2)		((map1) == (map2))
#define cpus_empty(map)			((map) == 0)

#if BITS_PER_LONG == 32
#define cpus_weight(map)		hweight32(map)
#elif BITS_PER_LONG == 64
#define cpus_weight(map)		hweight64(map)
#endif

#define cpus_shift_right(dst, src, n)	do { dst = (src) >> (n); } while (0)
#define cpus_shift_left(dst, src, n)	do { dst = (src) << (n); } while (0)

#define any_online_cpu(map)			\
({						\
	cpumask_t __tmp__;			\
	cpus_and(__tmp__, map, cpu_online_map);	\
	__tmp__ ? first_cpu(__tmp__) : NR_CPUS;	\
})

#define CPU_MASK_ALL	(~((cpumask_t)0) >> (8*sizeof(cpumask_t) - NR_CPUS))
#define CPU_MASK_NONE	((cpumask_t)0)

/* only ever use this for things that are _never_ used on large boxen */
#define cpus_coerce(map)		((unsigned long)(map))
#define cpus_promote(map)		({ map; })
#define cpumask_of_cpu(cpu)		({ ((cpumask_t)1) << (cpu); })

#define first_cpu(map)			__ffs(map)
#define next_cpu(cpu, map)		find_next_bit(&(map), NR_CPUS, cpu + 1)

#endif /* __ASM_GENERIC_CPUMASK_ARITH_H */
