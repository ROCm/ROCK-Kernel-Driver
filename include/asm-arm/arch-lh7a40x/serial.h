/* include/asm-arm/arch-lh7a40x/serial.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#include <asm/arch/registers.h>

#define UART_R_DATA	(0x00)
#define UART_R_FCON	(0x04)
#define UART_R_BRCON	(0x08)
#define UART_R_CON	(0x0c)
#define UART_R_STATUS	(0x10)
#define UART_R_RAWISR	(0x14)
#define UART_R_INTEN	(0x18)
#define UART_R_ISR	(0x1c)

#endif  /* _ASM_ARCH_SERIAL_H */
