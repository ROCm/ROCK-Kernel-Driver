/*
 *  linux/include/asm-arm/arch-rpc/time.h
 *
 *  Copyright (C) 1996-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   24-Sep-1996 RMK	Created
 *   10-Oct-1996 RMK	Brought up to date with arch-sa110eval
 *   04-Dec-1997 RMK	Updated for new arch/arm/time.c
 */
extern void ioctime_init(void);

static irqreturn_t
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);
	do_set_rtc();
	do_profile(regs);

	return IRQ_HANDLED;
}

/*
 * Set up timer interrupt.
 */
void __init time_init(void)
{
	ioctime_init();

	timer_irq.handler = timer_interrupt;

	setup_irq(IRQ_TIMER, &timer_irq);
}
