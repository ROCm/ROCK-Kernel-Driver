/*
 *  linux/include/asm-arm/arch-ebsa110/hardware.h
 *
 *  Copyright (C) 1996-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains the hardware definitions of the EBSA-110.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#ifndef __ASSEMBLY__

/*
 * IO definitions
 */
#define PIT_CTRL		((volatile unsigned char *)0xf200000d)
#define PIT_T2			((volatile unsigned char *)0xf2000009)
#define PIT_T1			((volatile unsigned char *)0xf2000005)
#define PIT_T0			((volatile unsigned char *)0xf2000001)

/*
 * Mapping areas
 */
#define IO_BASE			0xe0000000

/*
 * RAM definitions
 */
#define FLUSH_BASE_PHYS		0x40000000

#else	/* __ASSEMBLY__ */

#define IO_BASE			0

#endif	/* __ASSEMBLY__ */

#define IO_SIZE			0x20000000
#define IO_START		0xe0000000

#define FLUSH_BASE		0xdf000000
#define PCIO_BASE		0xf0000000

#define UNCACHEABLE_ADDR	0xf3000000

#define PARAMS_OFFSET		0x400

#endif

