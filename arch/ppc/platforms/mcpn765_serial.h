/*
 * include/asm-ppc/mcpn765_serial.h
 *
 * Definitions for Motorola MCG MCPN765 cPCI board support
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_MCPN765_SERIAL_H
#define __ASMPPC_MCPN765_SERIAL_H

#include <linux/config.h>

/* Define the UART base addresses */
#define	MCPN765_SERIAL_1		0xfef88000
#define	MCPN765_SERIAL_2		0xfef88200
#define	MCPN765_SERIAL_3		0xfef88400
#define	MCPN765_SERIAL_4		0xfef88600

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  4
#endif

/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD	( 1843200 / 16 )
#define UART_CLK	1843200

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

/* All UART IRQ's are wire-OR'd to IRQ 17 */
#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, MCPN765_SERIAL_1, 17, STD_COM_FLAGS, /* ttyS0 */\
		iomem_base: (u8 *)MCPN765_SERIAL_1,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },				\
        { 0, BASE_BAUD, MCPN765_SERIAL_2, 17, STD_COM_FLAGS, /* ttyS1 */\
		iomem_base: (u8 *)MCPN765_SERIAL_2,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },				\
        { 0, BASE_BAUD, MCPN765_SERIAL_3, 17, STD_COM_FLAGS, /* ttyS2 */\
		iomem_base: (u8 *)MCPN765_SERIAL_3,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },				\
        { 0, BASE_BAUD, MCPN765_SERIAL_4, 17, STD_COM_FLAGS, /* ttyS3 */\
		iomem_base: (u8 *)MCPN765_SERIAL_4,			\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif /* __ASMPPC_MCPN765_SERIAL_H */
