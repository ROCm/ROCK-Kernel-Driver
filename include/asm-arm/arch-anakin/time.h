/*
 *  linux/include/asm-arm/arch-anakin/time.h
 *
 *  Copyright (C) 2001 Aleph One Ltd. for Acunia N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   10-Apr-2001 TTC	Created
 */

#ifndef __ASM_ARCH_TIME_H
#define __ASM_ARCH_TIME_H

static irqreturn_t
anakin_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);
	return IRQ_HANDLED;
}

void __init time_init(void)
{
	timer_irq.handler = anakin_timer_interrupt;
	setup_irq(IRQ_TICK, &timer_irq);
}

#endif
