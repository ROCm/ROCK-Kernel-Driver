/*
 * Macros for manipulating and testing page->flags
 */

#ifndef PAGE_FLAGS_H
#define PAGE_FLAGS_H

/*
 * Global page accounting.  One instance per CPU.
 */
extern struct page_state {
	unsigned long nr_dirty;
	unsigned long nr_locked;
	unsigned long nr_pagecache;
} ____cacheline_aligned_in_smp page_states[NR_CPUS];

extern void get_page_state(struct page_state *ret);

#define mod_page_state(member, delta)					\
	do {								\
		preempt_disable();					\
		page_states[smp_processor_id()].member += (delta);	\
		preempt_enable();					\
	} while (0)

#define inc_page_state(member)	mod_page_state(member, 1UL)
#define dec_page_state(member)	mod_page_state(member, 0UL - 1)


/*
 * Manipulation of page state flags
 */
#define UnlockPage(page)	unlock_page(page)
#define PageLocked(page)	test_bit(PG_locked_dontuse, &(page)->flags)
#define SetPageLocked(page)						\
	do {								\
		if (!test_and_set_bit(PG_locked_dontuse,		\
				&(page)->flags))			\
			inc_page_state(nr_locked);			\
	} while (0)
#define LockPage(page)		SetPageLocked(page)	/* grr.  kill me */
#define TryLockPage(page)						\
	({								\
		int ret;						\
		ret = test_and_set_bit(PG_locked_dontuse,		\
					&(page)->flags);		\
		if (!ret)						\
			inc_page_state(nr_locked);			\
		ret;							\
	})
#define ClearPageLocked(page)						\
	do {								\
		if (test_and_clear_bit(PG_locked_dontuse,		\
				&(page)->flags))			\
			dec_page_state(nr_locked);			\
	} while (0)
#define TestClearPageLocked(page)					\
	({								\
		int ret;						\
		ret = test_and_clear_bit(PG_locked_dontuse,		\
				&(page)->flags);			\
		if (ret)						\
			dec_page_state(nr_locked);			\
		ret;							\
	})

#define PageError(page)		test_bit(PG_error, &(page)->flags)
#define SetPageError(page)	set_bit(PG_error, &(page)->flags)
#define ClearPageError(page)	clear_bit(PG_error, &(page)->flags)

#define PageReferenced(page)	test_bit(PG_referenced, &(page)->flags)
#define SetPageReferenced(page)	set_bit(PG_referenced, &(page)->flags)
#define ClearPageReferenced(page)	clear_bit(PG_referenced, &(page)->flags)
#define PageTestandClearReferenced(page)	test_and_clear_bit(PG_referenced, &(page)->flags)

#define Page_Uptodate(page)	test_bit(PG_uptodate, &(page)->flags)
#define SetPageUptodate(page)	set_bit(PG_uptodate, &(page)->flags)
#define ClearPageUptodate(page)	clear_bit(PG_uptodate, &(page)->flags)

#define PageDirty(page)		test_bit(PG_dirty_dontuse, &(page)->flags)
#define SetPageDirty(page)						\
	do {								\
		if (!test_and_set_bit(PG_dirty_dontuse,			\
					&(page)->flags))		\
			inc_page_state(nr_dirty);			\
	} while (0)
#define TestSetPageDirty(page)						\
	({								\
		int ret;						\
		ret = test_and_set_bit(PG_dirty_dontuse,		\
				&(page)->flags);			\
		if (!ret)						\
			inc_page_state(nr_dirty);			\
		ret;							\
	})
#define ClearPageDirty(page)						\
	do {								\
		if (test_and_clear_bit(PG_dirty_dontuse,		\
				&(page)->flags))			\
			dec_page_state(nr_dirty);			\
	} while (0)
#define TestClearPageDirty(page)					\
	({								\
		int ret;						\
		ret = test_and_clear_bit(PG_dirty_dontuse,		\
				&(page)->flags);			\
		if (ret)						\
			dec_page_state(nr_dirty);			\
		ret;							\
	})

#define PageLRU(page)		test_bit(PG_lru, &(page)->flags)
#define TestSetPageLRU(page)	test_and_set_bit(PG_lru, &(page)->flags)
#define TestClearPageLRU(page)	test_and_clear_bit(PG_lru, &(page)->flags)

#define PageActive(page)	test_bit(PG_active, &(page)->flags)
#define SetPageActive(page)	set_bit(PG_active, &(page)->flags)
#define ClearPageActive(page)	clear_bit(PG_active, &(page)->flags)

#define PageSlab(page)		test_bit(PG_slab, &(page)->flags)
#define PageSetSlab(page)	set_bit(PG_slab, &(page)->flags)
#define PageClearSlab(page)	clear_bit(PG_slab, &(page)->flags)

#ifdef CONFIG_HIGHMEM
#define PageHighMem(page)	test_bit(PG_highmem, &(page)->flags)
#else
#define PageHighMem(page)	0 /* needed to optimize away at compile time */
#endif

#define PageChecked(page)	test_bit(PG_checked, &(page)->flags)
#define SetPageChecked(page)	set_bit(PG_checked, &(page)->flags)

#define PageReserved(page)	test_bit(PG_reserved, &(page)->flags)
#define SetPageReserved(page)	set_bit(PG_reserved, &(page)->flags)
#define ClearPageReserved(page)	clear_bit(PG_reserved, &(page)->flags)
#define __SetPageReserved(page)	__set_bit(PG_reserved, &(page)->flags)

#define PageLaunder(page)	test_bit(PG_launder, &(page)->flags)
#define SetPageLaunder(page)	set_bit(PG_launder, &(page)->flags)

#define SetPagePrivate(page)	set_bit(PG_private, &(page)->flags)
#define ClearPagePrivate(page)	clear_bit(PG_private, &(page)->flags)
#define PagePrivate(page)	test_bit(PG_private, &(page)->flags)

#endif	/* PAGE_FLAGS_H */
