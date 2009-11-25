#ifndef _TRACE_HUGETLB_H
#define _TRACE_HUGETLB_H

#include <linux/tracepoint.h>

DECLARE_TRACE(hugetlb_page_release,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_TRACE(hugetlb_page_grab,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_TRACE(hugetlb_buddy_pgalloc,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_TRACE(hugetlb_page_alloc,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_TRACE(hugetlb_page_free,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_TRACE(hugetlb_pages_reserve,
	TP_PROTO(struct inode *inode, long from, long to, int ret),
	TP_ARGS(inode, from, to, ret));
DECLARE_TRACE(hugetlb_pages_unreserve,
	TP_PROTO(struct inode *inode, long offset, long freed),
	TP_ARGS(inode, offset, freed));

#endif
