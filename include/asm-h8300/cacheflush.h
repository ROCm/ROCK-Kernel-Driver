/*
 * (C) Copyright 2002, Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#ifndef _ASM_H8300_CACHEFLUSH_H
#define _AMS_H8300_CACHEFLUSH_H

/*
 * Cache handling functions
 * No Cache memory all dummy functions
 */

#define flush_cache_all()
#define	flush_cache_all()
#define	flush_cache_mm(mm)
#define	flush_cache_range(vma,a,b)
#define	flush_cache_page(vma,p)
#define	flush_page_to_ram(page)
#define	flush_dcache_page(page)
#define	flush_icache()
#define	flush_icache_page(vma,page)
#define	flush_icache_range(start,len)
#define	cache_push_v(vaddr,len)
#define	cache_push(paddr,len)
#define	cache_clear(paddr,len)

#define	flush_dcache_range(a,b)

#define	flush_icache_user_range(vma,page,addr,len)

#endif /* _ASM_H8300_CACHEFLUSH_H */
