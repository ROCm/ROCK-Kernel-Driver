#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Copyright 1995 Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>
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

#define page_cache_get(page)		get_page(page)
#define page_cache_release(page)	put_page(page)
void release_pages(struct page **pages, int nr);

static inline struct page *page_cache_alloc(struct address_space *x)
{
	return alloc_pages(x->gfp_mask, 0);
}

typedef int filler_t(void *, struct page *);

extern struct page * find_get_page(struct address_space *mapping,
				unsigned long index);
extern struct page * find_lock_page(struct address_space *mapping,
				unsigned long index);
extern struct page * find_trylock_page(struct address_space *mapping,
				unsigned long index);
extern struct page * find_or_create_page(struct address_space *mapping,
				unsigned long index, unsigned int gfp_mask);

/*
 * Returns locked page at given index in given cache, creating it if needed.
 */
static inline struct page *grab_cache_page(struct address_space *mapping, unsigned long index)
{
	return find_or_create_page(mapping, index, mapping->gfp_mask);
}

extern struct page * grab_cache_page_nowait(struct address_space *mapping,
				unsigned long index);
extern struct page * read_cache_page(struct address_space *mapping,
				unsigned long index, filler_t *filler,
				void *data);

extern int add_to_page_cache(struct page *page,
		struct address_space *mapping, unsigned long index);
extern int add_to_page_cache_lru(struct page *page,
		struct address_space *mapping, unsigned long index);
extern void remove_from_page_cache(struct page *page);
extern void __remove_from_page_cache(struct page *page);

static inline void ___add_to_page_cache(struct page *page,
		struct address_space *mapping, unsigned long index)
{
	list_add(&page->list, &mapping->clean_pages);
	page->mapping = mapping;
	page->index = index;

	mapping->nrpages++;
	inc_page_state(nr_pagecache);
}

extern void FASTCALL(__lock_page(struct page *page));
extern void FASTCALL(unlock_page(struct page *page));

static inline void lock_page(struct page *page)
{
	if (TestSetPageLocked(page))
		__lock_page(page);
}
	
/*
 * This is exported only for wait_on_page_locked/wait_on_page_writeback.
 * Never use this directly!
 */
extern void FASTCALL(wait_on_page_bit(struct page *page, int bit_nr));

/* 
 * Wait for a page to be unlocked.
 *
 * This must be called with the caller "holding" the page,
 * ie with increased "page->count" so that the page won't
 * go away during the wait..
 */
static inline void wait_on_page_locked(struct page *page)
{
	if (PageLocked(page))
		wait_on_page_bit(page, PG_locked);
}

/* 
 * Wait for a page to complete writeback
 */
static inline void wait_on_page_writeback(struct page *page)
{
	if (PageWriteback(page))
		wait_on_page_bit(page, PG_writeback);
}

extern void end_page_writeback(struct page *page);
#endif /* _LINUX_PAGEMAP_H */
