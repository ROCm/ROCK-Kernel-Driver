#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_

#define __GENERIC_PER_CPU
#include <linux/compiler.h>

extern unsigned long __per_cpu_offset[NR_CPUS];

/* var is in discarded region: offset to particular copy we want */
#define per_cpu(var, cpu) (*RELOC_HIDE(&var, __per_cpu_offset[cpu]))
#define __get_cpu_var(var) per_cpu(var, smp_processor_id())

#endif /* _ASM_GENERIC_PERCPU_H_ */
