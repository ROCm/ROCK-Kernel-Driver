/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PAGE_32_H
#define _ASM_PAGE_32_H

#include <linux/config.h>

/*
 * This handles the memory map.
 * We handle pages at KSEG0 for kernels with 32 bit address space.
 */
#define PAGE_OFFSET		0x80000000UL
#define UNCAC_BASE		0xa0000000UL

/*
 * Memory above this physical address will be considered highmem.
 */
#define HIGHMEM_START		0x20000000UL

#endif /* _ASM_PAGE_32_H */
