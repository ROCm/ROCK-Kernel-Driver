/* linux/include/asm-arm/arch-s3c2410/bast-cpld.h
 *
 * (c) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * BAST - CPLD control constants
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  25-May-2003 BJD  Created file, added CTRL1 registers
*/

#ifndef __ASM_ARCH_BASTCPLD_H
#define __ASM_ARCH_BASTCPLD_H

#define BAST_CPLD_CTRL1_LRCOFF	    (0x00)
#define BAST_CPLD_CTRL1_LRCADC	    (0x01)
#define BAST_CPLD_CTRL1_LRCDAC	    (0x02)
#define BAST_CPLD_CTRL1_LRCARM	    (0x03)
#define BAST_CPLD_CTRL1_LRMASK	    (0x03)

#endif /* __ASM_ARCH_BASTCPLD_H */
