/*
 * linux/include/asm-arm/arch-omap/uncompress.h
 *
 * Serial port stubs for kernel decompress status messages
 *
 * Initially based on:
 * linux-2.4.15-rmk1-dsplinux1.6/include/asm-arm/arch-omap1510/uncompress.h
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Rewritten by:
 * Author: <source@mvista.com>
 * 2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/serial_reg.h>
#include <asm/hardware.h>
#include <asm/arch/serial.h>

#define UART_OMAP_MDR1		0x08	/* mode definition register */
#define check_port(base, shift) ((base[UART_OMAP_MDR1 << shift] & 7) == 0)
#define omap_get_id() ((*(volatile unsigned int *)(0xfffed404)) >> 12) & 0xffff

static void
puts(const char *s)
{
	volatile u8 * uart = 0;
	int shift = 0;

#ifdef	CONFIG_OMAP_LL_DEBUG_UART3
	uart = (volatile u8 *)(OMAP_UART3_BASE);
#elif	CONFIG_OMAP_LL_DEBUG_UART2
	uart = (volatile u8 *)(OMAP_UART2_BASE);
#else
	uart = (volatile u8 *)(OMAP_UART1_BASE);
#endif

	/* Determine which serial port to use */
	do {
		/* MMU is not on, so cpu_is_omapXXXX() won't work here */
		unsigned int omap_id = omap_get_id();

		if (omap_id == OMAP_ID_1510 || omap_id == OMAP_ID_1610 ||
		    omap_id == OMAP_ID_1710 || omap_id == OMAP_ID_5912) {
			shift = 2;
		} else if (omap_id == OMAP_ID_730) {
			shift = 0;
		} else {
			/* Assume nothing for unknown OMAP processors.
			 * Add an entry for your OMAP type to select
			 * the default serial console here. If the
			 * serial port is enabled, we'll use it to
			 * display status messages. Else we'll be
			 * quiet.
			 */
			return;
		}
		if (check_port(uart, shift))
			break;
		/* Silent boot if no serial ports are enabled. */
		return;
	} while (0);

	/*
	 * Now, xmit each character
	 */
	while (*s) {
		while (!(uart[UART_LSR << shift] & UART_LSR_THRE))
			barrier();
		uart[UART_TX << shift] = *s;
		if (*s++ == '\n') {
			while (!(uart[UART_LSR << shift] & UART_LSR_THRE))
				barrier();
			uart[UART_TX << shift] = '\r';
		}
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
