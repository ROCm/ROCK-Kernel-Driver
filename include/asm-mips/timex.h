/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2003 by Ralf Baechle
 */
#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#include <linux/config.h>
#include <asm/mipsregs.h>

/*
 * This is the frequency of the timer used for Linux's timer interrupt.
 * The value should be defined as accurate as possible or under certain
 * circumstances Linux timekeeping might become inaccurate or fail.
 *
 * For IP22 we cheat and pretend to have a 1MHz timer whic isn't strictly
 * true - we only use the 8259 timer to calibrate the actual interrupt
 * timer, so after all it's the master clock source of the system.
 *
 * The obscure number 1193182 is the same as used by the original i8254
 * time in legacy PC hardware; the chip unfortunately also found in a
 * bunch of MIPS systems.
 */
#ifdef CONFIG_ACER_PICA_61
#define CLOCK_TICK_RATE		1193182
#elif defined(CONFIG_MIPS_MAGNUM_4000)
#define CLOCK_TICK_RATE		1193182
#elif defined(CONFIG_OLIVETTI_M700)
#define CLOCK_TICK_RATE		1193182
#elif defined(CONFIG_SGI_IP22)
#define CLOCK_TICK_RATE		1000000
#elif defined(CONFIG_SNI_RM200_PCI)
#define CLOCK_TICK_RATE		1193182
#endif

/*
 * Standard way to access the cycle counter.
 * Currently only used on SMP for scheduling.
 *
 * Only the low 32 bits are available as a continuously counting entity.
 * But this only means we'll force a reschedule every 8 seconds or so,
 * which isn't an evil thing.
 *
 * We know that all SMP capable CPUs have cycle counters.
 */

typedef unsigned int cycles_t;
extern cycles_t cacheflush_time;

static inline cycles_t get_cycles (void)
{
	return read_c0_count();
}

#endif /*  _ASM_TIMEX_H */
