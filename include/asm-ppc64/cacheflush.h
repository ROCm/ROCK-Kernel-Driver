#ifndef _PPC64_CACHEFLUSH_H
#define _PPC64_CACHEFLUSH_H

/* Keep includes the same across arches.  */
#include <linux/mm.h>

/*
 * No cache flushing is required when address mappings are
 * changed, because the caches on PowerPCs are physically
 * addressed.
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_icache_page(vma, page)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

extern void flush_dcache_page(struct page *page);
extern void flush_icache_range(unsigned long, unsigned long);
extern void flush_icache_user_range(struct vm_area_struct *vma,
				    struct page *page, unsigned long addr,
				    int len);

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { memcpy(dst, src, len); \
     flush_icache_user_range(vma, page, vaddr, len); \
} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

extern void __flush_dcache_icache(void *page_va);

#endif /* _PPC64_CACHEFLUSH_H */
