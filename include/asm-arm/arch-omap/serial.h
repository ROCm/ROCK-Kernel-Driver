/*
 * linux/include/asm-arm/arch-omap/serial.h
 *
 * BRIEF MODULE DESCRIPTION
 * serial definitions
 *
 */

#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#define OMAP_UART1_BASE		(unsigned char *)0xfffb0000
#define OMAP_UART2_BASE		(unsigned char *)0xfffb0800
#define OMAP_UART3_BASE		(unsigned char *)0xfffb9800

#ifndef __ASSEMBLY__

#include <asm/arch/hardware.h>
#include <asm/irq.h>

#define OMAP1510_BASE_BAUD	(12000000/16)
#define OMAP1610_BASE_BAUD	(48000000/16)

/* OMAP FCR trigger  redefinitions */
#define UART_FCR_R_TRIGGER_8	0x00	/* Mask for receive trigger set at 8 */
#define UART_FCR_R_TRIGGER_16	0x40	/* Mask for receive trigger set at 16 */
#define UART_FCR_R_TRIGGER_56	0x80	/* Mask for receive trigger set at 56 */
#define UART_FCR_R_TRIGGER_60	0xC0	/* Mask for receive trigger set at 60 */

/* There is an error in the description of the transmit trigger levels of
   OMAP5910 TRM from January 2003. The transmit trigger level 56 is not
   56 but 32, the transmit trigger level 60 is not 60 but 56!
   Additionally, the descritption of these trigger levels is
   a little bit unclear. The trigger level define the number of EMPTY
   entries in the FIFO. Thus, if TRIGGER_8 is used, an interrupt is requested
   if 8 FIFO entries are empty (and 56 entries are still filled [the FIFO
   size is 64]). Or: If TRIGGER_56 is selected, everytime there are less than
   8 characters in the FIFO, an interrrupt is spawned. In other words: The
   trigger number is equal the number of characters which can be
   written without FIFO overrun */

#define UART_FCR_T_TRIGGER_8	0x00	/* Mask for transmit trigger set at 8 */
#define UART_FCR_T_TRIGGER_16	0x10	/* Mask for transmit trigger set at 16 */
#define UART_FCR_T_TRIGGER_32	0x20	/* Mask for transmit trigger set at 32 */
#define UART_FCR_T_TRIGGER_56	0x30	/* Mask for transmit trigger set at 56 */

#define STD_SERIAL_PORT_DEFNS
#define EXTRA_SERIAL_PORT_DEFNS
#define BASE_BAUD 0

#endif	/* __ASSEMBLY__ */
#endif	/* __ASM_ARCH_SERIAL_H */
