/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999 by Ralf Baechle
 * Modified further for R[236]000 by Paul M. Antoine, 1996
 * Copyright (C) 1999 Silicon Graphics
 */
#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#include <linux/config.h>

#include <asm/sgidefs.h>
#include <linux/kernel.h>

extern __inline__ void
__sti(void)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"mfc0\t$1,$12\n\t"
		"ori\t$1,0x1f\n\t"
		"xori\t$1,0x1e\n\t"
		"mtc0\t$1,$12\n\t"
		".set\tat\n\t"
		".set\treorder"
		: /* no outputs */
		: /* no inputs */
		: "$1", "memory");
}

/*
 * For cli() we have to insert nops to make shure that the new value
 * has actually arrived in the status register before the end of this
 * macro.
 * R4000/R4400 need three nops, the R4600 two nops and the R10000 needs
 * no nops at all.
 */
extern __inline__ void
__cli(void)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"mfc0\t$1,$12\n\t"
		"ori\t$1,1\n\t"
		"xori\t$1,1\n\t"
		"mtc0\t$1,$12\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		".set\tat\n\t"
		".set\treorder"
		: /* no outputs */
		: /* no inputs */
		: "$1", "memory");
}

#define __save_flags(x)							\
__asm__ __volatile__(							\
	".set\tnoreorder\n\t"						\
	"mfc0\t%0,$12\n\t"						\
	".set\treorder"							\
	: "=r" (x))

#define __save_and_cli(x)						\
__asm__ __volatile__(							\
	".set\tnoreorder\n\t"						\
	".set\tnoat\n\t"						\
	"mfc0\t%0,$12\n\t"						\
	"ori\t$1,%0,1\n\t"						\
	"xori\t$1,1\n\t"						\
	"mtc0\t$1,$12\n\t"						\
	"nop\n\t"							\
	"nop\n\t"							\
	"nop\n\t"							\
	".set\tat\n\t"							\
	".set\treorder"							\
	: "=r" (x)							\
	: /* no inputs */						\
	: "$1", "memory")

#define __restore_flags(flags)						\
do {									\
	unsigned long __tmp1;						\
									\
	__asm__ __volatile__(						\
		".set\tnoreorder\t\t\t# __restore_flags\n\t"		\
		".set\tnoat\n\t"					\
		"mfc0\t$1, $12\n\t"					\
		"andi\t%0, 1\n\t"					\
		"ori\t$1, 1\n\t"					\
		"xori\t$1, 1\n\t"					\
		"or\t%0, $1\n\t"					\
		"mtc0\t%0, $12\n\t"					\
		"nop\n\t"						\
		"nop\n\t"						\
		"nop\n\t"						\
		".set\tat\n\t"						\
		".set\treorder"						\
		: "=r" (__tmp1)						\
		: "0" (flags)						\
		: "$1", "memory");					\
} while(0)

#ifdef CONFIG_SMP

extern void __global_cli(void);
extern void __global_sti(void);
extern unsigned long __global_save_flags(void);
extern void __global_restore_flags(unsigned long);
#define cli() __global_cli()
#define sti() __global_sti()
#define save_flags(x) ((x)=__global_save_flags())
#define restore_flags(x) __global_restore_flags(x)
#define save_and_cli(x) do { save_flags(x); cli(); } while(0)

#else

#define cli() __cli()
#define sti() __sti()
#define save_flags(x) __save_flags(x)
#define restore_flags(x) __restore_flags(x)
#define save_and_cli(x) __save_and_cli(x)

#endif /* CONFIG_SMP */

/* For spinlocks etc */
#define local_irq_save(x)	__save_and_cli(x);
#define local_irq_restore(x)	__restore_flags(x);
#define local_irq_disable()	__cli();
#define local_irq_enable()	__sti();

/*
 * These are probably defined overly paranoid ...
 */
#define mb()						\
__asm__ __volatile__(					\
	"# prevent instructions being moved around\n\t"	\
	".set\tnoreorder\n\t"				\
	"sync\n\t"					\
	".set\treorder"					\
	: /* no output */				\
	: /* no input */				\
	: "memory")
#define rmb() mb()
#define wmb() mb()

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#endif

#define set_mb(var, value) \
do { var = value; mb(); } while (0)

#define set_wmb(var, value) \
do { var = value; wmb(); } while (0)

#if !defined (_LANGUAGE_ASSEMBLY)
/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 */
extern asmlinkage void *resume(void *last, void *next);
#endif /* !defined (_LANGUAGE_ASSEMBLY) */

#define prepare_to_switch()	do { } while(0)

extern asmlinkage void lazy_fpu_switch(void *, void *);
extern asmlinkage void init_fpu(void);
extern asmlinkage void save_fp(void *);

#ifdef CONFIG_SMP
#define SWITCH_DO_LAZY_FPU \
	if (prev->flags & PF_USEDFPU) { \
		lazy_fpu_switch(prev, 0); \
		set_cp0_status(ST0_CU1, ~ST0_CU1); \
		prev->flags &= ~PF_USEDFPU; \
	}
#else /* CONFIG_SMP */
#define SWITCH_DO_LAZY_FPU	do { } while(0)
#endif /* CONFIG_SMP */

#define switch_to(prev,next,last) \
do { \
	SWITCH_DO_LAZY_FPU; \
	(last) = resume(prev, next); \
} while(0)

extern __inline__ unsigned long xchg_u32(volatile int * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\t\t\t# xchg_u32\n\t"
		".set\tnoat\n\t"
		"ll\t%0, %3\n"
		"1:\tmove\t$1, %2\n\t"
		"sc\t$1, %1\n\t"
		"beqzl\t$1, 1b\n\t"
		" ll\t%0, %3\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=o" (*m), "=r" (dummy)
		: "o" (*m), "2" (val)
		: "memory");

	return val;
}

extern __inline__ unsigned long xchg_u64(volatile long * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\t\t\t# xchg_u64\n\t"
		".set\tnoat\n\t"
		"lld\t%0, %3\n"
		"1:\tmove\t$1, %2\n\t"
		"scd\t$1, %1\n\t"
		"beqzl\t$1, 1b\n\t"
		" lld\t%0, %3\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=o" (*m), "=r" (dummy)
		: "o" (*m), "2" (val)
		: "memory");

	return val;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))


static __inline__ unsigned long
__xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 4:
			return xchg_u32(ptr, x);
		case 8:
			return xchg_u64(ptr, x);
	}
	return x;
}

extern void set_except_vector(int n, void *addr);

#endif /* _ASM_SYSTEM_H */
