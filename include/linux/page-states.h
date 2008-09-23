#ifndef _LINUX_PAGE_STATES_H
#define _LINUX_PAGE_STATES_H

/*
 * include/linux/page-states.h
 *
 * Copyright IBM Corp. 2005, 2007
 *
 * Authors: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *          Hubertus Franke <frankeh@watson.ibm.com>
 *          Himanshu Raj <rhim@cc.gatech.edu>
 */

#include <linux/pagevec.h>

#ifdef CONFIG_PAGE_STATES
/*
 * Guest page hinting primitives that need to be defined in the
 * architecture header file if PAGE_STATES=y:
 * - page_host_discards:
 *     Indicates whether the host system discards guest pages or not.
 * - page_set_unused:
 *     Indicates to the host that the page content is of no interest
 *     to the guest. The host can "forget" the page content and replace
 *     it with a page containing zeroes.
 * - page_set_stable:
 *     Indicate to the host that the page content is needed by the guest.
 * - page_set_volatile:
 *     Make the page discardable by the host. Instead of writing the
 *     page to the hosts swap device, the host can remove the page.
 *     A guest that accesses such a discarded page gets a special
 *     discard fault.
 * - page_set_stable_if_present:
 *     The page state is set to stable if the page has not been discarded
 *     by the host. The check and the state change have to be done
 *     atomically.
 * - page_discarded:
 *     Returns true if the page has been discarded by the host.
 * - page_volatile:
 *     Returns true if the page is marked volatile.
 * - page_test_set_state_change:
 *     Tries to lock the page for state change. The primitive does not need
 *     to have page granularity, it can lock a range of pages.
 * - page_clear_state_change:
 *     Unlocks a page for state changes.
 * - page_state_change:
 *     Returns true if the page is locked for state change.
 * - page_free_discarded:
 *     Free a discarded page. This might require to put the page on a
 *     discard list and a synchronization over all cpus. Returns true
 *     if the architecture backend wants to do special things on free.
 */
#include <asm/page-states.h>

extern void page_unmap_all(struct page *page);
extern void page_discard(struct page *page);
extern int  __page_make_stable(struct page *page);
extern void __page_make_volatile(struct page *page, int offset);
extern void __pagevec_make_volatile(struct pagevec *pvec);
extern void __page_check_writable(struct page *page, pte_t pte,
				  unsigned int offset);
extern void __page_reset_writable(struct page *page);

/*
 * Extended guest page hinting functions defined by using the
 * architecture primitives:
 * - page_make_stable:
 *     Tries to make a page stable. This operation can fail if the
 *     host has discarded a page. The function returns != 0 if the
 *     page could not be made stable.
 * - page_make_volatile:
 *     Tries to make a page volatile. There are a number of conditions
 *     that prevent a page from becoming volatile. If at least one
 *     is true the function does nothing. See mm/page-states.c for
 *     details.
 * - pagevec_make_volatile:
 *     Tries to make a vector of pages volatile. For each page in the
 *     vector the same conditions apply as for page_make_volatile.
 * - page_discard:
 *     Removes a discarded page from the system. The page is removed
 *     from the LRU list and the radix tree of its mapping.
 *     page_discard uses page_unmap_all to remove all page table
 *     entries for a page.
 * - page_check_writable:
 *     Checks if the page states needs to be adapted because a new
 *     writable page table entry refering to the page is established.
 * - page_reset_writable:
 *     Resets the page state after the last writable page table entry
 *     refering to the page has been removed.
 */

static inline int page_make_stable(struct page *page)
{
	return page_host_discards() ? __page_make_stable(page) : 1;
}

static inline void page_make_volatile(struct page *page, int offset)
{
	if (page_host_discards())
		__page_make_volatile(page, offset);
}

static inline void pagevec_make_volatile(struct pagevec *pvec)
{
	if (page_host_discards())
		__pagevec_make_volatile(pvec);
}

static inline void page_check_writable(struct page *page, pte_t pte,
				       unsigned int offset)
{
	if (page_host_discards() && pte_write(pte) &&
	    !test_bit(PG_writable, &page->flags))
		__page_check_writable(page, pte, offset);
}

static inline void page_reset_writable(struct page *page)
{
	if (page_host_discards() && test_bit(PG_writable, &page->flags))
		__page_reset_writable(page);
}

#else

#define page_host_discards()			(0)
#define page_set_unused(_page,_order)		do { } while (0)
#define page_set_stable(_page,_order)		do { } while (0)
#define page_set_volatile(_page,_writable)	do { } while (0)
#define page_set_stable_if_present(_page)	(1)
#define page_discarded(_page)			(0)
#define page_volatile(_page)			(0)

#define page_test_set_state_change(_page)	(0)
#define page_clear_state_change(_page)		do { } while (0)
#define page_state_change(_page)		(0)

#define page_free_discarded(_page)		(0)

#define page_make_stable(_page)			(1)
#define page_make_volatile(_page, offset)	do { } while (0)
#define pagevec_make_volatile(_pagevec)	do { } while (0)
#define page_discard(_page)			do { } while (0)
#define page_check_writable(_page,_pte,_off)	do { } while (0)
#define page_reset_writable(_page)		do { } while (0)

#endif

#endif /* _LINUX_PAGE_STATES_H */
