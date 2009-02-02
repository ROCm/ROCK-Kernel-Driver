#ifndef _TRACE_PAGE_ALLOC_H
#define _TRACE_PAGE_ALLOC_H

#include <linux/tracepoint.h>

/*
 * mm_page_alloc : page can be NULL.
 */
DECLARE_TRACE(page_alloc,
	TPPROTO(struct page *page, unsigned int order),
	TPARGS(page, order));
DECLARE_TRACE(page_free,
	TPPROTO(struct page *page, unsigned int order),
	TPARGS(page, order));

#endif
