/* linux/include/asm-arm/arch-s3c2410/serial.h
 *
 * (c) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - serial port definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  03-Sep-2003 BJD  Created file
 *  19-Mar-2004 BJD  Removed serial port definitions, inserted elsewhere
*/

#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

/* Standard COM flags */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#define BASE_BAUD ( 1843200 / 16 )

#define STD_SERIAL_PORT_DEFNS
#define EXTRA_SERIAL_PORT_DEFNS

#endif /* __ASM_ARCH_SERIAL_H */
