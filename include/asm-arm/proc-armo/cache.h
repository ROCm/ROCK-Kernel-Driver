/*
 *  linux/include/asm-arm/proc-armo/cache.h
 *
 *  Copyright (C) 1999-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Cache handling for 26-bit ARM processors.
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma,start,end)	do { } while (0)
#define flush_cache_page(vma,vmaddr)		do { } while (0)

#define invalidate_dcache_range(start,end)	do { } while (0)
#define clean_dcache_range(start,end)		do { } while (0)
#define flush_dcache_range(start,end)		do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define clean_dcache_entry(_s)      do { } while (0)
#define clean_cache_entry(_start)		do { } while (0)

#define flush_icache_range(start,end)		do { } while (0)
#define flush_icache_page(vma,page)		do { } while (0)

/* DAG: ARM3 will flush cache on MEMC updates anyway? so don't bother */
#define clean_cache_area(_start,_size) do { } while (0)
