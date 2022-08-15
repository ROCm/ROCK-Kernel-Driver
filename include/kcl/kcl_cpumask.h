/*SPDX-License-Identifier: GPL-2.0*/

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/cpumask.h>

#ifndef for_each_cpu_wrap

extern int _kcl_cpumask_next_wrap(int n, const struct cpumask *mask, 
				  int start, bool wrap);

static inline 
int cpumask_next_wrap(int n, const struct cpumask *mask,
		      int start, bool wrap) 
{
return _kcl_cpumask_next_wrap(n, mask, start, wrap);
}

/* Copied from include/linux/cpumask.h */
#if NR_CPUS == 1
#define for_each_cpu_wrap(cpu, mask, start)     \
        for ((cpu) = 0; (cpu) < 1; (cpu)++, (void)mask, (void)(start))
#else
/**
 * for_each_cpu_wrap - iterate over every cpu in a mask, starting at a specified location
 * @cpu: the (optionally unsigned) integer iterator
 * @mask: the cpumask pointer
 * @start: the start location
 *
 * The implementation does not assume any bit in @mask is set (including @start).
 *
 * After the loop, cpu is >= nr_cpu_ids.
 */
#define for_each_cpu_wrap(cpu, mask, start)                                     \
        for ((cpu) = cpumask_next_wrap((start)-1, (mask), (start), false);      \
             (cpu) < nr_cpumask_bits;                                           \
             (cpu) = cpumask_next_wrap((cpu), (mask), (start), true))

#endif
#endif

