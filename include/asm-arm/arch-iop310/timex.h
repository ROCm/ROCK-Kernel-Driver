/*
 * linux/include/asm-arm/arch-iop80310/timex.h
 *
 * IOP310 architecture timex specifications
 */



#ifdef CONFIG_ARCH_IQ80310

#ifndef CONFIG_XSCALE_PMU_TIMER
/* This is for the on-board timer */
#define CLOCK_TICK_RATE 33000000 /* Underlying HZ */
#else
/* This is for the underlying xs80200 PMU clock. We run the core @ 733MHz */
#define CLOCK_TICK_RATE	733000000
#endif

#else

#error "No IOP310 timex information for this architecture"

#endif
