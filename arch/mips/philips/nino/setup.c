/*
 *  linux/arch/mips/philips/nino/setup.c
 *
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Interrupt and exception initialization for Philips Nino.
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/sched.h>
#include <asm/addrspace.h>
#include <asm/gdb-stub.h>
#include <asm/irq.h>
#include <asm/wbflush.h>
#include <asm/tx3912.h>

extern struct rtc_ops nino_rtc_ops;

extern void nino_wait(void);
extern void setup_nino_reset_vectors(void);
extern asmlinkage void nino_handle_int(void);
extern int setup_nino_irq(int, struct irqaction *);
void (*board_time_init) (struct irqaction * irq);

#ifdef CONFIG_REMOTE_DEBUG
extern void set_debug_traps(void);
extern void breakpoint(void);
static int remote_debug = 0;
#endif

static void __init nino_irq_setup(void)
{
	unsigned int tmp;

	/* Turn all interrupts off */
	IntEnable1 = 0;
	IntEnable2 = 0;
	IntEnable3 = 0;
	IntEnable4 = 0;
	IntEnable5 = 0;
	IntEnable6 = 0;

	/* Clear all interrupts */
	IntClear1 = 0xffffffff;
	IntClear2 = 0xffffffff;
	IntClear3 = 0xffffffff;
	IntClear4 = 0xffffffff;
	IntClear5 = 0xffffffff;
	IntClear6 = 0xffffffff;

	/*
	 * Enable only the interrupts for the UART and negative
	 * edge (1-to-0) triggered multi-function I/O pins.
	 */
    	change_cp0_status(ST0_BEV, 0);
	tmp = read_32bit_cp0_register(CP0_STATUS);
    	change_cp0_status(ST0_IM, tmp | IE_IRQ2 | IE_IRQ4);

	/* Register the global interrupt handler */
	set_except_vector(0, nino_handle_int);

#ifdef CONFIG_REMOTE_DEBUG
	if (remote_debug) {
		set_debug_traps();
		breakpoint();
	}
#endif
}

static __init void nino_time_init(struct irqaction *irq)
{
	unsigned int scratch = 0;

	/*
	 * Enable periodic interrupts
	 */
	setup_nino_irq(0, irq);

	RTCperiodTimer = PER_TIMER_COUNT;
	RTCtimerControl = TIM_ENPERTIMER;
	IntEnable5 |= INT5_PERIODICINT;

	scratch = inl(TX3912_CLK_CTRL_BASE);
	scratch |= TX3912_CLK_CTRL_ENTIMERCLK;
	outl(scratch, TX3912_CLK_CTRL_BASE);

	/* Enable all interrupts */
	IntEnable6 |= INT6_GLOBALEN | INT6_PERIODICINT;
}

void __init nino_setup(void)
{
	irq_setup = nino_irq_setup;

	board_time_init = nino_time_init;

	/* Base address to use for PC type I/O accesses */
	mips_io_port_base = KSEG1ADDR(0xB0C00000);

	setup_nino_reset_vectors();

	/* Function called during process idle (cpu_idle) */
	cpu_wait = nino_wait;

#ifdef CONFIG_FB
	conswitchp = &dummy_con;
#endif

#ifdef CONFIG_REMOTE_DEBUG
	remote_debug = 1;
#endif

	rtc_ops = &nino_rtc_ops;
}
