/*
 *  include/asm-s390/system.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 *  Derived from "include/asm-i386/system.h"
 */

#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <linux/config.h>
#include <asm/types.h>
#ifdef __KERNEL__
#include <asm/lowcore.h>
#endif
#include <linux/kernel.h>

#define switch_to(prev,next,last) do {					     \
	if (prev == next)						     \
		break;							     \
	save_fp_regs1(&prev->thread.fp_regs);				     \
	restore_fp_regs1(&next->thread.fp_regs);			     \
	resume(prev,next);						     \
} while (0)

struct task_struct;

#define nop() __asm__ __volatile__ ("nop")

#define xchg(ptr,x) \
  ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

static inline unsigned long __xchg(unsigned long x, void * ptr, int size)
{
	unsigned long addr, old;
	int shift;

        switch (size) {
	case 1:
		addr = (unsigned long) ptr;
		shift = (3 ^ (addr & 3)) << 3;
		addr ^= addr & 3;
		asm volatile(
			"    l   %0,0(%3)\n"
			"0:  lr  0,%0\n"
			"    nr  0,%2\n"
			"    or  0,%1\n"
			"    cs  %0,0,0(%3)\n"
			"    jl  0b\n"
			: "=&d" (old)
			: "d" (x << shift), "d" (~(255 << shift)), "a" (addr)
			: "memory", "cc", "0" );
		x = old >> shift;
		break;
	case 2:
		addr = (unsigned long) ptr;
		shift = (2 ^ (addr & 2)) << 3;
		addr ^= addr & 2;
		asm volatile(
			"    l   %0,0(%3)\n"
			"0:  lr  0,%0\n"
			"    nr  0,%2\n"
			"    or  0,%1\n"
			"    cs  %0,0,0(%3)\n"
			"    jl  0b\n"
			: "=&d" (old) 
			: "d" (x << shift), "d" (~(65535 << shift)), "a" (addr)
			: "memory", "cc", "0" );
		x = old >> shift;
		break;
	case 4:
		asm volatile (
			"    l   %0,0(%2)\n"
			"0:  cs  %0,%1,0(%2)\n"
			"    jl  0b\n"
			: "=&d" (old) : "d" (x), "a" (ptr)
			: "memory", "cc", "0" );
		x = old;
		break;
        }
        return x;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG 1

#define cmpxchg(ptr,o,n)\
	((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),\
					(unsigned long)(n),sizeof(*(ptr))))

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	unsigned long addr, prev, tmp;
	int shift;

        switch (size) {
	case 1:
		addr = (unsigned long) ptr;
		shift = (3 ^ (addr & 3)) << 3;
		addr ^= addr & 3;
		asm volatile(
			"    l   %0,0(%4)\n"
			"0:  nr  %0,%5\n"
                        "    lr  %1,%0\n"
			"    or  %0,%2\n"
			"    or  %1,%3\n"
			"    cs  %0,%1,0(%4)\n"
			"    jnl 1f\n"
			"    xr  %1,%0\n"
			"    nr  %1,%5\n"
			"    jnz 0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp)
			: "d" (old << shift), "d" (new << shift), "a" (ptr),
			  "d" (~(255 << shift))
			: "memory", "cc" );
		return prev >> shift;
	case 2:
		addr = (unsigned long) ptr;
		shift = (2 ^ (addr & 2)) << 3;
		addr ^= addr & 2;
		asm volatile(
			"    l   %0,0(%4)\n"
			"0:  nr  %0,%5\n"
                        "    lr  %1,%0\n"
			"    or  %0,%2\n"
			"    or  %1,%3\n"
			"    cs  %0,%1,0(%4)\n"
			"    jnl 1f\n"
			"    xr  %1,%0\n"
			"    nr  %1,%5\n"
			"    jnz 0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp)
			: "d" (old << shift), "d" (new << shift), "a" (ptr),
			  "d" (~(65535 << shift))
			: "memory", "cc" );
		return prev >> shift;
	case 4:
		asm volatile (
			"    cs  %0,%2,0(%3)\n"
			: "=&d" (prev) : "0" (old), "d" (new), "a" (ptr)
			: "memory", "cc" );
		return prev;
        }
        return old;
}

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 *
 * This is very similar to the ppc eieio/sync instruction in that is
 * does a checkpoint syncronisation & makes sure that 
 * all memory ops have completed wrt other CPU's ( see 7-15 POP  DJB ).
 */

#define eieio()  __asm__ __volatile__ ("BCR 15,0") 
# define SYNC_OTHER_CORES(x)   eieio() 
#define mb()    eieio()
#define rmb()   eieio()
#define wmb()   eieio()
#define smp_mb()       mb()
#define smp_rmb()      rmb()
#define smp_wmb()      wmb()
#define smp_mb__before_clear_bit()     smp_mb()
#define smp_mb__after_clear_bit()      smp_mb()


