/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Fast pool allocation */

#ifndef __REISER4_POOL_H__
#define __REISER4_POOL_H__

#include "type_safe_list.h"
#include <linux/types.h>

/* each pool object is either on a "used" or "free" list. */
TYPE_SAFE_LIST_DECLARE(pool_usage);

/* list of extra pool objects */
TYPE_SAFE_LIST_DECLARE(pool_extra);

/* list of pool objects on a given level */
TYPE_SAFE_LIST_DECLARE(pool_level);

typedef struct reiser4_pool {
	size_t obj_size;
	int objs;
	char *data;
	pool_usage_list_head free;
	pool_usage_list_head used;
	pool_extra_list_head extra;
} reiser4_pool;

typedef struct reiser4_pool_header {
	/* object is either on free or "used" lists */
	pool_usage_list_link usage_linkage;
	pool_level_list_link level_linkage;
	pool_extra_list_link extra_linkage;
} reiser4_pool_header;

typedef enum {
	POOLO_BEFORE,
	POOLO_AFTER,
	POOLO_LAST,
	POOLO_FIRST
} pool_ordering;

/* each pool object is either on a "used" or "free" list. */
TYPE_SAFE_LIST_DEFINE(pool_usage, reiser4_pool_header, usage_linkage);
/* list of extra pool objects */
TYPE_SAFE_LIST_DEFINE(pool_extra, reiser4_pool_header, extra_linkage);
/* list of pool objects on a given level */
TYPE_SAFE_LIST_DEFINE(pool_level, reiser4_pool_header, level_linkage);

/* pool manipulation functions */

extern void reiser4_init_pool(reiser4_pool * pool, size_t obj_size, int num_of_objs, char *data);
extern void reiser4_done_pool(reiser4_pool * pool);
extern void *reiser4_pool_alloc(reiser4_pool * pool);
extern void reiser4_pool_free(reiser4_pool * pool, reiser4_pool_header * h);
reiser4_pool_header *add_obj(reiser4_pool * pool, pool_level_list_head * list,
			     pool_ordering order, reiser4_pool_header * reference);

/* __REISER4_POOL_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
