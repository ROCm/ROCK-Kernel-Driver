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

/* external functions for GPIO support
 *
 * These allow various different clients to access the same GPIO
 * registers without conflicting. If your driver only owns the entire
 * GPIO register, then it is safe to ioremap/__raw_{read|write} to it.
*/

/* s3c2410_gpio_cfgpin
 *
 * set the configuration of the given pin to the value passed.
 *
 * eg:
 *    s3c2410_gpio_cfgpin(S3C2410_GPA0, S3C2410_GPA0_ADDR0);
 *    s3c2410_gpio_cfgpin(S3C2410_GPE8, S3C2410_GPE8_SDDAT1);
*/

extern void s3c2410_gpio_cfgpin(unsigned int pin, unsigned int function);

/* s3c2410_gpio_pullup
 *
 * configure the pull-up control on the given pin
 *
 * to = 1 => disable the pull-up
 *      0 => enable the pull-up
 *
 * eg;
 *
 *   s3c2410_gpio_pullup(S3C2410_GPB0, 0);
 *   s3c2410_gpio_pullup(S3C2410_GPE8, 0);
*/

extern void s3c2410_gpio_pullup(unsigned int pin, unsigned int to);

extern void s3c2410_gpio_setpin(unsigned int pin, unsigned int to);

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
