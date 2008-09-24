#ifndef _TRACE_HUGETLB_H
#define _TRACE_HUGETLB_H

#include <linux/tracepoint.h>

DEFINE_TRACE(hugetlb_page_release,
	TPPROTO(struct page *page),
	TPARGS(page));
DEFINE_TRACE(hugetlb_page_grab,
	TPPROTO(struct page *page),
	TPARGS(page));
DEFINE_TRACE(hugetlb_buddy_pgalloc,
	TPPROTO(struct page *page),
	TPARGS(page));
DEFINE_TRACE(hugetlb_page_alloc,
	TPPROTO(struct page *page),
	TPARGS(page));
DEFINE_TRACE(hugetlb_page_free,
	TPPROTO(struct page *page),
	TPARGS(page));
DEFINE_TRACE(hugetlb_pages_reserve,
	TPPROTO(struct inode *inode, long from, long to, int ret),
	TPARGS(inode, from, to, ret));
DEFINE_TRACE(hugetlb_pages_unreserve,
	TPPROTO(struct inode *inode, long offset, long freed),
	TPARGS(inode, offset, freed));

#endif
