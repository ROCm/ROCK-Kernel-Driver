/* $Id: serial.h,v 1.2 2000/01/17 23:32:47 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SERIAL_H
#define _ASM_SERIAL_H

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD (1843200 / 16)

/*
 * Note about serial ports and consoles:
 * For console output, everyone uses the IOC3 UARTA (offset 0x178)
 * connected to the master node (look in ip27_setup_console() and
 * ip27prom_console_write()).
 *
 * For serial (/dev/ttyS0 etc), we can not have hardcoded serial port
 * addresses on a partitioned machine. Since we currently use the ioc3
 * serial ports, we use dynamic serial port discovery that the serial.c
 * driver uses for pci/pnp ports (there is an entry for the SGI ioc3
 * boards in pci_boards[]). Unfortunately, UARTA's pio address is greater
 * than UARTB's, although UARTA on o200s has traditionally been known as
 * port 0. So, we just use one serial port from each ioc3 (since the
 * serial driver adds addresses to get to higher ports).
 *
 * The first one to do a register_console becomes the preferred console
 * (if there is no kernel command line console= directive). /dev/console
 * (ie 5, 1) is then "aliased" into the device number returned by the 
 * "device" routine referred to in this console structure 
 * (ip27prom_console_dev).
 *
 * Also look in ip27-pci.c:pci_fixuop_ioc3() for some comments on working
 * around ioc3 oddities in this respect.
 *
 * The IOC3 serials use a 22MHz clock rate with an additional divider by 3.
 * (IOC3_BAUD = (22000000 / (3*16)))
 */

#define RS_TABLE_SIZE	64

#define SERIAL_PORT_DFNS

#endif /* _ASM_SERIAL_H */
