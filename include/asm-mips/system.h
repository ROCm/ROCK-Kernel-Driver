/* $Id: system.h,v 1.20 1999/12/06 23:13:21 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1994 - 1999 by Ralf Baechle
 */
#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#include <linux/config.h>
#include <asm/sgidefs.h>
#include <asm/ptrace.h>
#include <linux/kernel.h>

extern __inline__ void
__sti(void)
{
	__asm__ __volatile__(
		".set\tpush\n\t"
		".set\treorder\n\t"
		".set\tnoat\n\t"
		"mfc0\t$1,$12\n\t"
		"ori\t$1,0x1f\n\t"
		"xori\t$1,0x1e\n\t"
		"mtc0\t$1,$12\n\t"
		".set\tpop\n\t"
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
		".set\tpush\n\t"
		".set\treorder\n\t"
		".set\tnoat\n\t"
		"mfc0\t$1,$12\n\t"
		"ori\t$1,1\n\t"
		"xori\t$1,1\n\t"
		".set\tnoreorder\n\t"
		"mtc0\t$1,$12\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		".set\tpop\n\t"
		: /* no outputs */
		: /* no inputs */
		: "$1", "memory");
}

#define __save_flags(x)                  \
__asm__ __volatile__(                    \
	".set\tpush\n\t"		 \
	".set\treorder\n\t"              \
	"mfc0\t%0,$12\n\t"               \
	".set\tpop\n\t"                      \
	: "=r" (x)                       \
	: /* no inputs */                \
	: "memory")

#define __save_and_cli(x)                \
__asm__ __volatile__(                    \
	".set\tpush\n\t"		 \
	".set\treorder\n\t"              \
	".set\tnoat\n\t"                 \
	"mfc0\t%0,$12\n\t"               \
	"ori\t$1,%0,1\n\t"               \
	"xori\t$1,1\n\t"                 \
	".set\tnoreorder\n\t"		 \
	"mtc0\t$1,$12\n\t"               \
	"nop\n\t"                        \
	"nop\n\t"                        \
	"nop\n\t"                        \
	".set\tpop\n\t"                  \
	: "=r" (x)                       \
	: /* no inputs */                \
	: "$1", "memory")

extern void __inline__
__restore_flags(int flags)
{
	__asm__ __volatile__(
		".set\tpush\n\t"
		".set\treorder\n\t"
		"mfc0\t$8,$12\n\t"
		"li\t$9,0xff00\n\t"
		"and\t$8,$9\n\t"
		"nor\t$9,$0,$9\n\t"
		"and\t%0,$9\n\t"
		"or\t%0,$8\n\t"
		".set\tnoreorder\n\t"
		"mtc0\t%0,$12\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		".set\tpop\n\t"
		:
		: "r" (flags)
		: "$8", "$9", "memory");
}

/*
 * Non-SMP versions ...
 */
#define sti() __sti()
#define cli() __cli()
#define save_flags(x) __save_flags(x)
#define save_and_cli(x) __save_and_cli(x)
#define restore_flags(x) __restore_flags(x)

/* For spinlocks etc */
#define local_irq_save(x)	__save_and_cli(x);
#define local_irq_restore(x)	__restore_flags(x);
#define local_irq_disable()	__cli();
#define local_irq_enable()	__sti();

/*
 * These are probably defined overly paranoid ...
 */
#ifdef CONFIG_CPU_HAS_WB
#include <asm/wbflush.h>
#define rmb()
#define wmb() wbflush()
#define mb() wbflush()
#else
#define mb()						\
__asm__ __volatile__(					\
	"# prevent instructions being moved around\n\t"	\
	".set\tnoreorder\n\t"				\
	"# 8 nops to fool the R4400 pipeline\n\t"	\
	"nop;nop;nop;nop;nop;nop;nop;nop\n\t"		\
	".set\treorder"					\
	: /* no output */				\
	: /* no input */				\
	: "memory")
#define rmb() mb()
#define wmb() mb()
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
#define switch_to(prev,next,last) \
do { \
	(last) = resume(prev, next); \
} while(0)

/*
 * For 32 and 64 bit operands we can take advantage of ll and sc.
 * FIXME: This doesn't work for R3000 machines.
 */
extern __inline__ unsigned long xchg_u32(volatile int * m, unsigned long val)
{
#if defined(CONFIG_CPU_HAS_LLSC)
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"ll\t%0,(%1)\n"
		"1:\tmove\t$1,%2\n\t"
		"sc\t$1,(%1)\n\t"
		"beqzl\t$1,1b\n\t"
		"ll\t%0,(%1)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=r" (m), "=r" (dummy)
		: "1" (m), "2" (val)
		: "memory");

	return val;
#else
	unsigned long flags, retval;

	save_flags(flags);
	cli();
	retval = *m;
	*m = val;
	restore_flags(flags);
	return retval;

#endif /* Processor-dependent optimization */
}

/*
 * Only used for 64 bit kernel.
 */
extern __inline__ unsigned long xchg_u64(volatile long * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"lld\t%0,(%1)\n"
		"1:\tmove\t$1,%2\n\t"
		"scd\t$1,(%1)\n\t"
		"beqzl\t$1,1b\n\t"
		"lld\t%0,(%1)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (val), "=r" (m), "=r" (dummy)
		: "1" (m), "2" (val)
		: "memory");

	return val;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but I define that one to
 * be dead anyway.
 */
extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 4:
			return xchg_u32(ptr, x);
#if defined(__mips64)
		case 8:
			return xchg_u64(ptr, x);
#endif
	}
	__xchg_called_with_bad_pointer();
	return x;
}

extern void set_except_vector(int n, void *addr);

extern void __die(const char *, struct pt_regs *, const char *where,
	unsigned long line) __attribute__((noreturn));
extern void __die_if_kernel(const char *, struct pt_regs *, const char *where,
	unsigned long line);
extern int abs(int);

#define die(msg, regs)							\
	__die(msg, regs, __FILE__ ":"__FUNCTION__, __LINE__)
#define die_if_kernel(msg, regs)					\
	__die_if_kernel(msg, regs, __FILE__ ":"__FUNCTION__, __LINE__)

#endif /* _ASM_SYSTEM_H */
