/*
 * include/asm-ppc/mvme5100_serial.h
 *
 * Definitions for Motorola MVME5100 support
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_MVME5100_SERIAL_H__
#define __ASM_MVME5100_SERIAL_H__

#include <linux/config.h>
#include <platforms/mvme5100.h>

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  4
#endif

#define BASE_BAUD ( MVME5100_BASE_BAUD / 16 )

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

/* All UART IRQ's are wire-OR'd to one MPIC IRQ */
#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, MVME5100_SERIAL_1, \
		MVME5100_SERIAL_IRQ, \
		STD_COM_FLAGS, /* ttyS0 */ \
		iomem_base: (unsigned char *)MVME5100_SERIAL_1,		\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },				\
        { 0, BASE_BAUD, MVME5100_SERIAL_2, \
		MVME5100_SERIAL_IRQ, \
		STD_COM_FLAGS, /* ttyS1 */ \
		iomem_base: (unsigned char *)MVME5100_SERIAL_2,		\
		iomem_reg_shift: 4,					\
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif /* __ASM_MVME5100_SERIAL_H__ */
#endif /* __KERNEL__ */
