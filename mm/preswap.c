/*
 * linux/mm/preswap.c
 *
 * Implements a fast "preswap" on top of the transcendent memory ("tmem") API.
 * When a swapdisk is enabled (with swapon), a "private persistent tmem pool"
 * is created along with a bit-per-page preswap_map.  When swapping occurs
 * and a page is about to be written to disk, a "put" into the pool may first
 * be attempted by passing the pageframe to be swapped, along with a "handle"
 * consisting of a pool_id, an object id, and an index.  Since the pool is of
 * indeterminate size, the "put" may be rejected, in which case the page
 * is swapped to disk as normal.  If the "put" is successful, the page is
 * copied to tmem and the preswap_map records the success.  Later, when
 * the page needs to be swapped in, the preswap_map is checked and, if set,
 * the page may be obtained with a "get" operation.  Note that the swap
 * subsystem is responsible for: maintaining coherency between the swapcache,
 * preswap, and the swapdisk; for evicting stale pages from preswap; and for
 * emptying preswap when swapoff is performed. The "flush page" and "flush
 * object" actions are provided for this.
 *
 * Note that if a "duplicate put" is performed to overwrite a page and
 * the "put" operation fails, the page (and old data) is flushed and lost.
 * Also note that multiple accesses to a tmem pool may be concurrent and
 * any ordering must be guaranteed by the caller.
 *
 * Copyright (C) 2008,2009 Dan Magenheimer, Oracle Corp.
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sysctl.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/capability.h>
#include <linux/uaccess.h>
#include "tmem.h"

static u32 preswap_poolid = -1; /* if negative, preswap will never call tmem */

const unsigned long preswap_zero = 0, preswap_infinity = ~0UL; /* for sysctl */

/*
 * Swizzling increases objects per swaptype, increasing tmem concurrency
 * for heavy swaploads.  Later, larger nr_cpus -> larger SWIZ_BITS
 */
#define SWIZ_BITS		4
#define SWIZ_MASK		((1 << SWIZ_BITS) - 1)
#define oswiz(_type, _ind)	((_type << SWIZ_BITS) | (_ind & SWIZ_MASK))
#define iswiz(_ind)		(_ind >> SWIZ_BITS)

/*
 * preswap_map test/set/clear operations (must be atomic)
 */

int preswap_test(struct swap_info_struct *sis, unsigned long offset)
{
	if (!sis->preswap_map)
		return 0;
	return test_bit(offset % BITS_PER_LONG,
		&sis->preswap_map[offset/BITS_PER_LONG]);
}

static inline void preswap_set(struct swap_info_struct *sis,
				unsigned long offset)
{
	if (!sis->preswap_map)
		return;
	set_bit(offset % BITS_PER_LONG,
		&sis->preswap_map[offset/BITS_PER_LONG]);
}

static inline void preswap_clear(struct swap_info_struct *sis,
				unsigned long offset)
{
	if (!sis->preswap_map)
		return;
	clear_bit(offset % BITS_PER_LONG,
		&sis->preswap_map[offset/BITS_PER_LONG]);
}

/*
 * preswap tmem operations
 */

/* returns 1 if the page was successfully put into preswap, 0 if the page
 * was declined, and -ERRNO for a specific error */
int preswap_put(struct page *page)
{
	swp_entry_t entry = { .val = page_private(page), };
	unsigned type = swp_type(entry);
	pgoff_t offset = swp_offset(entry);
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	unsigned long mfn = pfn_to_mfn(page_to_pfn(page));
	struct swap_info_struct *sis = get_swap_info_struct(type);
	int dup = 0, ret;

	if ((s32)preswap_poolid < 0)
		return 0;
	if (ind64 != ind)
		return 0;
	if (preswap_test(sis, offset))
		dup = 1;
	mb(); /* ensure page is quiescent; tmem may address it with an alias */
	ret = tmem_put_page(preswap_poolid, oswiz(type, ind), iswiz(ind), mfn);
	if (ret == 1) {
		preswap_set(sis, offset);
		if (!dup)
			sis->preswap_pages++;
	} else if (dup) {
		/* failed dup put always results in an automatic flush of
		 * the (older) page from preswap */
		preswap_clear(sis, offset);
		sis->preswap_pages--;
	}
	return ret;
}

/* returns 1 if the page was successfully gotten from preswap, 0 if the page
 * was not present (should never happen!), and -ERRNO for a specific error */
int preswap_get(struct page *page)
{
	swp_entry_t entry = { .val = page_private(page), };
	unsigned type = swp_type(entry);
	pgoff_t offset = swp_offset(entry);
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	unsigned long mfn = pfn_to_mfn(page_to_pfn(page));
	struct swap_info_struct *sis = get_swap_info_struct(type);
	int ret;

	if ((s32)preswap_poolid < 0)
		return 0;
	if (ind64 != ind)
		return 0;
	if (!preswap_test(sis, offset))
		return 0;
	ret = tmem_get_page(preswap_poolid, oswiz(type, ind), iswiz(ind), mfn);
	return ret;
}

/* flush a single page from preswap */
void preswap_flush(unsigned type, unsigned long offset)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	struct swap_info_struct *sis = get_swap_info_struct(type);
	int ret = 1;

	if ((s32)preswap_poolid < 0)
		return;
	if (ind64 != ind)
		return;
	if (preswap_test(sis, offset)) {
		ret = tmem_flush_page(preswap_poolid,
					oswiz(type, ind), iswiz(ind));
		sis->preswap_pages--;
		preswap_clear(sis, offset);
	}
}

/* flush all pages from the passed swaptype */
void preswap_flush_area(unsigned type)
{
	struct swap_info_struct *sis = get_swap_info_struct(type);
	int ind;

	if ((s32)preswap_poolid < 0)
		return;
	for (ind = SWIZ_MASK; ind >= 0; ind--)
		(void)tmem_flush_object(preswap_poolid, oswiz(type, ind));
	sis->preswap_pages = 0;
}

void preswap_init(unsigned type)
{
	/* only need one tmem pool for all swap types */
	if ((s32)preswap_poolid >= 0)
		return;
	preswap_poolid = tmem_new_pool(0, 0, TMEM_POOL_PERSIST);
	if (preswap_poolid < 0)
		return;
}
