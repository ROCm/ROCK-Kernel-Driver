/*
 * linux/include/asm-arm/arch-ixp4xx/timex.h
 * 
 */

#include <asm/hardware.h>

/*
 * We use IXP425 General purpose timer for our timer needs, it runs at 66 MHz
 */
#define CLOCK_TICK_RATE (IXP4XX_PERIPHERAL_BUS_CLOCK * 1000000)

