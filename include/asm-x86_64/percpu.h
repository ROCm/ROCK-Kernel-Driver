#ifndef __ARCH_X8664_PERCPU__
#define __ARCH_X8664_PERCPU__

#include <asm/pda.h>

/* var is in discarded region: offset to particular copy we want */
#define __get_cpu_var(var)     (*RELOC_HIDE(&var, read_pda(cpudata_offset)))
#define per_cpu(var, cpu) (*RELOC_HIDE(&var, per_cpu_pda[cpu]))

void setup_per_cpu_areas(void);

#endif /* __ARCH_X8664_PERCPU__ */
