/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Declaration of lnode (light-weight node). */

#ifndef __LNODE_H__
#define __LNODE_H__

#include "forward.h"
#include "dformat.h"
#include "kcond.h"
#include "type_safe_hash.h"
#include "plugin/plugin_header.h"
#include "plugin/plugin_set.h"
#include "key.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block, etc.  */
#include <linux/dcache.h>	/* for struct super_block, etc.  */

typedef enum {
	LNODE_DENTRY,
	LNODE_INODE,
	LNODE_REISER4_INODE,
	LNODE_LW,
	LNODE_PSEUDO,
	LNODE_NR_TYPES
} lnode_type;

typedef union lnode lnode;

/* declare hash table of lnode_lw's */
TYPE_SAFE_HASH_DECLARE(ln, lnode);

/* common part of various lnode types */
typedef struct lnode_header {
	/* lnode type. Taken from lnode_type enum. Never changed after
	   initialisation, so needs no locking.  */
	__u8 type;
	/* unused. Alignment requires this anyway. */
	__u8 flags;
	/* condition variable to wake up waiters */
	kcond_t cvar;
	/* hash table linkage. Updated under hash-table spinlock. */
	ln_hash_link link;
	/* objectid of underlying file system object. Never changed after
	   initialisation, so needs no locking.  */
	oid_t oid;
	/* reference counter. Updated under hash-table spinlock. */
	int ref;
} lnode_header;

typedef struct lnode_dentry {
	lnode_header h;
	struct dentry *dentry;
	struct vfsmount *mnt;
} lnode_dentry;

typedef struct lnode_inode {
	lnode_header h;
	struct inode *inode;
} lnode_inode;

typedef struct lnode_reiser4_inode {
	lnode_header h;
	struct reiser4_inode *inode;
} lnode_reiser4_inode;

typedef struct lnode_lw {
	lnode_header h;
	struct super_block * lw_sb;
	reiser4_key key;
} lnode_lw;

struct assign_result {
	loff_t len ;
	int return_code ;
};

typedef struct lnode_pseudo {
	lnode_header h;
	struct assign_result rez;

	//	lnode *host;
	/* something to identify pseudo file type, like name or plugin */
} lnode_pseudo;

union lnode {
	lnode_header h;
	lnode_dentry dentry;
	lnode_inode inode;
	lnode_reiser4_inode reiser4_inode;
	lnode_lw lw;
	lnode_pseudo pseudo;
};

extern int lnodes_init(void);
extern int lnodes_done(void);

extern lnode *lget( lnode_type type, oid_t oid);
extern void lput(lnode * node);
extern int lnode_eq(const lnode * node1, const lnode * node2);
extern lnode *lref(lnode * node);

extern struct inode *inode_by_lnode(const lnode * node);
extern reiser4_key *lnode_key(const lnode * node, reiser4_key * result);

extern int get_lnode_plugins(const lnode * node, plugin_set * area);
extern int set_lnode_plugins(lnode * node, const plugin_set * area);

/* __LNODE_H__ */
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
