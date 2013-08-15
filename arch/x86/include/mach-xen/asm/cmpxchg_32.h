#ifndef _ASM_X86_XEN_CMPXCHG_32_H
#define _ASM_X86_XEN_CMPXCHG_32_H

#include_next <asm/cmpxchg_32.h>

static inline u64 get_64bit(const volatile u64 *ptr)
{
	u64 res;
	__asm__("movl %%ebx,%%eax\n"
		"movl %%ecx,%%edx\n"
		LOCK_PREFIX "cmpxchg8b %1"
		: "=&A" (res) : "m" (*ptr));
	return res;
}

static inline u64 get_64bit_local(const volatile u64 *ptr)
{
	u64 res;
	__asm__("movl %%ebx,%%eax\n"
		"movl %%ecx,%%edx\n"
		"cmpxchg8b %1"
		: "=&A" (res) : "m" (*ptr));
	return res;
}

#endif /* _ASM_X86_XEN_CMPXCHG_32_H */
