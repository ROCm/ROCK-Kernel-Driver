/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * include/asm-mips/time.h
 *     header file for the new style time.c file and time services.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/*
 * Please refer to Documentation/MIPS/time.README.
 */

#ifndef _ASM_TIME_H
#define _ASM_TIME_H

#include <linux/ptrace.h>               /* for struct pt_regs */
#include <linux/linkage.h>              /* for asmlinkage */
#include <linux/rtc.h>                  /* for struct rtc_time */

/* 
 * RTC ops.  By default, they point a no-RTC functions.
 *	rtc_get_time - mktime(year, mon, day, hour, min, sec) in seconds.
 *	rtc_set_time - reverse the above translation and set time to RTC.
 */
extern unsigned long (*rtc_get_time)(void);
extern int (*rtc_set_time)(unsigned long);

/*
 * to_tm() converts system time back to (year, mon, day, hour, min, sec).
 * It is intended to help implement rtc_set_time() functions.
 * Copied from PPC implementation.
 */
extern void to_tm(unsigned long tim, struct rtc_time * tm);

/*
 * do_gettimeoffset(). By default, this func pointer points to 
 * do_null_gettimeoffset(), which leads to the same resolution as HZ.
 * Higher resolution versions are vailable, which gives ~1us resolution.
 */
extern unsigned long (*do_gettimeoffset)(void);

extern unsigned long null_gettimeoffset(void);
extern unsigned long fixed_rate_gettimeoffset(void);
extern unsigned long calibrate_div32_gettimeoffset(void);
extern unsigned long calibrate_div64_gettimeoffset(void);

/*
 * high-level timer interrupt routines.
 */
extern void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs);

/*
 * the corresponding low-level timer interrupt routine.
 */
asmlinkage void ll_timer_interrupt(int irq, struct pt_regs *regs);

/*
 * board specific routines required by time_init().
 * board_time_init is defaulted to NULL and can remains so.
 * board_timer_setup must be setup properly in machine setup routine.
 */
struct irqaction;
extern void (*board_time_init)(void);
extern void (*board_timer_setup)(struct irqaction *irq);

/*
 * mips_counter_frequency - must be set if you intend to use
 * counter as timer interrupt source or use fixed_rate_gettimeoffset.
 */
extern unsigned int mips_counter_frequency;

#endif /* _ASM_TIME_H */
