/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Hewlett Packard (Paul Bame bame@puffin.external.hp.com)
 *
 * most of these calls might reasonably be moved to ../kernel -PB
 *
 * The basic principle is to construct a stack frame in C then call
 * some assembly which adopts that stack, does some rfi magic, may
 * switch wide/narrow mode, and calls the routine described by the
 * 'fn' parameter WHICH IS NOT A FUNCTION POINTER!!!!!!!!!!!!!!!!
 */
#include <linux/spinlock.h>
#include <asm/system.h>
#include <stdarg.h>
#include <asm/pgtable.h>		/* for __pa() */
#include <asm/pdc.h>

static spinlock_t pdc_lock = SPIN_LOCK_UNLOCKED;

/***************** 32-bit real-mode calls ***********/
/* The struct below is used
 * to overlay real_stack (real2.S), preparing a 32-bit call frame.
 * real32_call_asm() then uses this stack in narrow real mode
 */

struct narrow_stack {
    /* use int, not long which is 64 bits */
    unsigned int arg13;
    unsigned int arg12;
    unsigned int arg11;
    unsigned int arg10;
    unsigned int arg9;
    unsigned int arg8;
    unsigned int arg7;
    unsigned int arg6;
    unsigned int arg5;
    unsigned int arg4;
    unsigned int arg3;
    unsigned int arg2;
    unsigned int arg1;
    unsigned int arg0;
    unsigned int frame_marker[8];
    unsigned int sp;
    /* in reality, there's nearly 8k of stack after this */
};

long
real32_call(unsigned long fn, ...)
{
    unsigned long r;
    va_list args;
    unsigned long flags;
    extern struct narrow_stack real_stack;
    extern unsigned long real32_call_asm(unsigned int *,
				unsigned int *, unsigned int);
    
    va_start(args, fn);
    real_stack.arg0 = va_arg(args, unsigned int);
    real_stack.arg1 = va_arg(args, unsigned int);
    real_stack.arg2 = va_arg(args, unsigned int);
    real_stack.arg3 = va_arg(args, unsigned int);
    real_stack.arg4 = va_arg(args, unsigned int);
    real_stack.arg5 = va_arg(args, unsigned int);
    real_stack.arg6 = va_arg(args, unsigned int);
    real_stack.arg7 = va_arg(args, unsigned int);
    real_stack.arg8 = va_arg(args, unsigned int);
    real_stack.arg9 = va_arg(args, unsigned int);
    real_stack.arg10 = va_arg(args, unsigned int);
    real_stack.arg11 = va_arg(args, unsigned int);
    real_stack.arg12 = va_arg(args, unsigned int);
    real_stack.arg13 = va_arg(args, unsigned int);
    va_end(args);

    if (fn == 0) {
	    /* mem_pdc call */
	    fn = PAGE0->mem_pdc;
    }

    spin_lock_irqsave(&pdc_lock, flags);
    r = real32_call_asm(&real_stack.sp, &real_stack.arg0, fn);
    spin_unlock_irqrestore(&pdc_lock, flags);

    return r;
}

#ifdef __LP64__
/***************** 64-bit real-mode calls ***********/

struct wide_stack {
    unsigned long arg0;
    unsigned long arg1;
    unsigned long arg2;
    unsigned long arg3;
    unsigned long arg4;
    unsigned long arg5;
    unsigned long arg6;
    unsigned long arg7;
    unsigned long arg8;
    unsigned long arg9;
    unsigned long arg10;
    unsigned long arg11;
    unsigned long arg12;
    unsigned long arg13;
    unsigned long frame_marker[2];	/* rp, previous sp */
    unsigned long sp;
    /* in reality, there's nearly 8k of stack after this */
};

long
real64_call(unsigned long fn, ...)
{
    unsigned long r;
    va_list args;
    unsigned long flags;
    extern struct wide_stack real_stack;
    extern unsigned long real64_call_asm(unsigned long *,
				unsigned long *, unsigned long);
    
    va_start(args, fn);
    real_stack.arg0 = va_arg(args, unsigned long);
    real_stack.arg1 = va_arg(args, unsigned long);
    real_stack.arg2 = va_arg(args, unsigned long);
    real_stack.arg3 = va_arg(args, unsigned long);
    real_stack.arg4 = va_arg(args, unsigned long);
    real_stack.arg5 = va_arg(args, unsigned long);
    real_stack.arg6 = va_arg(args, unsigned long);
    real_stack.arg7 = va_arg(args, unsigned long);
    real_stack.arg8 = va_arg(args, unsigned long);
    real_stack.arg9 = va_arg(args, unsigned long);
    real_stack.arg10 = va_arg(args, unsigned long);
    real_stack.arg11 = va_arg(args, unsigned long);
    real_stack.arg12 = va_arg(args, unsigned long);
    real_stack.arg13 = va_arg(args, unsigned long);
    va_end(args);

    if (fn == 0) {
	    /* mem_pdc call */
	    fn = PAGE0->mem_pdc_hi;
	    fn <<= 32;
	    fn |= PAGE0->mem_pdc;
    }

    spin_lock_irqsave(&pdc_lock, flags);
    r = real64_call_asm(&real_stack.sp, &real_stack.arg0, fn);
    spin_unlock_irqrestore(&pdc_lock, flags);

    return r;
}

#endif
