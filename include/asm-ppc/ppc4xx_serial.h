/*
 * BK Id: SCCS/s.ppc4xx_serial.h 1.3 05/17/01 18:14:25 cort
 */
/*
 *    Copyright 2000 MontaVista Software Inc.
 *	PPC405GP modifications
 * 	Author: MontaVista Software, Inc.
 *         	frank_rowand@mvista.com or source@mvista.com
 * 	   	debbie_chu@mvista.com
 *
 *    Module name: ppc405_serial.h
 *
 *    Description:
 *      Macros, definitions, and data structures specific to the IBM PowerPC
 *      405 on-chip serial port devices.
 */

#ifdef __KERNEL__
#ifndef __ASMPPC_PPC4xx_SERIAL_H
#define __ASMPPC_PPC4xx_SERIAL_H

#include <linux/config.h>

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE	64
#else
#define RS_TABLE_SIZE	4
#endif

#define PPC405GP_UART0_INT	0
#define PPC405GP_UART1_INT	1

/*
** 405GP UARTs are *not* PCI devices, so need to specify a non-pci memory
** address and an io_type of SERIAL_IO_MEM.
*/

#define PPC405GP_UART0_IO_BASE	(u8 *) 0xef600300
#define PPC405GP_UART1_IO_BASE	(u8 *) 0xef600400

/*
**  - there is no config option for this
**  - this name could be more informative
**  - also see arch/ppc/kernel/ppc405_serial.c
**
** #define CONFIG_PPC405GP_INTERNAL_CLOCK
*/
#ifdef	CONFIG_PPC405GP_INTERNAL_CLOCK
#define BASE_BAUD		201600
#else
#define BASE_BAUD		691200
#endif


#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS	(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ)
#define STD_COM4_FLAGS	(ASYNC_BOOT_AUTOCONF | ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS	(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)
#define STD_COM4_FLAGS	(ASYNC_BOOT_AUTOCONF)
#endif


#ifdef CONFIG_STB03XXX

#define UART0_IO_BASE 0x40040000
#define UART0_INT     20

#define STD_SERIAL_PORT_DFNS \
    /* ttyS0 */ \
    { 0, BASE_BAUD, 0, UART0_INT, STD_COM_FLAGS, 0, 0, 0, 0, 0, 0, 0, \
    UART0_IO_BASE, 0, 0, 0, {}, {}, {}, SERIAL_IO_MEM, NULL },

#elif defined(CONFIG_UART1_DFLT_CONSOLE)

#define STD_SERIAL_PORT_DFNS \
    /* ttyS1 */ \
    { 0, BASE_BAUD, 0, PPC405GP_UART1_INT, STD_COM_FLAGS, 0, 0, 0, 0, 0, 0, 0, \
    PPC405GP_UART1_IO_BASE, 0, 0, 0, {}, {}, {}, SERIAL_IO_MEM, NULL },        \
    /* ttyS0 */ \
    { 0, BASE_BAUD, 0, PPC405GP_UART0_INT, STD_COM_FLAGS, 0, 0, 0, 0, 0, 0, 0, \
    PPC405GP_UART0_IO_BASE, 0, 0, 0, {}, {}, {}, SERIAL_IO_MEM, NULL },

#else

#define STD_SERIAL_PORT_DFNS \
    /* ttyS0 */ \
    { 0, BASE_BAUD, 0, PPC405GP_UART0_INT, STD_COM_FLAGS, 0, 0, 0, 0, 0, 0, 0, \
    PPC405GP_UART0_IO_BASE, 0, 0, 0, {}, {}, {}, SERIAL_IO_MEM, NULL },        \
    /* ttyS1 */ \
    { 0, BASE_BAUD, 0, PPC405GP_UART1_INT, STD_COM_FLAGS, 0, 0, 0, 0, 0, 0, 0, \
    PPC405GP_UART1_IO_BASE, 0, 0, 0, {}, {}, {}, SERIAL_IO_MEM, NULL },

#endif


#define SERIAL_PORT_DFNS     \
	STD_SERIAL_PORT_DFNS \
	{}



#endif	/* __ASMPPC_PPC4xx_SERIAL_H */
#endif /* __KERNEL__ */
