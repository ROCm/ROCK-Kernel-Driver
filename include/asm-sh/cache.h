/* $Id: cache.h,v 1.4 2003/05/06 23:28:50 lethal Exp $
 *
 * include/asm-sh/cache.h
 *
 * Copyright 1999 (C) Niibe Yutaka
 * Copyright 2002, 2003 (C) Paul Mundt
 */
#ifndef __ASM_SH_CACHE_H
#define __ASM_SH_CACHE_H

#include <asm/cpu/cache.h>
#include <asm/cpu/cacheflush.h>

#define SH_CACHE_VALID		1
#define SH_CACHE_UPDATED	2
#define SH_CACHE_COMBINED	4
#define SH_CACHE_ASSOC		8

#define SMP_CACHE_BYTES L1_CACHE_BYTES

#define L1_CACHE_ALIGN(x)	(((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#ifdef MODULE
#define __cacheline_aligned __attribute__((__aligned__(L1_CACHE_BYTES)))
#else
#define __cacheline_aligned					\
  __attribute__((__aligned__(L1_CACHE_BYTES),			\
		 __section__(".data.cacheline_aligned")))
#endif

#define L1_CACHE_SHIFT_MAX 5	/* largest L1 which this arch supports */

struct cache_info {
	unsigned int ways;
	unsigned int sets;
	unsigned int linesz;

	unsigned int way_shift;
	unsigned int entry_shift;
	unsigned int entry_mask;

	unsigned long flags;
};

/* Flush (write-back only) a region (smaller than a page) */
extern void __flush_wback_region(void *start, int size);
/* Flush (write-back & invalidate) a region (smaller than a page) */
extern void __flush_purge_region(void *start, int size);
/* Flush (invalidate only) a region (smaller than a page) */
extern void __flush_invalidate_region(void *start, int size);

#endif /* __ASM_SH_CACHE_H */

