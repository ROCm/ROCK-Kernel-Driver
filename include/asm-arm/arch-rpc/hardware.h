/*
 *  linux/include/asm-arm/arch-rpc/hardware.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the hardware definitions of the RiscPC series machines.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/arch/memory.h>

/*
 * What hardware must be present
 */
#define HAS_IOMD
#define HAS_VIDC20

/* Hardware addresses of major areas.
 *  *_START is the physical address
 *  *_SIZE  is the size of the region
 *  *_BASE  is the virtual address
 */
#define RAM_SIZE		0x10000000
#define RAM_START		0x10000000

#define EASI_SIZE		0x08000000	/* EASI I/O */
#define EASI_START		0x08000000
#define EASI_BASE		0xe5000000

#define IO_START		0x03000000	/* I/O */
#define IO_SIZE			0x01000000
#define IO_BASE			0xe0000000

#define SCREEN_START		0x02000000	/* VRAM */
#define SCREEN_END		0xdfc00000
#define SCREEN_BASE		0xdf800000

#define FLUSH_BASE		0xdf000000
#define UNCACHEABLE_ADDR	0xdf010000


#ifndef __ASSEMBLY__

/*
 * for use with inb/outb
 */
#define IO_VIDC_AUDIO_BASE	0x80140000
#define IO_VIDC_BASE		0x80100000
#define IO_IOMD_BASE		0x80080000
#define IOC_BASE		0x80080000

#define IO_EC_EASI_BASE		0x81400000
#define IO_EC_IOC4_BASE		0x8009c000
#define IO_EC_IOC_BASE		0x80090000
#define IO_EC_MEMC8_BASE	0x8000ac00
#define IO_EC_MEMC_BASE		0x80000000

/*
 * IO definitions
 */
#define EXPMASK_BASE		((volatile unsigned char *)0xe0360000)
#define IOEB_BASE		((volatile unsigned char *)0xe0350050)
#define PCIO_FLOPPYDMABASE	((volatile unsigned char *)0xe002a000)
#define PCIO_BASE		0xe0010000

/*
 * Offsets from RAM base
 */
#define PARAMS_OFFSET		0x0100

/*
 * RAM definitions
 */
#define GET_MEMORY_END(p)	(PAGE_OFFSET + p->u1.s.page_size * \
						(p->u1.s.pages_in_bank[0] + \
						 p->u1.s.pages_in_bank[1] + \
						 p->u1.s.pages_in_bank[2] + \
						 p->u1.s.pages_in_bank[3]))

#define Z_PARAMS_BASE		(RAM_START + PARAMS_OFFSET)
#define FLUSH_BASE_PHYS		0x00000000	/* ROM */

#else

#define VIDC_SND_BASE		0xe0500000
#define VIDC_BASE		0xe0400000
#define IOMD_BASE		0xe0200000
#define IOC_BASE		0xe0200000
#define PCIO_FLOPPYDMABASE	0xe002a000
#define PCIO_BASE		0xe0010000

#endif

#ifndef __ASSEMBLY__
#define __EXPMASK(offset)	(((volatile unsigned char *)EXPMASK_BASE)[offset])
#else
#define __EXPMASK(offset)	offset
#endif

#define	EXPMASK_STATUS	__EXPMASK(0x00)
#define EXPMASK_ENABLE	__EXPMASK(0x04)

#endif
