/*
 * arch/arm/mach-iop3xx/iop331-time.c
 *
 * Timer code for IOP331 based systems
 *
 * Author: Dave Jiang <dave.jiang@intel.com>
 *
 * Copyright 2003 Intel Corp.
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

#undef IOP331_TIME_SYNC

static unsigned long iop331_latch;

static inline unsigned long get_elapsed(void)
{
	return iop331_latch - *IOP331_TU_TCR0;
}

static unsigned long iop331_gettimeoffset(void)
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
		elapsed += iop331_latch;
	else if (tisr2 & 1)
		elapsed = iop331_latch + get_elapsed();

	/*
	 * Now convert them to usec.
	 */
	usec = (unsigned long)(elapsed * (tick_nsec / 1000)) / iop331_latch;

	return usec;
}

static irqreturn_t
iop331_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 tisr;
#ifdef IOP331_TIME_SYNC
	u32 passed;
#define TM_THRESH (iop331_latch*2)
#endif

	asm volatile("mrc p6, 0, %0, c6, c1, 0" : "=r" (tisr));

	tisr |= 1;

	asm volatile("mcr p6, 0, %0, c6, c1, 0" : : "r" (tisr));

#ifdef IOP331_TIME_SYNC
	passed = 0xffffffff - *IOP331_TU_TCR1;

	do
	{
		do_timer(regs);
		if(passed < TM_THRESH)
			break;
		if(passed > iop331_latch)
			passed -= iop331_latch;
		else
			passed = 0;
	} while(1);

	asm volatile("mcr p6, 0, %0, c3, c1, 0" : : "r" (0xffffffff));
#else
	do_timer(regs);
#endif

	return IRQ_HANDLED;
}

static struct irqaction iop331_timer_irq = {
	.name		= "IOP331 Timer Tick",
	.handler	= iop331_timer_interrupt,
	.flags		= SA_INTERRUPT
};

extern int setup_arm_irq(int, struct irqaction*);

void __init iop331_init_time(void)
{
	u32 timer_ctl;

	iop331_latch = (CLOCK_TICK_RATE + HZ / 2) / HZ;
	gettimeoffset = iop331_gettimeoffset;
	setup_irq(IRQ_IOP331_TIMER0, &iop331_timer_irq);

	timer_ctl = IOP331_TMR_EN | IOP331_TMR_PRIVILEGED | IOP331_TMR_RELOAD |
			IOP331_TMR_RATIO_1_1;

	asm volatile("mcr p6, 0, %0, c4, c1, 0" : : "r" (iop331_latch));

	asm volatile("mcr p6, 0, %0, c0, c1, 0"	: : "r" (timer_ctl));

#ifdef IOP331_TIME_SYNC
        /* Setup second timer */
        /* setup counter */
        timer_ctl = IOP331_TMR_EN | IOP331_TMR_PRIVILEGED |
                        IOP331_TMR_RATIO_1_1;
        asm volatile("mcr p6, 0, %0, c3, c1, 0" : : "r" (0xffffffff));
        /* setup control */
        asm volatile("mcr p6, 0, %0, c1, c1, 0" : : "r" (timer_ctl));
#endif
}


