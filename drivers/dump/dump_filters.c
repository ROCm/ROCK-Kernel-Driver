/*
 * Default filters to select data to dump for various passes.
 *
 * Started: Oct 2002 -  Suparna Bhattacharya <suparna@in.ibm.com>
 * 	Split and rewrote default dump selection logic to generic dump
 * 	method interfaces
 * Derived from a portion of dump_base.c created by
 * 	Matt Robinson <yakker@sourceforge.net>)
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 International Business Machines Corp.
 *
 * Used during single-stage dumping and during stage 1 of the 2-stage scheme
 * (Stage 2 of the 2-stage scheme uses the fully transparent filters
 * i.e. passthru filters in dump_overlay.c)
 *
 * Future: Custom selective dump may involve a different set of filters.
 *
 * This code is released under version 2 of the GNU GPL.
 */

#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/dump.h>
#include "dump_methods.h"

#define DUMP_PFN_SAFETY_MARGIN  1024  /* 4 MB */
static unsigned long bootmap_pages;

/* Copied from mm/bootmem.c - FIXME */
/* return the number of _pages_ that will be allocated for the boot bitmap */
void dump_calc_bootmap_pages (void)
{
	unsigned long mapsize;
	unsigned long pages = num_physpages;

	mapsize = (pages+7)/8;
	mapsize = (mapsize + ~PAGE_MASK) & PAGE_MASK;
	mapsize >>= PAGE_SHIFT;
	bootmap_pages = mapsize + DUMP_PFN_SAFETY_MARGIN + 1;
}


/* temporary */
extern unsigned long min_low_pfn;


int dump_low_page(struct page *p)
{
	return ((page_to_pfn(p) >= min_low_pfn) &&
		(page_to_pfn(p) < (min_low_pfn + bootmap_pages)));
}

static inline int kernel_page(struct page *p)
{
	return (PageReserved(p) && !PageInuse(p)) ||
		(!PageLRU(p) && PageInuse(p))     ||
		(PageCompound(p) && (long)p->lru.prev < 4);
}

static inline int user_page(struct page *p)
{
	return PageInuse(p) && !PageReserved(p) &&
		(PageLRU(p) ||
		(PageCompound(p) && ((struct page *)p->private)->mapping));
}

static inline int unreferenced_page(struct page *p)
{
	return (!PageInuse(p) && !PageReserved(p)) ||
		(PageCompound(p) && !((struct page *)p->private)->mapping);
}


/* loc marks the beginning of a range of pages */
int dump_filter_kernpages(int pass, unsigned long loc, unsigned long sz)
{
	struct page *page = (struct page *)loc;
	/* if any of the pages is a kernel page, select this set */
	while (sz) {
		if (dump_low_page(page) || kernel_page(page))
			return 1;
		sz -= PAGE_SIZE;
		page++;
	}
	return 0;
}


/* loc marks the beginning of a range of pages */
int dump_filter_userpages(int pass, unsigned long loc, unsigned long sz)
{
	struct page *page = (struct page *)loc;
	int ret = 0;
	/* select if the set has any user page, and no kernel pages  */
	while (sz) {
		if (user_page(page) && !dump_low_page(page)) {
			ret = 1;
		} else if (kernel_page(page) || dump_low_page(page)) {
			return 0;
		}
		page++;
		sz -= PAGE_SIZE;
	}
	return ret;
}



/* loc marks the beginning of a range of pages */
int dump_filter_unusedpages(int pass, unsigned long loc, unsigned long sz)
{
	struct page *page = (struct page *)loc;

	/* select if the set does not have any used pages  */
	while (sz) {
		if (!unreferenced_page(page) || dump_low_page(page)) {
			return 0;
		}
		page++;
		sz -= PAGE_SIZE;
	}
	return 1;
}

/* dummy: last (non-existent) pass */
int dump_filter_none(int pass, unsigned long loc, unsigned long sz)
{
	return 0;
}

/* TBD: resolve level bitmask ? */
struct dump_data_filter dump_filter_table[] = {
	{ .name = "kern", .selector = dump_filter_kernpages,
		.level_mask = DUMP_MASK_KERN},
	{ .name = "user", .selector = dump_filter_userpages,
		.level_mask = DUMP_MASK_USED},
	{ .name = "unused", .selector = dump_filter_unusedpages,
		.level_mask = DUMP_MASK_UNUSED},
	{ .name = "none", .selector = dump_filter_none,
		.level_mask = DUMP_MASK_REST},
	{ .name = "", .selector = NULL, .level_mask = 0}
};

