#ifndef __ASM_ARM_DIV64
#define __ASM_ARM_DIV64

/* We're not 64-bit, but... */
#define do_div(n,base)						\
({								\
	register int __res asm("r2") = base;			\
	register unsigned long long __n asm("r0") = n;		\
	asm("bl do_div64"					\
		: "=r" (__n), "=r" (__res)			\
		: "0" (__n), "1" (__res)			\
		: "r3", "ip", "lr", "cc");			\
	n = __n;						\
	__res;							\
})

#endif

