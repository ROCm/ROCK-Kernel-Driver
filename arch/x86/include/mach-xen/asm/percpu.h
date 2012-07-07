#ifndef _ASM_X86_XEN_PERCPU_H
#define _ASM_X86_XEN_PERCPU_H

#include_next <asm/percpu.h>

#define this_vcpu_read_1 this_cpu_read_1
#define this_vcpu_read_2 this_cpu_read_2
#define this_vcpu_read_4 this_cpu_read_4

#ifdef CONFIG_64BIT
# define this_vcpu_read_8 this_cpu_read_8
#else
# define this_vcpu_read_8(pcp) ({ \
	typeof(pcp) res__; \
	__asm__ ("movl %%ebx,%%eax\n" \
		 "movl %%ecx,%%edx\n" \
		 "cmpxchg8b " __percpu_arg(1) \
		 : "=&A" (res__) : "m" (pcp)); \
	res__; })
#endif

#define this_vcpu_read(pcp) __pcpu_size_call_return(this_vcpu_read_, pcp)

#define percpu_exchange_op(op, var, val)		\
({							\
	typedef typeof(var) pxo_T__;			\
	pxo_T__ pxo_ret__;				\
	if (0) {					\
		pxo_ret__ = (val);			\
		(void)pxo_ret__;			\
	}						\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b %0,"__percpu_arg(1)		\
		    : "=q" (pxo_ret__), "+m" (var)	\
		    : "0" ((pxo_T__)(val)));		\
		break;					\
	case 2:						\
		asm(op "w %0,"__percpu_arg(1)		\
		    : "=r" (pxo_ret__), "+m" (var)	\
		    : "0" ((pxo_T__)(val)));		\
		break;					\
	case 4:						\
		asm(op "l %0,"__percpu_arg(1)		\
		    : "=r" (pxo_ret__), "+m" (var)	\
		    : "0" ((pxo_T__)(val)));		\
		break;					\
	case 8:						\
		asm(op "q %0,"__percpu_arg(1)		\
		    : "=r" (pxo_ret__), "+m" (var)	\
		    : "0" ((pxo_T__)(val)));		\
		break;					\
	default: __bad_percpu_size();			\
	}						\
	pxo_ret__;					\
})

#if defined(CONFIG_XEN_VCPU_INFO_PLACEMENT)
# define vcpu_info_read(fld) percpu_from_op("mov", vcpu_info.fld, "m" (vcpu_info.fld))
# define vcpu_info_write(fld, val) percpu_to_op("mov", vcpu_info.fld, val)
# define vcpu_info_xchg(fld, val) percpu_exchange_op("xchg", vcpu_info.fld, val)
#elif defined(CONFIG_XEN)
# define vcpu_info_read(fld) (current_vcpu_info()->fld)
# define vcpu_info_write(fld, val) (current_vcpu_info()->fld = (val))
#endif

#endif /* _ASM_X86_XEN_PERCPU_H */
