/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Emergency flush */

#ifndef __EMERGENCY_FLUSH_H__
#define __EMERGENCY_FLUSH_H__

#if REISER4_USE_EFLUSH

#include "block_alloc.h"

struct eflush_node;
typedef struct eflush_node eflush_node_t;

TYPE_SAFE_HASH_DECLARE(ef, eflush_node_t);

struct eflush_node {
	jnode           *node;
	reiser4_block_nr blocknr;
	ef_hash_link     linkage;
	struct list_head inode_link; /* for per inode list of eflush nodes */
	struct list_head inode_anon_link;
	int              hadatom :1;
	int              incatom :1;
	int              reserve :1;
#if REISER4_DEBUG
	block_stage_t    initial_stage;
#endif
};

int eflush_init(void);
int eflush_done(void);

extern int  eflush_init_at(struct super_block *super);
extern void eflush_done_at(struct super_block *super);

extern reiser4_block_nr *eflush_get(const jnode *node);
extern void eflush_del(jnode *node, int page_locked);

extern int emergency_flush(struct page *page);
extern int emergency_unflush(jnode *node);

/* eflushed jnodes are stored in reiser4_inode's radix tree. Eflushed jnodes may be either "captured" or
 * "anonymous". Use existing tags to tag jnodes in reiser4_inode's tree of eflushed jnodes */
#define EFLUSH_TAG_ANONYMOUS PAGECACHE_TAG_DIRTY
#define EFLUSH_TAG_CAPTURED PAGECACHE_TAG_WRITEBACK

#else /* REISER4_USE_EFLUSH */

#define eflush_init()  (0)
#define eflush_done()  (0)

#define eflush_init_at(super) (0)
#define eflush_done_at(super) (0)

#define eflush_get(node)  NULL
#define eflush_del(node, flag) do{}while(0)

#define emergency_unflush(node) (0)
#define emergency_flush(page) (1)

#endif  /* REISER4_USE_EFLUSH */

/* __EMERGENCY_FLUSH_H__ */
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
