/*
 * linux/include/asm-arm/arch-tbox/time.h
 *
 * Copyright (c) 1997, 1999 Phil Blundell.
 * Copyright (c) 2000 FutureTV Labs Ltd
 *
 * Tbox has no real-time clock -- we get millisecond ticks to update
 * our soft copy.
 */

#include <asm/io.h>
#include <asm/hardware.h>

#define update_rtc()

static void timer_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	/* Clear irq */
	__raw_writel(1, FPGA1CONT + 0xc); 
	__raw_writel(0, FPGA1CONT + 0xc);

	do_timer(regs);
}

extern __inline__ void setup_timer (void)
{
	/*
	 * Default the date to 1 Jan 1970 0:0:0
	 * You will have to run a time daemon to set the
	 * clock correctly at bootup
	 */
	xtime.tv_sec = mktime(1970, 1, 1, 0, 0, 0);

	timer_irq.handler = timer_interrupt;
	setup_arm_irq(IRQ_TIMER, &timer_irq);
}
