/*
 *  linux/include/asm-arm/arch-arc/hardware.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the hardware definitions of the
 *  Acorn Archimedes/A5000 machines.
 *
 *  Modifications:
 *   04-04-1998	PJB/RMK	Merged arc and a5k versions
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>

#include <asm/arch/memory.h>

/*
 * What hardware must be present - these can be tested by the kernel
 * source.
 */
#define HAS_IOC
#define HAS_MEMC
#include <asm/hardware/memc.h>
#define HAS_VIDC

/* Hardware addresses of major areas.
 *  *_START is the physical address
 *  *_SIZE  is the size of the region
 *  *_BASE  is the virtual address
 */
#define IO_START		0x03000000
#define IO_SIZE			0x01000000
#define IO_BASE			0x03000000

/*
 * Screen mapping information
 */
#define SCREEN_START		0x02000000
#define SCREEN_END		0x02078000
#define SCREEN_BASE		0x02000000


#ifndef __ASSEMBLY__

/*
 * for use with inb/outb
 */
#define IO_VIDC_BASE		0x80100000
#ifdef CONFIG_ARCH_A5K
#define IOEB_VID_CTL		0x800d4012
#define IOEB_PRESENT		0x800d4014
#define IOEB_PSCLR		0x800d4016
#define IOEB_MONTYPE		0x800d401c
#endif
#define LATCHAADDR		0x80094010
#define LATCHBADDR		0x80094006
#define IOC_BASE		0x80080000

#define IO_EC_IOC4_BASE		0x8009c000
#define IO_EC_IOC_BASE		0x80090000
#define IO_EC_MEMC_BASE		0x80000000

#ifdef CONFIG_ARCH_ARC
/* A680 hardware */
#define WD1973_BASE		0x03290000
#define WD1973_LATCH		0x03350000
#define Z8530_BASE		0x032b0008
#define SCSI_BASE		0x03100000
#endif

/*
 * IO definitions
 */
#define EXPMASK_BASE		((volatile unsigned char *)0x03360000)
#define IOEB_BASE		((volatile unsigned char *)0x03350050)
#define PCIO_FLOPPYDMABASE	((volatile unsigned char *)0x0302a000)
#define PCIO_BASE		0x03010000

/*
 * RAM definitions
 */
#define GET_MEMORY_END(p)	(PAGE_OFFSET + (p->u1.s.page_size) * (p->u1.s.nr_pages))
#define PARAMS_OFFSET		0x7c000

#else

#define IOEB_BASE		0x03350050
#define IOC_BASE		0x03200000
#define PCIO_FLOPPYDMABASE	0x0302a000
#define PCIO_BASE		0x03010000

#endif

#ifndef __ASSEMBLY__
#define __EXPMASK(offset)	(((volatile unsigned char *)EXPMASK_BASE)[offset])
#else
#define __EXPMASK(offset)	offset
#endif

#define	EXPMASK_STATUS	__EXPMASK(0x00)
#define EXPMASK_ENABLE	__EXPMASK(0x04)

#endif
