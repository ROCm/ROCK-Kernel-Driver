/* linux/include/asm-arm/arch-s3c2410/hardware.h
 *
 * (c) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - hardware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  21-May-2003 BJD  Created file
 *  06-Jun-2003 BJD  Added CPU frequency settings
 *  03-Sep-2003 BJD  Linux v2.6 support
 *  12-Mar-2004 BJD  Fixed include protection, fixed type of clock vars
*/

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#ifndef __ASSEMBLY__

/* processor clock settings, in Hz */
extern unsigned long s3c2410_pclk;
extern unsigned long s3c2410_hclk;
extern unsigned long s3c2410_fclk;

#endif /* __ASSEMBLY__ */

#include <asm/sizes.h>
#include <asm/arch/map.h>

/* machine specific includes, such as the BAST */

#if defined(CONFIG_ARCH_BAST)
#include <asm/arch/bast-cpld.h>
#endif

/* currently here until moved into config (todo) */
#define CONFIG_NO_MULTIWORD_IO

#endif /* __ASM_ARCH_HARDWARE_H */
