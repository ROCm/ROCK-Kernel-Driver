/*
 * include/asm-ppc/platforms/powerpmc250_serial.h
 * 
 * Motorola PrPMC750 serial support
 *
 * Author: Troy Benjegerdes <tbenjegerdes@mvista.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASMPPC_POWERPMC250_SERIAL_H
#define __ASMPPC_POWERPMC250_SERIAL_H

#include <linux/config.h>
#include <platforms/powerpmc250.h>

#define RS_TABLE_SIZE  1

#define BASE_BAUD  (POWERPMC250_BASE_BAUD / 16)

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF)
#endif

#define SERIAL_PORT_DFNS \
{ 0, BASE_BAUD, POWERPMC250_SERIAL, POWERPMC250_SERIAL_IRQ, STD_COM_FLAGS, /* ttyS0 */\
		iomem_base: (u8 *)POWERPMC250_SERIAL,		\
		iomem_reg_shift: 0,					\
		io_type: SERIAL_IO_MEM }

#endif
#endif /* __KERNEL__ */
