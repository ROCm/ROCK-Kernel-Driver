/*
 * arch/ppc/platforms/prpmc800_serial.h
 *
 * Definitions for Motorola MCG PRPMC800 cPCI board support
 *
 * Author: Dale Farnsworth	dale.farnsworth@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_PRPMC800_SERIAL_H
#define __ASMPPC_PRPMC800_SERIAL_H

#include <linux/config.h>
#include <platforms/prpmc800.h>

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  4
#endif

/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD (PRPMC800_BASE_BAUD / 16)

#ifndef SERIAL_MAGIC_KEY
#define kernel_debugger ppc_kernel_debug
#endif

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

/* UARTS are at IRQ 16 */
#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, PRPMC800_SERIAL_1, 16, STD_COM_FLAGS, /* ttyS0 */\
		iomem_base: (unsigned char *)PRPMC800_SERIAL_1,		\
		iomem_reg_shift: 0,					\
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif /* __ASMPPC_PRPMC800_SERIAL_H */
