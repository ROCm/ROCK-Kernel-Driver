/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 98, 99, 2000 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_CACHE_H
#define _ASM_CACHE_H

#include <linux/config.h>

#ifndef _LANGUAGE_ASSEMBLY
/*
 * Descriptor for a cache
 */
struct cache_desc {
	int linesz;
	int sets;
	int ways;
	int flags;	/* Details like write thru/back, coherent, etc. */
};
#endif

/*
 * Flag definitions
 */
#define MIPS_CACHE_NOT_PRESENT 0x00000001

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_R6000)
#define L1_CACHE_BYTES		16
#else
#define L1_CACHE_BYTES 		32	/* A guess */
#endif

#define SMP_CACHE_BYTES		L1_CACHE_BYTES

#endif /* _ASM_CACHE_H */
