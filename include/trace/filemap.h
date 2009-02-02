#ifndef _TRACE_FILEMAP_H
#define _TRACE_FILEMAP_H

#include <linux/tracepoint.h>

DECLARE_TRACE(wait_on_page_start,
	TPPROTO(struct page *page, int bit_nr),
	TPARGS(page, bit_nr));
DECLARE_TRACE(wait_on_page_end,
	TPPROTO(struct page *page, int bit_nr),
	TPARGS(page, bit_nr));

#endif
