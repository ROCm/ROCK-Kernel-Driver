/*
 * linux/include/asm-arm/arch-tbox/time.h
 *
 * Copyright (c) 1997, 1999 Phil Blundell.
 * Copyright (c) 2000 FutureTV Labs Ltd
 *
 * Tbox has no real-time clock -- we get millisecond ticks to update
 * our soft copy.
 */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/io.h>
#include <asm/hardware.h>

#define update_rtc()

static irqreturn_t
timer_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	/* Clear irq */
	__raw_writel(1, FPGA1CONT + 0xc); 
	__raw_writel(0, FPGA1CONT + 0xc);

	do_timer(regs);

	return IRQ_HANDLED;
}

void __init time_init(void)
{
	timer_irq.handler = timer_interrupt;
	setup_irq(IRQ_TIMER, &timer_irq);
}
