#ifndef _ASM_X8664_PERCPU_H_
#define _ASM_X8664_PERCPU_H_
#include <linux/compiler.h>
#include <linux/config.h>

#ifdef CONFIG_SMP

#include <asm/pda.h>

extern unsigned long __per_cpu_offset[NR_CPUS];

/* Separate out the type, so (int[3], foo) works. */
#ifndef MODULE
#define DEFINE_PER_CPU(type, name) \
    __attribute__((__section__(".percpu"))) __typeof__(type) name##__per_cpu
#endif

/* Completely hide the relocation from the compiler to avoid problems with 
   the optimizer */ 
#define __per_cpu(offset,base) \
	({ typeof(base) ptr = (void *)base; \
	   asm("addq %1,%0" : "=r" (ptr) : "r" (offset), "0" (ptr)); ptr; })

/* var is in discarded region: offset to particular copy we want */

#define per_cpu(var,cpu) (*__per_cpu(__per_cpu_offset[cpu], &var##__per_cpu)) 
#define __get_cpu_var(var) (*__per_cpu(read_pda(cpudata_offset), &var##__per_cpu))

#else /* ! SMP */

/* Can't define per-cpu variables in modules.  Sorry --RR */
#ifndef MODULE
#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) name##__per_cpu
#endif

#define per_cpu(var, cpu)			((void)cpu, var##__per_cpu)
#define __get_cpu_var(var)			var##__per_cpu
#endif

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) name##__per_cpu

extern void setup_per_cpu_areas(void);

#endif /* _ASM_X8664_PERCPU_H_ */
