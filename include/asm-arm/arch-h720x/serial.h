/*
 * linux/include/asm-arm/arch-h72x/serial.h
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *
 * Serial port setup for Hynix boards
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#include <asm/arch/irqs.h>

/*
 * Standard COM flags
 */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)
#define RS_TABLE_SIZE

/* Base clock is 3.6864 MHz */
#define BASE_BAUD       (115200*2)
#define EXTRA_SERIAL_PORT_DEFNS

/*
 * Board dependend defines
 */
#if defined (CONFIG_CPU_H7201)
#define BASE_BAUD_P3C 	(115200)

#define STD_SERIAL_PORT_DEFNS \
	{ \
	.baud_base	= BASE_BAUD, \
	.port		= SERIAL0_BASE, \
	.iomem_base     = (u8*)SERIAL0_BASE, \
	.io_type        = UPIO_MEM, \
	.irq		= IRQ_UART0, \
	.flags		= STD_COM_FLAGS, \
	.iomem_reg_shift = 2,\
	}, \
	{ \
	.baud_base	= BASE_BAUD, \
	.port		= SERIAL1_BASE, \
	.iomem_base     = (u8*)SERIAL1_BASE, \
	.io_type        = UPIO_MEM, \
	.irq		= IRQ_UART1, \
	.flags		= STD_COM_FLAGS, \
	.iomem_reg_shift = 2,\
	}

#elif defined (CONFIG_CPU_H7202)

#define STD_SERIAL_PORT_DEFNS \
	{ \
	.baud_base	= BASE_BAUD, \
	.port		= SERIAL0_BASE, \
	.iomem_base     = (u8*)SERIAL0_BASE, \
	.io_type        = UPIO_MEM, \
	.irq		= IRQ_UART0, \
	.flags		= STD_COM_FLAGS, \
	.iomem_reg_shift = 2,\
	}, \
	{ \
	.baud_base	= BASE_BAUD, \
	.port		= SERIAL1_BASE, \
	.iomem_base     = (u8*)SERIAL1_BASE, \
	.io_type        = UPIO_MEM, \
	.irq		= IRQ_UART1, \
	.flags		= STD_COM_FLAGS, \
	.iomem_reg_shift = 2,\
	}, \
	{ \
	.baud_base	= BASE_BAUD, \
	.port		= SERIAL2_BASE, \
	.iomem_base     = (u8*)SERIAL2_BASE, \
	.io_type        = UPIO_MEM, \
	.irq		= IRQ_UART2, \
	.flags		= STD_COM_FLAGS, \
	.iomem_reg_shift = 2,\
	}, \
	{ \
	.baud_base	= BASE_BAUD, \
	.port		= SERIAL3_BASE, \
	.iomem_base     = (u8*)SERIAL3_BASE, \
	.io_type        = UPIO_MEM, \
	.irq		= IRQ_UART3, \
	.flags		= STD_COM_FLAGS, \
	.iomem_reg_shift = 2,\
	}

#else
#error machine definition mismatch
#endif

/* __ASM_ARCH_SERIAL_H */
#endif
