/*
 * linux/include/asm-arm/arch-omap/serial.h
 *
 * BRIEF MODULE DESCRIPTION
 * serial definitions
 *
 * NOTE: There is an error in the description of the transmit trigger levels of
 * OMAP5910 TRM from January 2003. The transmit trigger level 56 is not 56 but
 * 32, the transmit trigger level 60 is not 60 but 56!
 * Additionally, the description of these trigger levels is a little bit
 * unclear. The trigger level define the number of EMPTY entries in the FIFO.
 * Thus, if TRIGGER_8 is used, an interrupt is requested if 8 FIFO entries are
 * empty (and 56 entries are still filled [the FIFO size is 64]). Or: If
 * TRIGGER_56 is selected, everytime there are less than 8 characters in the
 * FIFO, an interrrupt is spawned. In other words: The trigger number is equal
 * the number of characters which can be written without FIFO overrun.
 */

#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#define OMAP_UART1_BASE		(unsigned char *)0xfffb0000
#define OMAP_UART2_BASE		(unsigned char *)0xfffb0800
#define OMAP_UART3_BASE		(unsigned char *)0xfffb9800
#define OMAP_MAX_NR_PORTS	3

#define is_omap_port(p)	({int __ret = 0;				\
			if (p == (char*)IO_ADDRESS(OMAP_UART1_BASE) ||	\
			    p == (char*)IO_ADDRESS(OMAP_UART2_BASE) ||	\
			    p == (char*)IO_ADDRESS(OMAP_UART3_BASE))	\
				__ret = 1;				\
			__ret;						\
			})

#ifndef __ASSEMBLY__

#include <asm/arch/hardware.h>
#include <asm/irq.h>

#define OMAP1510_BASE_BAUD	(12000000/16)
#define OMAP16XX_BASE_BAUD	(48000000/16)

#define UART_SYSC		0x15

#define STD_SERIAL_PORT_DEFNS
#define EXTRA_SERIAL_PORT_DEFNS
#define BASE_BAUD 0

#endif	/* __ASSEMBLY__ */
#endif	/* __ASM_ARCH_SERIAL_H */
