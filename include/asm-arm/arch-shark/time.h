/*
 * linux/include/asm-arm/arch-shark/time.h
 *
 * by Alexander Schulz
 *
 * derived from include/asm-arm/arch-ebsa110/time.h
 * Copyright (c) 1996,1997,1998 Russell King.
 */

#include <asm/leds.h>
#include <asm/param.h>

#define IRQ_TIMER 0
#define HZ_TIME ((1193180 + HZ/2) / HZ)

static irqreturn_t
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_leds();
	do_timer(regs);
	do_profile(regs);

	return IRQ_HANDLED;
}

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init time_init(void)
{
        unsigned long flags;

	outb(0x34, 0x43);               /* binary, mode 0, LSB/MSB, Ch 0 */
	outb(HZ_TIME & 0xff, 0x40);     /* LSB of count */
	outb(HZ_TIME >> 8, 0x40);

	xtime.tv_sec = 0;

	timer_irq.handler = timer_interrupt;
	setup_irq(IRQ_TIMER, &timer_irq);
}
