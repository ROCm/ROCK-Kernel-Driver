/*
 * include/asm-arm/arch-adifcc/serial.h
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright (c) 2001 MontaVista Software, Inc.
 */
#include <linux/config.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ( 1852000 / 16 )

/* Standard COM flags */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#ifdef CONFIG_ARCH_ADI_EVB

/*
 * One serial port, int goes to FIQ, so we run in polled mode
 */
#define STD_SERIAL_PORT_DEFNS			\
       /* UART CLK      PORT        IRQ        FLAGS        */			\
	{ 0, BASE_BAUD, 0xff400000, 0,  STD_COM_FLAGS }  /* ttyS0 */

#define EXTRA_SERIAL_PORT_DEFNS

#endif

