#ifndef __ASM_GENERIC_CPUMASK_H
#define __ASM_GENERIC_CPUMASK_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/types.h>
#include <linux/bitmap.h>

#if NR_CPUS > BITS_PER_LONG && NR_CPUS != 1
#define CPU_ARRAY_SIZE		BITS_TO_LONGS(NR_CPUS)

struct cpumask
{
	unsigned long mask[CPU_ARRAY_SIZE];
};

typedef struct cpumask cpumask_t;

#else
typedef unsigned long cpumask_t;
#endif

#ifdef CONFIG_SMP
#if NR_CPUS > BITS_PER_LONG
#include <asm-generic/cpumask_array.h>
#else
#include <asm-generic/cpumask_arith.h>
#endif
#else
#include <asm-generic/cpumask_up.h>
#endif

#if NR_CPUS <= 4*BITS_PER_LONG
#include <asm-generic/cpumask_const_value.h>
#else
#include <asm-generic/cpumask_const_reference.h>
#endif

#endif /* __ASM_GENERIC_CPUMASK_H */
