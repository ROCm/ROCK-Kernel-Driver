/* include/asm-arm/arch-lh7a40x/time.h
 *
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#if HZ < 100
# define TIMER_CONTROL	TIMER_CONTROL1
# define TIMER_LOAD	TIMER_LOAD1
# define TIMER_CONSTANT	(508469/HZ)
# define TIMER_MODE	(TIMER_C_ENABLE | TIMER_C_PERIODIC | TIMER_C_508KHZ)
# define TIMER_EOI	TIMER_EOI1
# define TIMER_IRQ	IRQ_T1UI
#else
# define TIMER_CONTROL	TIMER_CONTROL3
# define TIMER_LOAD	TIMER_LOAD3
# define TIMER_CONSTANT	(3686400/HZ)
# define TIMER_MODE	(TIMER_C_ENABLE | TIMER_C_PERIODIC)
# define TIMER_EOI	TIMER_EOI3
# define TIMER_IRQ	IRQ_T3UI
#endif

static irqreturn_t
lh7a40x_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	TIMER_EOI = 0;
	do_profile (regs);
	do_leds();
	do_set_rtc();
	do_timer (regs);

	return IRQ_HANDLED;
}

void __init time_init(void)
{
				/* Stop/disable all timers */
	TIMER_CONTROL1 = 0;
	TIMER_CONTROL2 = 0;
	TIMER_CONTROL3 = 0;

	timer_irq.handler = lh7a40x_timer_interrupt;
	timer_irq.flags |= SA_INTERRUPT;
	setup_irq (TIMER_IRQ, &timer_irq);

	TIMER_LOAD = TIMER_CONSTANT;
	TIMER_CONTROL = TIMER_MODE;
}

