/*
 * include/asm-arm/arch-ixp4xx/serial.h
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2002-2004 MontaVista Software, Inc.
 * 
 */

#ifndef _ARCH_SERIAL_H_
#define _ARCH_SERIAL_H_

/*
 * We don't hardcode our serial port information but instead
 * fill it in dynamically based on our platform in arch->map_io.
 * This allows for per-board serial ports w/o a bunch of
 * #ifdefs in this file.
 */
#define	STD_SERIAL_PORT_DEFNS
#define	EXTRA_SERIAL_PORT_DEFNS

/*
 * IXP4XX uses 15.6MHz clock for uart
 */
#define BASE_BAUD ( IXP4XX_UART_XTAL / 16 )

#endif // _ARCH_SERIAL_H_
