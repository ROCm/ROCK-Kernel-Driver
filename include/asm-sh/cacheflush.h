#ifndef __ASM_SH_CACHEFLUSH_H
#define __ASM_SH_CACHEFLUSH_H

#include <asm/cpu/cacheflush.h>

/* Flush (write-back only) a region (smaller than a page) */
extern void __flush_wback_region(void *start, int size);
/* Flush (write-back & invalidate) a region (smaller than a page) */
extern void __flush_purge_region(void *start, int size);
/* Flush (invalidate only) a region (smaller than a page) */
extern void __flush_invalidate_region(void *start, int size);

#endif /* __ASM_SH_CACHEFLUSH_H */
