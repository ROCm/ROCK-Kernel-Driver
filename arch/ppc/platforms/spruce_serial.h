/*
 * include/asm-ppc/spruce_serial.h
 *
 * Definitions for IBM Spruce reference board support
 *
 * Authors: Matt Porter and Johnnie Peters
 *          mporter@mvista.com
 *          jpeters@mvista.com
 *
 * 2000 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_SPRUCE_SERIAL_H
#define __ASMPPC_SPRUCE_SERIAL_H

#include <linux/config.h>

/* This is where the serial ports exist */
#define SPRUCE_SERIAL_1_ADDR	0xff600300
#define SPRUCE_SERIAL_2_ADDR	0xff600400

#define RS_TABLE_SIZE  4

/* Rate for the baud clock for the onboard serial chip */
#ifndef CONFIG_SPRUCE_BAUD_33M
#define BASE_BAUD  (30000000 / 4 / 16)
#else
#define BASE_BAUD  (33000000 / 4 / 16)
#endif

#ifndef SERIAL_MAGIC_KEY
#define kernel_debugger ppc_kernel_debug
#endif

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, SPRUCE_SERIAL_1_ADDR, 3, STD_COM_FLAGS,	/* ttyS0 */ \
		iomem_base: (u8 *)SPRUCE_SERIAL_1_ADDR,			    \
		io_type: SERIAL_IO_MEM },				    \
        { 0, BASE_BAUD, SPRUCE_SERIAL_2_ADDR, 4, STD_COM_FLAGS,	/* ttyS1 */ \
		iomem_base: (u8 *)SPRUCE_SERIAL_2_ADDR,			    \
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif /* __ASMPPC_SPRUCE_SERIAL_H */
