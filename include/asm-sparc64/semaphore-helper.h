#ifndef _SPARC64_SEMAPHORE_HELPER_H
#define _SPARC64_SEMAPHORE_HELPER_H

/*
 * SMP- and interrupt-safe semaphore helper functions, sparc64 version.
 *
 * (C) Copyright 1999 David S. Miller (davem@redhat.com)
 * (C) Copyright 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */
#define wake_one_more(__sem)      atomic_inc(&((__sem)->waking));
#define waking_non_zero(__sem)				\
({	int __ret;					\
	__asm__ __volatile__(				\
"1:	ldsw		[%1], %%g5\n\t"			\
	"brlez,pt	%%g5, 2f\n\t"			\
	" mov		0, %0\n\t"			\
	"sub		%%g5, 1, %%g7\n\t"		\
	"cas		[%1], %%g5, %%g7\n\t"		\
	"cmp		%%g5, %%g7\n\t"			\
	"bne,pn		%%icc, 1b\n\t"			\
	" mov		1, %0\n"			\
"2:"	: "=&r" (__ret)					\
	: "r" (&((__sem)->waking))			\
	: "g5", "g7", "cc", "memory");			\
	__ret;						\
})

#define waking_non_zero_interruptible(__sem, __tsk)	\
({	int __ret;					\
	__asm__ __volatile__(				\
"1:	ldsw		[%1], %%g5\n\t"			\
	"brlez,pt	%%g5, 2f\n\t"			\
	" mov		0, %0\n\t"			\
	"sub		%%g5, 1, %%g7\n\t"		\
	"cas		[%1], %%g5, %%g7\n\t"		\
	"cmp		%%g5, %%g7\n\t"			\
	"bne,pn		%%icc, 1b\n\t"			\
	" mov		1, %0\n"			\
"2:"	: "=&r" (__ret)					\
	: "r" (&((__sem)->waking))			\
	: "g5", "g7", "cc", "memory");			\
	if(__ret == 0 && signal_pending(__tsk)) {	\
		atomic_inc(&((__sem)->count));		\
		__ret = -EINTR;				\
	}						\
	__ret;						\
})

#define waking_non_zero_trylock(__sem)			\
({	int __ret;					\
	__asm__ __volatile__(				\
"1:	ldsw		[%1], %%g5\n\t"			\
	"brlez,pt	%%g5, 2f\n\t"			\
	" mov		1, %0\n\t"			\
	"sub		%%g5, 1, %%g7\n\t"		\
	"cas		[%1], %%g5, %%g7\n\t"		\
	"cmp		%%g5, %%g7\n\t"			\
	"bne,pn		%%icc, 1b\n\t"			\
	" mov		0, %0\n"			\
"2:"	: "=&r" (__ret)					\
	: "r" (&((__sem)->waking))			\
	: "g5", "g7", "cc", "memory");			\
	if(__ret == 1)					\
		atomic_inc(&((__sem)->count));		\
	__ret;						\
})

#endif /* !(_SPARC64_SEMAPHORE_HELPER_H) */
