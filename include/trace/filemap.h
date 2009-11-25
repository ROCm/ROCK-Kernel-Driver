#ifndef _TRACE_FILEMAP_H
#define _TRACE_FILEMAP_H

#include <linux/tracepoint.h>

DECLARE_TRACE(wait_on_page_start,
	TP_PROTO(struct page *page, int bit_nr),
		TP_ARGS(page, bit_nr));
DECLARE_TRACE(wait_on_page_end,
	TP_PROTO(struct page *page, int bit_nr),
		TP_ARGS(page, bit_nr));

#endif
