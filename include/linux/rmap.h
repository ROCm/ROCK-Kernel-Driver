#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
/*
 * Declarations for Reverse Mapping functions in mm/rmap.c
 * Its structures are declared within that file.
 */

#include <linux/config.h>

#define page_map_lock(page) \
	bit_spin_lock(PG_maplock, (unsigned long *)&(page)->flags)
#define page_map_unlock(page) \
	bit_spin_unlock(PG_maplock, (unsigned long *)&(page)->flags)

#ifdef CONFIG_MMU

/*
 * rmap interfaces called when adding or removing pte of page
 */
void page_add_anon_rmap(struct page *, struct vm_area_struct *, unsigned long);
void page_add_file_rmap(struct page *);
void page_remove_rmap(struct page *);

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

int mremap_move_anon_rmap(struct page *page, unsigned long addr);

/**
 * mremap_moved_anon_rmap - does new address clash with that noted?
 * @page:	the page just brought back in from swap
 * @addr:	the user virtual address at which it is mapped
 *
 * Returns boolean, true if addr clashes with address already in page.
 *
 * For do_swap_page and unuse_pte: anonmm rmap cannot find the page if
 * it's at different addresses in different mms, so caller must take a
 * copy of the page to avoid that: not very clever, but too rare a case
 * to merit cleverness.
 */
static inline int mremap_moved_anon_rmap(struct page *page, unsigned long addr)
{
	return page->index != (addr & PAGE_MASK);
}

/**
 * make_page_exclusive - try to make page exclusive to one mm
 * @vma		the vm_area_struct covering this address
 * @addr	the user virtual address of the page in question
 *
 * Assumes that the page at this address is anonymous (COWable),
 * and that the caller holds mmap_sem for reading or for writing.
 *
 * For mremap's move_page_tables and for swapoff's unuse_process:
 * not a general purpose routine, and in general may not succeed.
 * But move_page_tables loops until it succeeds, and unuse_process
 * holds the original page locked, which protects against races.
 */
static inline int make_page_exclusive(struct vm_area_struct *vma,
					unsigned long addr)
{
	if (handle_mm_fault(vma->vm_mm, vma, addr, 1) != VM_FAULT_OOM)
		return 0;
	return -ENOMEM;
}

/*
 * Called from kernel/fork.c to manage anonymous memory
 */
void init_rmap(void);
int exec_rmap(struct mm_struct *);
int dup_rmap(struct mm_struct *, struct mm_struct *oldmm);
void exit_rmap(struct mm_struct *);

/*
 * Called from mm/vmscan.c to handle paging out
 */
int page_referenced(struct page *);
int try_to_unmap(struct page *);

#else	/* !CONFIG_MMU */

#define init_rmap()		do {} while (0)
#define exec_rmap(mm)		(0)
#define dup_rmap(mm, oldmm)	(0)
#define exit_rmap(mm)		do {} while (0)

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
