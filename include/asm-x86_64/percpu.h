#ifndef _ASM_X8664_PERCPU_H_
#define _ASM_X8664_PERCPU_H_

#include <asm/pda.h>

#ifdef CONFIG_SMP

/* Same as the generic code except that we cache the per cpu offset
   in the pda. This gives an 3 instruction reference for per cpu data */

#include <linux/compiler.h>
#include <asm/pda.h>
#define __my_cpu_offset() read_pda(data_offset)
#define __per_cpu_offset(cpu) (cpu_pda[cpu].data_offset)

/* Separate out the type, so (int[3], foo) works. */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) name##__per_cpu

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&var##__per_cpu, __per_cpu_offset(cpu)))
#define __get_cpu_var(var) \
	(*RELOC_HIDE(&var##__per_cpu, __my_cpu_offset()))

static inline void percpu_modcopy(void *pcpudst, const void *src,
				  unsigned long size)
{
	unsigned int i;
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_possible(i))
			memcpy(pcpudst + __per_cpu_offset(i), src, size);
}

extern void setup_per_cpu_areas(void);

#else /* ! SMP */

#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) name##__per_cpu

#define per_cpu(var, cpu)			((void)cpu, var##__per_cpu)
#define __get_cpu_var(var)			var##__per_cpu

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) name##__per_cpu

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(var##__per_cpu)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(var##__per_cpu)

DECLARE_PER_CPU(struct x8664_pda, per_cpu_pda);

#endif
