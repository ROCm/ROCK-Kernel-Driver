/*
 * linux/arch/arm/mach-iop310/time-iq80310.c
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
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/smp.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
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
	b0 = (((b0 & 0x20) >> 1) | (b0 & 0x1f));
	b1 = (((b1 & 0x20) >> 1) | (b1 & 0x1f));
	b2 = (((b2 & 0x20) >> 1) | (b2 & 0x1f));
	b3 = (b3 & 0x0f);
	val = ((b0 << 0) | (b1 << 6) | (b2 << 12) | (b3 << 18));
	return val;
}

/* IRQs are disabled before entering here from do_gettimeofday() */
static unsigned long iq80310_gettimeoffset (void)
{
	unsigned long elapsed, usec;

	/* We need elapsed timer ticks since last interrupt */
	elapsed = iq80310_read_timer();

	/* Now convert them to usec */
	usec = (unsigned long)(elapsed*tick)/LATCH;

	return usec;
}


static void iq80310_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile u_char *timer_en = (volatile u_char *)IQ80310_TIMER_EN;

	/* clear timer interrupt */
	*timer_en &= ~2;
	*timer_en |= 2;

	/*
	 * AHEM..HACK
	 *
	 * Since the timer interrupt is cascaded through the CPLD and
	 * the 80312 and the demux code calls do_IRQ, the irq count is
	 * going to be atleast 2 when we get here and this will cause the
	 * kernel to increment the system tick counter even if we're
	 * idle. This causes it to look like there's always 100% system
	 * time, which is not the case.  To get around it, we just decrement
	 * the IRQ count before calling do_timer. We increment it again
	 * b/c otherwise it will go negative and than bad things happen.
	 *
	 * -DS
	 */
	irq_exit(smp_processor_id(), irq);
	do_timer(regs);
	irq_enter(smp_processor_id(), irq);
}

extern unsigned long (*gettimeoffset)(void);

static struct irqaction timer_irq = {
	name:		"timer",
	handler:	iq80310_timer_interrupt,
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

