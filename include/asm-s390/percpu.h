#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

#include <asm-generic/percpu.h>
#include <asm/lowcore.h>

/*
 * For builtin kernel code s390 uses the generic implementation for
 * per cpu data, with the exception that the offset of the cpu local
 * data area is cached in the cpu's lowcore memory
 * For 64 bit module code s390 forces the use of a GOT slot for the
 * address of the per cpu variable. This is needed because the module
 * may be more than 4G above the per cpu area.
 */
#if defined(__s390x__) && defined(MODULE)
#define __get_got_cpu_var(var,offset) \
  (*({ unsigned long *__ptr; \
       asm ( "larl %0,per_cpu__"#var"@GOTENT" : "=a" (__ptr) ); \
       ((typeof(&per_cpu__##var))((*__ptr) + offset)); \
    }))
#undef __get_cpu_var
#define __get_cpu_var(var) __get_got_cpu_var(var,S390_lowcore.percpu_offset)
#undef per_cpu
#define per_cpu(var,cpu) __get_got_cpu_var(var,__per_cpu_offset[cpu])
#else
#undef __get_cpu_var
#define __get_cpu_var(var) (*RELOC_HIDE(&per_cpu__##var, S390_lowcore.percpu_offset))
#endif

#endif /* __ARCH_S390_PERCPU__ */
