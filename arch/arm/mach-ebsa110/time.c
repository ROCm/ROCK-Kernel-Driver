/*
 *  linux/include/asm-arm/arch-ebsa110/time.h
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * No real time clock on the evalulation board!
 *
 * Changelog:
 *  10-Oct-1996	RMK	Created
 *  04-Dec-1997	RMK	Updated for new arch/arm/kernel/time.c
 *  07-Aug-1998	RMK	Updated for arch/arm/kernel/leds.c
 *  28-Dec-1998	APH	Made leds code optional
 */

#include <asm/leds.h>
#include <asm/io.h>

extern unsigned long (*gettimeoffset)(void);

#define PIT_CTRL		(PIT_BASE + 0x0d)
#define PIT_T2			(PIT_BASE + 0x09)
#define PIT_T1			(PIT_BASE + 0x05)
#define PIT_T0			(PIT_BASE + 0x01)

/*
 * This is the rate at which your MCLK signal toggles (in Hz)
 * This was measured on a 10 digit frequency counter sampling
 * over 1 second.
 */
#define MCLK	47894000

/*
 * This is the rate at which the PIT timers get clocked
 */
#define CLKBY7	(MCLK / 7)

/*
 * This is the counter value.  We tick at 200Hz on this platform.
 */
#define COUNT	((CLKBY7 + (HZ / 2)) / HZ)

/*
 * Get the time offset from the system PIT.  Note that if we have missed an
 * interrupt, then the PIT counter will roll over (ie, be negative).
 * This actually works out to be convenient.
 */
static unsigned long ebsa110_gettimeoffset(void)
{
	unsigned long offset, count;

	__raw_writeb(0x40, PIT_CTRL);
	count = __raw_readb(PIT_T1);
	count |= __raw_readb(PIT_T1) << 8;

	/*
	 * If count > COUNT, make the number negative.
	 */
	if (count > COUNT)
		count |= 0xffff0000;

	offset = COUNT;
	offset -= count;

	/*
	 * `offset' is in units of timer counts.  Convert
	 * offset to units of microseconds.
	 */
	offset = offset * (1000000 / HZ) / COUNT;

	return offset;
}

static irqreturn_t
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 count;

	/* latch and read timer 1 */
	__raw_writeb(0x40, PIT_CTRL);
	count = __raw_readb(PIT_T1);
	count |= __raw_readb(PIT_T1) << 8;

	count += COUNT;

	__raw_writeb(count & 0xff, PIT_T1);
	__raw_writeb(count >> 8, PIT_T1);

	do_leds();
	do_timer(regs);
	do_profile(regs);

	return IRQ_HANDLED;
}

/*
 * Set up timer interrupt.
 */
void __init time_init(void)
{
	/*
	 * Timer 1, mode 2, LSB/MSB
	 */
	__raw_writeb(0x70, PIT_CTRL);
	__raw_writeb(COUNT & 0xff, PIT_T1);
	__raw_writeb(COUNT >> 8, PIT_T1);

	gettimeoffset = ebsa110_gettimeoffset;

	timer_irq.handler = timer_interrupt;

	setup_irq(IRQ_EBSA110_TIMER0, &timer_irq);
}


