/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999 by Ralf Baechle
 *
 * FIXME: For some of the supported machines this is dead wrong.
 */
#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */
#define CLOCK_TICK_FACTOR	20	/* Factor of both 1000000 and CLOCK_TICK_RATE */
#define FINETUNE ((((((long)LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) * \
	(1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR)) \
		<< (SHIFT_SCALE-SHIFT_HZ)) / HZ)

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
	cycles_t val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"mfc0 %0, $9\n\t"
		".set reorder"
		: "=r" (val));

	return val;
}

#endif /*  _ASM_TIMEX_H */
