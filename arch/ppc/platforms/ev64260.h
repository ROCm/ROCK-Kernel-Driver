/*
 * arch/ppc/platforms/ev64260.h
 *
 * Definitions for Marvell/Galileo EV-64260-BP Evaluation Board.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * The GT64260 has 2 PCI buses each with 1 window from the CPU bus to
 * PCI I/O space and 4 windows from the CPU bus to PCI MEM space.
 * We'll only use one PCI MEM window on each PCI bus.
 */

#ifndef __PPC_PLATFORMS_EV64260_H
#define __PPC_PLATFORMS_EV64260_H

#define	EV64260_BRIDGE_REG_BASE		0xf8000000
#define	EV64260_BRIDGE_REG_BASE_TO_TOP	0x08000000U

#define	EV64260_TODC_BASE		0xfc800000
#define	EV64260_TODC_LEN		0x00800000
#define	EV64260_TODC_END		(EV64260_TODC_BASE + \
					 EV64260_TODC_LEN - 1)

#define	EV64260_UART_BASE		0xfd000000
#define	EV64260_UART_LEN		0x00800000
#define	EV64260_UART_END		(EV64260_UART_BASE + \
					 EV64260_UART_LEN - 1)
/* Serial driver setup.  */
#define EV64260_SERIAL_0		(EV64260_UART_BASE + 0x20)
#define EV64260_SERIAL_1		EV64260_UART_BASE

#define BASE_BAUD ( 3686400 / 16 )

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE	64
#else
#define RS_TABLE_SIZE	2
#endif

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#if	!defined(CONFIG_GT64260_CONSOLE)
/* Required for bootloader's ns16550.c code */
#define STD_SERIAL_PORT_DFNS 						\
        { 0, BASE_BAUD, EV64260_SERIAL_0, 85, STD_COM_FLAGS, /* ttyS0 */\
	iomem_base: (u8 *)EV64260_SERIAL_0,				\
	iomem_reg_shift: 2,						\
	io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS
#else
#define SERIAL_PORT_DFNS
#endif

#endif /* __PPC_PLATFORMS_EV64260_H */
