/*
 * arch/arm/mach-iop3xx/iop321-time.c
 *
 * Timer code for IOP321 based systems
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2002-2003 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/timex.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#define IOP321_TIME_SYNC 0

static unsigned long iop321_latch;

static inline unsigned long get_elapsed(void)
{
	return iop321_latch - *IOP321_TU_TCR0;
}

static unsigned long iop321_gettimeoffset(void)
{
	unsigned long elapsed, usec;
	u32 tisr1, tisr2;

	/*
	 * If an interrupt was pending before we read the timer,
	 * we've already wrapped.  Factor this into the time.
	 * If an interrupt was pending after we read the timer,
	 * it may have wrapped between checking the interrupt
	 * status and reading the timer.  Re-read the timer to
	 * be sure its value is after the wrap.
	 */

	asm volatile("mrc p6, 0, %0, c6, c1, 0" : "=r" (tisr1));
	elapsed = get_elapsed();
	asm volatile("mrc p6, 0, %0, c6, c1, 0" : "=r" (tisr2));

	if(tisr1 & 1)
		elapsed += iop321_latch;
	else if (tisr2 & 1)
		elapsed = iop321_latch + get_elapsed();

	/*
	 * Now convert them to usec.
	 */
	usec = (unsigned long)(elapsed * (tick_nsec / 1000)) / iop321_latch;

	return usec;
}

static irqreturn_t
iop321_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 tisr;
#ifdef IOP321_TIME_SYNC
	u32 passed;
#define TM_THRESH (iop321_latch*2)
#endif

	asm volatile("mrc p6, 0, %0, c6, c1, 0" : "=r" (tisr));

	tisr |= 1;

	asm volatile("mcr p6, 0, %0, c6, c1, 0" : : "r" (tisr));

#ifdef IOP321_TIME_SYNC
	passed = 0xffffffff - *IOP321_TU_TCR1;

	do
	{
		do_timer(regs);
		if(passed < TM_THRESH)
			break;
		if(passed > iop321_latch)
			passed -= iop321_latch;
		else
			passed = 0;
	} while(1);

	asm volatile("mcr p6, 0, %0, c3, c1, 0" : : "r" (0xffffffff));
#else
	do_timer(regs);
#endif

	return IRQ_HANDLED;
}

static struct irqaction iop321_timer_irq = {
	.name		= "IOP321 Timer Tick",
	.handler	= iop321_timer_interrupt,
	.flags		= SA_INTERRUPT
};

extern int setup_arm_irq(int, struct irqaction*);

void __init iop321_init_time(void)
{
	u32 timer_ctl;

	iop321_latch = (CLOCK_TICK_RATE + HZ / 2) / HZ;
	gettimeoffset = iop321_gettimeoffset;
	setup_irq(IRQ_IOP321_TIMER0, &iop321_timer_irq);

	timer_ctl = IOP321_TMR_EN | IOP321_TMR_PRIVILEGED | IOP321_TMR_RELOAD |
			IOP321_TMR_RATIO_1_1;

	asm volatile("mcr p6, 0, %0, c4, c1, 0" : : "r" (iop321_latch));

	asm volatile("mcr p6, 0, %0, c0, c1, 0"	: : "r" (timer_ctl));

#ifdef IOP321_TIME_SYNC
        /* Setup second timer */
        /* setup counter */
        timer_ctl = IOP321_TMR_EN | IOP321_TMR_PRIVILEGED |
                        IOP321_TMR_RATIO_1_1;
        asm volatile("mcr p6, 0, %0, c3, c1, 0" : : "r" (0xffffffff));
        /* setup control */
        asm volatile("mcr p6, 0, %0, c1, c1, 0" : : "r" (timer_ctl));
#endif
}


