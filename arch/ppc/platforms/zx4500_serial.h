/*
 * include/asm-ppc/zx4500_serial.h
 * 
 * Definitions for Znyx ZX4500 board support
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * Copyright 2000, 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASMPPC_ZX4500_SERIAL_H
#define __ASMPPC_ZX4500_SERIAL_H

#include <linux/config.h>

/* Define the UART base address (only 1 UART) */
#define ZX4500_SERIAL_1			0xff880000

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  1
#endif

/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD ( 1843200 / 16 )

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, ZX4500_SERIAL_1, 17, STD_COM_FLAGS, /* ttyS0 */	\
		iomem_base: (u8 *)ZX4500_SERIAL_1,			\
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif /* __ASMPPC_ZX4500_SERIAL_H */
