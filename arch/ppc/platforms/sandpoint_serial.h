/*
 * include/asm-ppc/sandpoint_serial.h
 * 
 * Definitions for Motorola SPS Sandpoint Test Platform
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_SANDPOINT_SERIAL_H
#define __ASMPPC_SANDPOINT_SERIAL_H

#include <linux/config.h>

#define SANDPOINT_SERIAL_0		0xfe0003f8
#define SANDPOINT_SERIAL_1		0xfe0002f8

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  2
#endif

/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD ( 1843200 / 16 )

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, SANDPOINT_SERIAL_0, 4, STD_COM_FLAGS, /* ttyS0 */ \
		iomem_base: (u8 *)SANDPOINT_SERIAL_0,			  \
		io_type: SERIAL_IO_MEM },				  \
        { 0, BASE_BAUD, SANDPOINT_SERIAL_1, 3, STD_COM_FLAGS, /* ttyS1 */ \
		iomem_base: (u8 *)SANDPOINT_SERIAL_1,			  \
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif /* __ASMPPC_SANDPOINT_SERIAL_H */
