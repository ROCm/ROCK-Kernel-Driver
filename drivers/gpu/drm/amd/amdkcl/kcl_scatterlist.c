// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007 Jens Axboe <jens.axboe@oracle.com>
 *
 * Scatterlist handling helpers.
 */
#include <kcl/kcl_scatterlist.h>

#ifndef HAVE_SG_ALLOC_TABLE_FROM_PAGES_SEGMENT
int _kcl_sg_alloc_table_from_pages_segment(struct sg_table *sgt, struct page **pages,
				unsigned int n_pages, unsigned int offset,
				unsigned long size, unsigned int max_segment,
				gfp_t gfp_mask)
{
	return PTR_ERR_OR_ZERO(__sg_alloc_table_from_pages(sgt, pages, n_pages,
			offset, size, max_segment, gfp_mask));
}
EXPORT_SYMBOL(_kcl_sg_alloc_table_from_pages_segment);
#endif
