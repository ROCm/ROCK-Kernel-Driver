/*
 * linux/include/asm-sparc64/timex.h
 *
 * sparc64 architecture timex specifications
 */
#ifndef _ASMsparc64_TIMEX_H
#define _ASMsparc64_TIMEX_H

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */
#define CLOCK_TICK_FACTOR	20	/* Factor of both 1000000 and CLOCK_TICK_RATE */
#define FINETUNE ((((((long)LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) * \
	(1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR)) \
		<< (SHIFT_SCALE-SHIFT_HZ)) / HZ)

/* Getting on the cycle counter on sparc64. */
typedef unsigned long cycles_t;
extern cycles_t cacheflush_time;
#define get_cycles() \
({	cycles_t ret; \
	__asm__("rd	%%tick, %0" : "=r" (ret)); \
	ret; \
})

#endif
