/*
 * linux/include/asm-arm/arch-iop3xx/timex.h
 *
 * IOP3xx architecture timex specifications
 */
#include <linux/config.h>


#if defined(CONFIG_ARCH_IQ80321)

#define CLOCK_TICK_RATE 200000000

#else

#error "No IOP3xx timex information for this architecture"

#endif
