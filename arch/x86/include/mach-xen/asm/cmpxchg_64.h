#ifndef _ASM_X86_XEN_CMPXCHG_64_H
#define _ASM_X86_XEN_CMPXCHG_64_H

#include_next <asm/cmpxchg_64.h>

static inline u64 get_64bit(const volatile u64 *ptr)
{
	return *ptr;
}

#define get_64bit_local get_64bit

#endif /* _ASM_X86_XEN_CMPXCHG_64_H */
