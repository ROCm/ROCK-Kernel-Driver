#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_
#include <linux/compiler.h>

#define __GENERIC_PER_CPU
#ifdef CONFIG_SMP

extern unsigned long __per_cpu_offset[NR_CPUS];

/* Separate out the type, so (int[3], foo) works. */
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) name##__per_cpu

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&var##__per_cpu, __per_cpu_offset[cpu]))
#define __get_cpu_var(var) per_cpu(var, smp_processor_id())

static inline void percpu_modcopy(void *pcpudst, const void *src,
				  unsigned long size)
{
	unsigned int i;
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_possible(i))
			memcpy(pcpudst + __per_cpu_offset[i], src, size);
}
#else /* ! SMP */

#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) name##__per_cpu

#define per_cpu(var, cpu)			((void)cpu, var##__per_cpu)
#define __get_cpu_var(var)			var##__per_cpu

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) name##__per_cpu

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(var##__per_cpu)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(var##__per_cpu)

#endif /* _ASM_GENERIC_PERCPU_H_ */
