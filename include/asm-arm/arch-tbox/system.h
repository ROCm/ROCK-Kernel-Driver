/*
 * linux/include/asm-arm/arch-tbox/system.h
 *
 * Copyright (c) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

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

#define arch_reset(mode)	do { } while (0)

#endif
