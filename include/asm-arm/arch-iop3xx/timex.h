/*
 * linux/include/asm-arm/arch-iop3xx/timex.h
 *
 * IOP310 architecture timex specifications
 */
#include <linux/config.h>


#ifdef CONFIG_ARCH_IQ80310

#ifndef CONFIG_XSCALE_PMU_TIMER
/* This is for the on-board timer */
#define CLOCK_TICK_RATE 33000000 /* Underlying HZ */
#else
/* This is for the underlying xs80200 PMU clock. We run the core @ 733MHz */
#define CLOCK_TICK_RATE	733000000
#endif // IQ80310

#elif defined(CONFIG_ARCH_IQ80321)

#define CLOCK_TICK_RATE 200000000

#else

#error "No IOP3xx timex information for this architecture"

#endif
