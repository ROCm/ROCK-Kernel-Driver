#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
/*
 * Declarations for Reverse Mapping functions in mm/rmap.c
 * Its structures are declared within that file.
 */

#include <linux/config.h>
#include <linux/linkage.h>

#define page_map_lock(page) \
	bit_spin_lock(PG_maplock, (unsigned long *)&(page)->flags)
#define page_map_unlock(page) \
	bit_spin_unlock(PG_maplock, (unsigned long *)&(page)->flags)

#ifdef CONFIG_MMU

void fastcall page_add_anon_rmap(struct page *,
		struct mm_struct *, unsigned long addr);
void fastcall page_add_file_rmap(struct page *);
void fastcall page_remove_rmap(struct page *);

/**
 * page_dup_rmap - duplicate pte mapping to a page
 * @page:	the page to add the mapping to
 *
 * For copy_page_range only: minimal extract from page_add_rmap,
 * avoiding unnecessary tests (already checked) so it's quicker.
 */
static inline void page_dup_rmap(struct page *page)
{
	page_map_lock(page);
	page->mapcount++;
	page_map_unlock(page);
}

/*
 * Called from mm/vmscan.c to handle paging out
 */
int fastcall page_referenced(struct page *);
int fastcall try_to_unmap(struct page *);

#else	/* !CONFIG_MMU */

#define page_referenced(page)	TestClearPageReferenced(page)
#define try_to_unmap(page)	SWAP_FAIL

#endif	/* CONFIG_MMU */

/*
 * Return values of try_to_unmap
 */
#define SWAP_SUCCESS	0
#define SWAP_AGAIN	1
#define SWAP_FAIL	2

#endif	/* _LINUX_RMAP_H */
