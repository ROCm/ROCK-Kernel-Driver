/*
 * linux/arch/arm/mach-iop3xx/time-iq80310.c
 *
 * Timer functions for IQ80310 onboard timer
 *
 * Author:  Nicolas Pitre
 * Copyright:   (C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
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
#include <asm/mach/irq.h>

static void iq80310_write_timer (u_long val)
{
	volatile u_char *la0 = (volatile u_char *)IQ80310_TIMER_LA0;
	volatile u_char *la1 = (volatile u_char *)IQ80310_TIMER_LA1;
	volatile u_char *la2 = (volatile u_char *)IQ80310_TIMER_LA2;

	*la0 = val;
	*la1 = val >> 8;
	*la2 = (val >> 16) & 0x3f;
}

static u_long iq80310_read_timer (void)
{
	volatile u_char *la0 = (volatile u_char *)IQ80310_TIMER_LA0;
	volatile u_char *la1 = (volatile u_char *)IQ80310_TIMER_LA1;
	volatile u_char *la2 = (volatile u_char *)IQ80310_TIMER_LA2;
	volatile u_char *la3 = (volatile u_char *)IQ80310_TIMER_LA3;
	u_long b0, b1, b2, b3, val;

	b0 = *la0; b1 = *la1; b2 = *la2; b3 = *la3;
	b0 = (((b0 & 0x40) >> 1) | (b0 & 0x1f));
	b1 = (((b1 & 0x40) >> 1) | (b1 & 0x1f));
	b2 = (((b2 & 0x40) >> 1) | (b2 & 0x1f));
	b3 = (b3 & 0x0f);
	val = ((b0 << 0) | (b1 << 6) | (b2 << 12) | (b3 << 18));
	return val;
}

/*
 * IRQs are disabled before entering here from do_gettimeofday().
 * Note that the counter may wrap.  When it does, 'elapsed' will
 * be small, but we will have a pending interrupt.
 */
static unsigned long iq80310_gettimeoffset (void)
{
	unsigned long elapsed, usec;
	unsigned int stat1, stat2;

	stat1 = *(volatile u8 *)IQ80310_INT_STAT;
	elapsed = iq80310_read_timer();
	stat2 = *(volatile u8 *)IQ80310_INT_STAT;

	/*
	 * If an interrupt was pending before we read the timer,
	 * we've already wrapped.  Factor this into the time.
	 * If an interrupt was pending after we read the timer,
	 * it may have wrapped between checking the interrupt
	 * status and reading the timer.  Re-read the timer to
	 * be sure its value is after the wrap.
	 */
	if (stat1 & 1)
		elapsed += LATCH;
	else if (stat2 & 1)
		elapsed = LATCH + iq80310_read_timer();

	/*
	 * Now convert them to usec.
	 */
	usec = (unsigned long)(elapsed * (tick_nsec / 1000))/LATCH;

	return usec;
}


static irqreturn_t
iq80310_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile u_char *timer_en = (volatile u_char *)IQ80310_TIMER_EN;

	/* clear timer interrupt */
	*timer_en &= ~2;
	*timer_en |= 2;

	do_timer(regs);

	return IRQ_HANDLED;
}

extern unsigned long (*gettimeoffset)(void);

static struct irqaction timer_irq = {
	.name		= "timer",
	.handler	= iq80310_timer_interrupt,
};


void __init time_init(void)
{
	volatile u_char *timer_en = (volatile u_char *)IQ80310_TIMER_EN;

	gettimeoffset = iq80310_gettimeoffset;

	setup_irq(IRQ_IQ80310_TIMER, &timer_irq);

	*timer_en = 0;
	iq80310_write_timer(LATCH);
	*timer_en |= 2;
	*timer_en |= 1;
}
