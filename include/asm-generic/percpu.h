#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_
#include <linux/compiler.h>

#define __GENERIC_PER_CPU
#ifdef CONFIG_SMP

extern unsigned long __per_cpu_offset[NR_CPUS];

/* Separate out the type, so (int[3], foo) works. */
#ifndef MODULE
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".data.percpu"))) __typeof__(type) name##__per_cpu
#endif

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&var##__per_cpu, __per_cpu_offset[cpu]))
#define __get_cpu_var(var) per_cpu(var, smp_processor_id())

#else /* ! SMP */

/* Can't define per-cpu variables in modules.  Sorry --RR */
#ifndef MODULE
#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) name##__per_cpu
#endif

#define per_cpu(var, cpu)			var##__per_cpu
#define __get_cpu_var(var)			var##__per_cpu
#endif

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) name##__per_cpu

#endif /* _ASM_GENERIC_PERCPU_H_ */
