/*
 *  linux/include/asm-arm/arch-versatile/serial.h
 *
 *  Copyright (C) 2003 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

/*
 * This assumes you have a 14.7456 MHz clock UART.
 */
#define BASE_BAUD 115200

     /* UART CLK        PORT  IRQ     FLAGS        */
#define STD_SERIAL_PORT_DEFNS \
	{ 0, BASE_BAUD, 0, 0, ASYNC_SKIP_TEST },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0, 0, ASYNC_SKIP_TEST },	/* ttyS1 */     \
	{ 0, BASE_BAUD, 0, 0, ASYNC_SKIP_TEST },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0, 0, ASYNC_SKIP_TEST },	/* ttyS3 */

#define EXTRA_SERIAL_PORT_DEFNS

#endif
