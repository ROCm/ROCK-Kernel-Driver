#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <asm-generic/percpu.h>
#include <asm/lowcore.h>

/*
 * s390 uses the generic implementation for per cpu data, with the exception that
 * the offset of the cpu local data area is cached in the cpu's lowcore memory
 */
#undef __get_cpu_var
#define __get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, S390_lowcore.percpu_offset))

#endif /* __ARCH_S390_PERCPU__ */
