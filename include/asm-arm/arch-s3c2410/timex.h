/* linux/include/asm-arm/arch-s3c2410/timex.h
 *
 * (c) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - time parameters
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  02-Sep-2003 BJD  Created file
 *  05-Jan-2004 BJD  Updated for Linux 2.6.0
*/

#ifndef __ASM_ARCH_TIMEX_H
#define __ASM_ARCH_TIMEX_H

#if 0
/* todo - this does not seem to work with 2.6.0 -> division by zero
 * in header files
 */
extern int s3c2410_clock_tick_rate;

#define CLOCK_TICK_RATE (s3c2410_clock_tick_rate)
#endif

/* currently, the BAST uses 24MHz as a base clock rate */
#define CLOCK_TICK_RATE 24000000


#endif /* __ASM_ARCH_TIMEX_H */
