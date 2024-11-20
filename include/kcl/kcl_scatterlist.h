/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_LINUX_SCATTERLIST_H
#define _KCL_LINUX_SCATTERLIST_H

#include <linux/scatterlist.h>

#ifndef HAVE_SG_ALLOC_TABLE_FROM_PAGES_SEGMENT
int _kcl_sg_alloc_table_from_pages_segment(struct sg_table *sgt, struct page **pages,
				      unsigned int n_pages, unsigned int offset,
				      unsigned long size,
				      unsigned int max_segment, gfp_t gfp_mask);
#define sg_alloc_table_from_pages_segment _kcl_sg_alloc_table_from_pages_segment
#endif

#endif /* _LINUX_SCATTERLIST_H */