#define set_mb(var, value)      do { var = value; mb(); } while (0)
#define set_wmb(var, value)     do { var = value; wmb(); } while (0)

/* interrupt control.. */
#define local_irq_enable() ({ \
        __u8 __dummy; \
        __asm__ __volatile__ ( \
                "stosm 0(%1),0x03" : "=m" (__dummy) : "a" (&__dummy) ); \
        })

#define local_irq_disable() ({ \
        __u32 __flags; \
        __asm__ __volatile__ ( \
                "stnsm 0(%1),0xFC" : "=m" (__flags) : "a" (&__flags) ); \
        __flags; \
        })

#define local_save_flags(x) \
        __asm__ __volatile__("stosm 0(%1),0" : "=m" (x) : "a" (&x) )

#define local_irq_restore(x) \
        __asm__ __volatile__("ssm   0(%0)" : : "a" (&x) : "memory")

#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
        !((flags >> 24) & 3);		\
})

#define __load_psw(psw) \
	__asm__ __volatile__("lpsw 0(%0)" : : "a" (&psw) : "cc" );

#define __ctl_load(array, low, high) ({ \
	__asm__ __volatile__ ( \
		"   la    1,%0\n" \
		"   bras  2,0f\n" \
                "   lctl  0,0,0(1)\n" \
		"0: ex    %1,0(2)" \
		: : "m" (array), "a" (((low)<<4)+(high)) : "1", "2" ); \
	})

#define __ctl_store(array, low, high) ({ \
	__asm__ __volatile__ ( \
		"   la    1,%0\n" \
		"   bras  2,0f\n" \
		"   stctl 0,0,0(1)\n" \
		"0: ex    %1,0(2)" \
		: "=m" (array) : "a" (((low)<<4)+(high)): "1", "2" ); \
	})

#define __ctl_set_bit(cr, bit) ({ \
        __u8 __dummy[16]; \
        __asm__ __volatile__ ( \
                "    la    1,%0\n"       /* align to 8 byte */ \
                "    ahi   1,7\n" \
                "    srl   1,3\n" \
                "    sll   1,3\n" \
                "    bras  2,0f\n"       /* skip indirect insns */ \
                "    stctl 0,0,0(1)\n" \
                "    lctl  0,0,0(1)\n" \
                "0:  ex    %1,0(2)\n"    /* execute stctl */ \
                "    l     0,0(1)\n" \
                "    or    0,%2\n"       /* set the bit */ \
                "    st    0,0(1)\n" \
                "1:  ex    %1,4(2)"      /* execute lctl */ \
                : "=m" (__dummy) : "a" (cr*17), "a" (1<<(bit)) \
                : "cc", "0", "1", "2"); \
        })

#define __ctl_clear_bit(cr, bit) ({ \
        __u8 __dummy[16]; \
        __asm__ __volatile__ ( \
                "    la    1,%0\n"       /* align to 8 byte */ \
                "    ahi   1,7\n" \
                "    srl   1,3\n" \
                "    sll   1,3\n" \
                "    bras  2,0f\n"       /* skip indirect insns */ \
                "    stctl 0,0,0(1)\n" \
                "    lctl  0,0,0(1)\n" \
                "0:  ex    %1,0(2)\n"    /* execute stctl */ \
                "    l     0,0(1)\n" \
                "    nr    0,%2\n"       /* set the bit */ \
                "    st    0,0(1)\n" \
                "1:  ex    %1,4(2)"      /* execute lctl */ \
                : "=m" (__dummy) : "a" (cr*17), "a" (~(1<<(bit))) \
                : "cc", "0", "1", "2"); \
        })

/* For spinlocks etc */
#define local_irq_save(x)	((x) = local_irq_disable())

#ifdef CONFIG_SMP

extern void smp_ctl_set_bit(int cr, int bit);
extern void smp_ctl_clear_bit(int cr, int bit);
#define ctl_set_bit(cr, bit) smp_ctl_set_bit(cr, bit)
#define ctl_clear_bit(cr, bit) smp_ctl_clear_bit(cr, bit)

#else

#define ctl_set_bit(cr, bit) __ctl_set_bit(cr, bit)
#define ctl_clear_bit(cr, bit) __ctl_clear_bit(cr, bit)

#endif

#ifdef __KERNEL__
extern struct task_struct *resume(void *, void *);

extern int save_fp_regs1(s390_fp_regs *fpregs);
extern void save_fp_regs(s390_fp_regs *fpregs);
extern int restore_fp_regs1(s390_fp_regs *fpregs);
extern void restore_fp_regs(s390_fp_regs *fpregs);

extern void (*_machine_restart)(char *command);
extern void (*_machine_halt)(void);
extern void (*_machine_power_off)(void);

#endif

#endif



