/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PAGE_64_H
#define _ASM_PAGE_64_H

#include <linux/config.h>

/*
 * This handles the memory map.
 */
#ifdef CONFIG_NONCOHERENT_IO
#define PAGE_OFFSET	0x9800000000000000UL
#else
#define PAGE_OFFSET	0xa800000000000000UL
#endif


/*
 * Memory above this physical address will be considered highmem.
 * Fixme: 59 bits is a fictive number and makes assumptions about processors
 * in the distant future.  Nobody will care for a few years :-)
 */
#define HIGHMEM_START		(1UL << 59UL)

#endif /* _ASM_PAGE_64_H */
