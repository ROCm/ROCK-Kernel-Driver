#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Page-mapping primitive inline functions
 *
 * Copyright 1995 Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <linux/highmem.h>

/*
 * The page cache can done in larger chunks than
 * one page, because it allows for more efficient
 * throughput (it can then be mapped into user
 * space in smaller chunks for same flexibility).
 *
 * Or rather, it _will_ be done in larger chunks.
 */
#define PAGE_CACHE_SHIFT	PAGE_SHIFT
#define PAGE_CACHE_SIZE		PAGE_SIZE
#define PAGE_CACHE_MASK		PAGE_MASK
#define PAGE_CACHE_ALIGN(addr)	(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK)

#define page_cache_get(x)	get_page(x)
extern void FASTCALL(page_cache_release(struct page *));

static inline struct page *page_cache_alloc(struct address_space *x)
{
	return alloc_pages(x->gfp_mask, 0);
}

/*
 * From a kernel address, get the "struct page *"
 */
#define page_cache_entry(x)	virt_to_page(x)

extern struct page * find_get_page(struct address_space *mapping,
				unsigned long index);
extern struct page * find_lock_page(struct address_space *mapping,
				unsigned long index);
extern struct page * find_trylock_page(struct address_space *mapping,
				unsigned long index);
extern struct page * find_or_create_page(struct address_space *mapping,
				unsigned long index, unsigned int gfp_mask);

extern struct page * grab_cache_page(struct address_space *mapping,
				unsigned long index);
extern struct page * grab_cache_page_nowait(struct address_space *mapping,
				unsigned long index);

extern int add_to_page_cache(struct page *page,
		struct address_space *mapping, unsigned long index);
extern int add_to_page_cache_unique(struct page *page,
		struct address_space *mapping, unsigned long index);

static inline void ___add_to_page_cache(struct page *page,
		struct address_space *mapping, unsigned long index)
{
	list_add(&page->list, &mapping->clean_pages);
	page->mapping = mapping;
	page->index = index;

	mapping->nrpages++;
	inc_page_state(nr_pagecache);
}

extern void FASTCALL(lock_page(struct page *page));
extern void FASTCALL(unlock_page(struct page *page));
extern void end_page_writeback(struct page *page);

extern void ___wait_on_page_locked(struct page *);

static inline void wait_on_page_locked(struct page *page)
{
	if (PageLocked(page))
		___wait_on_page_locked(page);
}

extern void wake_up_page(struct page *);
extern void wait_on_page_writeback(struct page *page);

typedef int filler_t(void *, struct page*);

extern struct page *read_cache_page(struct address_space *, unsigned long,
				filler_t *, void *);
#endif
