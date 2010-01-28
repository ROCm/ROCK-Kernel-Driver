#include <linux/module.h>
#include "blkback-pagemap.h"

static int blkback_pagemap_size;
static struct blkback_pagemap *blkback_pagemap;

static inline int
blkback_pagemap_entry_clear(struct blkback_pagemap *map)
{
	static struct blkback_pagemap zero;
	return !memcmp(map, &zero, sizeof(zero));
}

int
blkback_pagemap_init(int pages)
{
	blkback_pagemap = kzalloc(pages * sizeof(struct blkback_pagemap),
				  GFP_KERNEL);
	if (!blkback_pagemap)
		return -ENOMEM;

	blkback_pagemap_size = pages;
	return 0;
}
EXPORT_SYMBOL_GPL(blkback_pagemap_init);

void
blkback_pagemap_set(int idx, struct page *page,
		    domid_t domid, busid_t busid, grant_ref_t gref)
{
	struct blkback_pagemap *entry;

	BUG_ON(!blkback_pagemap);
	BUG_ON(idx >= blkback_pagemap_size);

	SetPageBlkback(page);
	set_page_private(page, idx);

	entry = blkback_pagemap + idx;
	if (!blkback_pagemap_entry_clear(entry)) {
		printk("overwriting pagemap %d: d %u b %u g %u\n",
		       idx, entry->domid, entry->busid, entry->gref);
		BUG();
	}

	entry->domid = domid;
	entry->busid = busid;
	entry->gref  = gref;
}
EXPORT_SYMBOL_GPL(blkback_pagemap_set);

void
blkback_pagemap_clear(struct page *page)
{
	int idx;
	struct blkback_pagemap *entry;

	idx = (int)page_private(page);

	BUG_ON(!blkback_pagemap);
	BUG_ON(!PageBlkback(page));
	BUG_ON(idx >= blkback_pagemap_size);

	entry = blkback_pagemap + idx;
	if (blkback_pagemap_entry_clear(entry)) {
		printk("clearing empty pagemap %d\n", idx);
		BUG();
	}

	memset(entry, 0, sizeof(*entry));
}
EXPORT_SYMBOL_GPL(blkback_pagemap_clear);

struct blkback_pagemap
blkback_pagemap_read(struct page *page)
{
	int idx;
	struct blkback_pagemap *entry;

	idx = (int)page_private(page);

	BUG_ON(!blkback_pagemap);
	BUG_ON(!PageBlkback(page));
	BUG_ON(idx >= blkback_pagemap_size);

	entry = blkback_pagemap + idx;
	if (blkback_pagemap_entry_clear(entry)) {
		printk("reading empty pagemap %d\n", idx);
		BUG();
	}

	return *entry;
}
EXPORT_SYMBOL(blkback_pagemap_read);

MODULE_LICENSE("Dual BSD/GPL");
