/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03, 04 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_IP32_SPACES_H
#define _ASM_MACH_IP32_SPACES_H

#include <linux/config.h>

/*
 * This handles the memory map.
 */
#define PAGE_OFFSET		0xffffffff80000000

/*
 * Memory above this physical address will be considered highmem.
 * Fixme: 59 bits is a fictive number and makes assumptions about processors
 * in the distant future.  Nobody will care for a few years :-)
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		(1UL << 59UL)
#endif

#ifdef CONFIG_DMA_NONCOHERENT
#define CAC_BASE		0x9800000000000000
#else
#define CAC_BASE		0xa800000000000000
#endif
#define IO_BASE			0x9000000000000000
#define UNCAC_BASE		0x9000000000000000
#define MAP_BASE		0xc000000000000000

#define TO_PHYS(x)		(             ((x) & TO_PHYS_MASK))
#define TO_CAC(x)		(CAC_BASE   | ((x) & TO_PHYS_MASK))
#define TO_UNCAC(x)		(UNCAC_BASE | ((x) & TO_PHYS_MASK))

#endif /* __ASM_MACH_IP32_SPACES_H */
