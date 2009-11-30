#ifndef _TRACE_SWAP_H
#define _TRACE_SWAP_H

#include <linux/swap.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(swap_in,
	TP_PROTO(struct page *page, swp_entry_t entry),
		TP_ARGS(page, entry));
DECLARE_TRACE(swap_out,
	TP_PROTO(struct page *page),
		TP_ARGS(page));
DECLARE_TRACE(swap_file_open,
	TP_PROTO(struct file *file, char *filename),
		TP_ARGS(file, filename));
DECLARE_TRACE(swap_file_close,
	TP_PROTO(struct file *file),
		TP_ARGS(file));

#endif
