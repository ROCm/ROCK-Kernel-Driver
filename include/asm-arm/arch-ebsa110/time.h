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

#define MCLK_47_8

#if defined(MCLK_42_3)
#define PIT1_COUNT 0xecbe
#elif defined(MCLK_47_8)
/*
 * This should be 0x10B43, but that doesn't exactly fit.
 * We run the timer interrupt at 5ms, and then divide it by
 * two in software...  This is so that the user processes
 * see exactly the same model whichever ARM processor they're
 * running on.
 */
#define PIT1_COUNT 0x85A1
#define DIVISOR 2
#endif

static void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	*PIT_T1 = (PIT1_COUNT) & 0xff;
	*PIT_T1 = (PIT1_COUNT) >> 8;

#ifdef DIVISOR
	{
		static unsigned int divisor;

		if (divisor--)
			return;
		divisor = DIVISOR - 1;
	}
#endif
	do_leds();
	do_timer(regs);
	do_profile(regs);
}

/*
 * Set up timer interrupt.
 */
extern __inline__ void setup_timer(void)
{
	/*
	 * Timer 1, mode 0, 16-bit, autoreload
	 */
	*PIT_CTRL = 0x70;

	/*
	 * Refresh counter clocked at 47.8MHz/7 = 146.4ns
	 * We want centi-second interrupts
	 */
	*PIT_T1 = (PIT1_COUNT) & 0xff;
	*PIT_T1 = (PIT1_COUNT) >> 8;

	timer_irq.handler = timer_interrupt;

	setup_arm_irq(IRQ_EBSA110_TIMER0, &timer_irq);
}


