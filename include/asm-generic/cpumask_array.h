#ifndef __ASM_GENERIC_CPUMASK_ARRAY_H
#define __ASM_GENERIC_CPUMASK_ARRAY_H

/*
 * Array-based cpu bitmaps. An array of unsigned longs is used to contain
 * the bitmap, and then contained in a structure so it may be passed by
 * value.
 */

#define CPU_ARRAY_SIZE		BITS_TO_LONGS(NR_CPUS)

#define cpu_set(cpu, map)		set_bit(cpu, (map).mask)
#define cpu_clear(cpu, map)		clear_bit(cpu, (map).mask)
#define cpu_isset(cpu, map)		test_bit(cpu, (map).mask)
#define cpu_test_and_set(cpu, map)	test_and_set_bit(cpu, (map).mask)

#define cpus_and(dst,src1,src2)	bitmap_and((dst).mask,(src1).mask, (src2).mask, NR_CPUS)
#define cpus_or(dst,src1,src2)	bitmap_or((dst).mask, (src1).mask, (src2).mask, NR_CPUS)
#define cpus_clear(map)		bitmap_clear((map).mask, NR_CPUS)
#define cpus_complement(map)	bitmap_complement((map).mask, NR_CPUS)
#define cpus_equal(map1, map2)	bitmap_equal((map1).mask, (map2).mask, NR_CPUS)
#define cpus_empty(map)		bitmap_empty(map.mask, NR_CPUS)
#define cpus_weight(map)		bitmap_weight((map).mask, NR_CPUS)
#define cpus_shift_right(d, s, n)	bitmap_shift_right((d).mask, (s).mask, n, NR_CPUS)
#define cpus_shift_left(d, s, n)	bitmap_shift_left((d).mask, (s).mask, n, NR_CPUS)
#define first_cpu(map)		find_first_bit((map).mask, NR_CPUS)
#define next_cpu(cpu, map)	find_next_bit((map).mask, NR_CPUS, cpu + 1)

/* only ever use this for things that are _never_ used on large boxen */
#define cpus_coerce(map)	((map).mask[0])
#define cpus_promote(map)	({ cpumask_t __cpu_mask = CPU_MASK_NONE;\
					__cpu_mask.mask[0] = map;	\
					__cpu_mask;			\
				})
#define cpumask_of_cpu(cpu)	({ cpumask_t __cpu_mask = CPU_MASK_NONE;\
					cpu_set(cpu, __cpu_mask);	\
					__cpu_mask;			\
				})
#define any_online_cpu(map)			\
({						\
	cpumask_t __tmp__;			\
	cpus_and(__tmp__, map, cpu_online_map);	\
	find_first_bit(__tmp__.mask, NR_CPUS);	\
})


/*
 * um, these need to be usable as static initializers
 */
#define CPU_MASK_ALL	{ {[0 ... CPU_ARRAY_SIZE-1] = ~0UL} }
#define CPU_MASK_NONE	{ {[0 ... CPU_ARRAY_SIZE-1] =  0UL} }

#endif /* __ASM_GENERIC_CPUMASK_ARRAY_H */
