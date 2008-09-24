#ifndef _TRACE_SWAP_H
#define _TRACE_SWAP_H

#include <linux/swap.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(swap_in,
	TPPROTO(struct page *page, swp_entry_t entry),
	TPARGS(page, entry));
DEFINE_TRACE(swap_out,
	TPPROTO(struct page *page),
	TPARGS(page));
DEFINE_TRACE(swap_file_open,
	TPPROTO(struct file *file, char *filename),
	TPARGS(file, filename));
DEFINE_TRACE(swap_file_close,
	TPPROTO(struct file *file),
	TPARGS(file));

#endif
