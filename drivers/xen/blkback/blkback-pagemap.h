#ifndef _BLKBACK_PAGEMAP_H_
#define _BLKBACK_PAGEMAP_H_

#include <linux/mm.h>
#include <xen/interface/xen.h>
#include <xen/interface/grant_table.h>

typedef unsigned int busid_t;

struct blkback_pagemap {
	domid_t          domid;
	busid_t          busid;
	grant_ref_t      gref;
};

#if defined(CONFIG_XEN_BLKBACK_PAGEMAP) || defined(CONFIG_XEN_BLKBACK_PAGEMAP_MODULE)

int blkback_pagemap_init(int);
void blkback_pagemap_set(int, struct page *, domid_t, busid_t, grant_ref_t);
void blkback_pagemap_clear(struct page *);
struct blkback_pagemap blkback_pagemap_read(struct page *);

#else /* CONFIG_XEN_BLKBACK_PAGEMAP */

static inline int blkback_pagemap_init(int pages) { return 0; }
static inline void blkback_pagemap_set(int idx, struct page *page, domid_t dom,
				       busid_t bus, grant_ref_t gnt) {}
static inline void blkback_pagemap_clear(struct page *page) {}
static inline struct blkback_pagemap blkback_pagemap_read(struct page *page)
{
	BUG();
	return (struct blkback_pagemap){-1, -1, -1};
}

#endif /* CONFIG_XEN_BLKBACK_PAGEMAP */

#endif
