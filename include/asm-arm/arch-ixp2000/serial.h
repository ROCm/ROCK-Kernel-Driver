/*
 * include/asm-arm/arch-ixp2000/serial.h
 *
 * Serial port defn for ixp2000 based systems.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 *
 * We do not register serial ports staticly b/c there is no easy way
 * to autodetect an XScale port. Instead we register them at runtime
 * via early_serial_init().
 */

#ifndef _ARCH_SERIAL_H_
#define _ARCH_SERIAL_H_

#define BASE_BAUD (50000000/ 16)

/*
 * Currently no IXP2000 systems with > 3 serial ports.
 * If you add a system that does, just up this.
 */
#define	STD_SERIAL_PORT_DEFNS
#define	EXTRA_SERIAL_PORT_DEFNS

#endif  // __ARCH_SERIAL_H_
