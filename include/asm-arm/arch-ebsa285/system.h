/*
 *  linux/include/asm-arm/arch-ebsa285/system.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/hardware/dec21285.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

static void arch_idle(void)
{
	unsigned long start_idle;

	start_idle = jiffies;

	do {
		if (current->need_resched || hlt_counter)
			goto slow_out;
		cpu_do_idle(IDLE_WAIT_FAST);
	} while (time_before(jiffies, start_idle + HZ/50));

	cpu_do_idle(IDLE_CLOCK_SLOW);

	while (!current->need_resched && !hlt_counter) {
		cpu_do_idle(IDLE_WAIT_SLOW);
	}

	cpu_do_idle(IDLE_CLOCK_FAST);
slow_out:
}

extern __inline__ void arch_reset(char mode)
{
	if (mode == 's') {
		/*
		 * Jump into the ROM
		 */
		cpu_reset(0x41000000);
	} else {
		if (machine_is_netwinder()) {
			/* open up the SuperIO chip
			 */
			outb(0x87, 0x370);
			outb(0x87, 0x370);

			/* aux function group 1 (logical device 7)
			 */
			outb(0x07, 0x370);
			outb(0x07, 0x371);

			/* set GP16 for WD-TIMER output
			 */
			outb(0xe6, 0x370);
			outb(0x00, 0x371);

			/* set a RED LED and toggle WD_TIMER for rebooting
			 */
			outb(0xc4, 0x338);
		} else {
			/* To reboot, we set up the 21285 watchdog and
			 * enable it.  We then wait for it to timeout.
			 */
			*CSR_TIMER4_LOAD = 0x8000;
			*CSR_TIMER4_CNTL = TIMER_CNTL_ENABLE |
					   TIMER_CNTL_AUTORELOAD |
					   TIMER_CNTL_DIV16;
			*CSR_SA110_CNTL |= 1 << 13;
		}
	}
}
