/*
 * include/asm-ppc/platforms/prpmc750_serial.h
 *
 * Motorola PrPMC750 serial support
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_PRPMC750_SERIAL_H__
#define __ASM_PRPMC750_SERIAL_H__

#include <linux/config.h>
#include <platforms/prpmc750.h>

#define RS_TABLE_SIZE  4

/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD  (PRPMC750_BASE_BAUD / 16)

#ifndef SERIAL_MAGIC_KEY
#define kernel_debugger ppc_kernel_debug
#endif

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#define SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, PRPMC750_SERIAL_0, 1, STD_COM_FLAGS, \
		iomem_base: (unsigned char *)PRPMC750_SERIAL_0, \
		iomem_reg_shift: 4, \
		io_type: SERIAL_IO_MEM } /* ttyS0 */

#endif /* __ASM_PRPMC750_SERIAL_H__ */
#endif /* __KERNEL__ */
