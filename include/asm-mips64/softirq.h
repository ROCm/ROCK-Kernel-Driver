/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2001 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#ifndef _ASM_SOFTIRQ_H
#define _ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

extern inline void cpu_bh_disable(int cpu)
{
	local_bh_count(cpu)++;
	barrier();
}
 
extern inline void __cpu_bh_enable(int cpu)
{
	barrier();
	local_bh_count(cpu)--;
}

#define local_bh_disable()	cpu_bh_disable(smp_processor_id())
#define __local_bh_enable()	__cpu_bh_enable(smp_processor_id())
#define local_bh_enable()					\
do {								\
	int cpu;						\
								\
	barrier();						\
	cpu = smp_processor_id();				\
	if (!--local_bh_count(cpu) && softirq_pending(cpu))	\
		do_softirq();					\
} while (0)

#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

extern inline void __cpu_raise_softirq(int cpu, int nr)
{
	unsigned int *m = (unsigned int *) &softirq_pending(cpu);
	unsigned int temp;

	__asm__ __volatile__(
		"1:\tll\t%0, %1\t\t\t# __cpu_raise_softirq\n\t"
		"or\t%0, %2\n\t"
		"sc\t%0, %1\n\t"
		"beqz\t%0, 1b"
		: "=&r" (temp), "=m" (*m)
		: "ir" (1UL << nr), "m" (*m)
		: "memory");
}

#endif /* _ASM_SOFTIRQ_H */
