/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Lnode manipulation functions. */
/* Lnode is light-weight node used as common data-structure by both VFS access
   paths and reiser4() system call processing.

   One of the main targets of reiser4() system call is to allow manipulation
   on potentially huge number of objects. This makes use of inode in reiser4()
   impossible. On the other hand there is a need to synchronize reiser4() and
   VFS access.

   To do this small object (lnode) is allocated (on the stack if possible) for
   each object involved into reiser4() system call. Such lnode only contains
   lock, information necessary to link it into global hash table, and
   condition variable to wake up waiters (see below).

   In other words, lnode is handle that reiser4 keeps for a file system object
   while object is being actively used. For example, when read is performed by
   reiser4_read(), lnode exists for inode being read. When reiser4_read()
   exits lnode is deleted, but inode is still there in the inode cache.

   As lnode only exists while object is being actively manipulated by some
   threads, it follows that lnodes can always live on the stack of such
   threads.

   Case-by-case:

     A. access through VFS (reiser4_{read|write|truncate|*}()):

       1. operation starts with inode supplied by VFS.

       2. lget( &local_lnode, LNODE_INODE, inode -> i_ino ) is called. This,
       if necessary, will wait until sys_reiser4() access to this file is
       finished, and

       3. add lnode to the per super block hash table.

     B. creation of new inode in reiser4_iget():

       1. create new empty inode (iget(), or icreate())

       2. step A.3. A.2 is not necessary, because we are creating new object
       and parent is in VFS access (hence sys_reiser4() cannot add/delete
       objects in parent).

       3. read stat data from disk and initialise inode

     C. sys_reiser4() access:

       1. check for existing inode in a hash-table.

          Rationale: if inode is already here it is advantageous to use it,
          because it already has information from stat data.

          If inode is found proceed as in case A.

       2. otherwise, lget( &local_lnode, LNODE_LW, oid ) is called.


   NOT FINISHED.







   INTERNAL NOTES:

   1. fs/inode.c:inode_lock is not static: we can use it. Good.

   2. but fs/inode.c:find_inode() is. Either write own version, or remove
   static and EXPORT_SYMBOL-ize it.



*/

#include "debug.h"
#include "kcond.h"
#include "key.h"
#include "kassign.h"
#include "plugin/plugin_header.h"
#include "plugin/plugin_set.h"
#include "lnode.h"
#include "super.h"
#include "reiser4.h"

#include <linux/fs.h>		/* for struct super_block  */
#include <linux/spinlock.h>

static reiser4_key *lnode_dentry_key(const lnode * node, reiser4_key * result);
static reiser4_key *lnode_inode_key(const lnode * node, reiser4_key * result);
static reiser4_key *lnode_lw_key(const lnode * node, reiser4_key * result);
static int lnode_inode_eq(const lnode * node1, const lnode * node2);
static int lnode_lw_eq(const lnode * node1, const lnode * node2);

#if REISER4_DEBUG
static int lnode_valid_type(lnode_type type);
#endif

/* Common operations for various types of lnodes.

   NOTE-NIKITA consider making this plugin. */
static struct {
	/* get a key of the corresponding file system object */
	reiser4_key *(*key) (const lnode * node, reiser4_key * result);
	/* get a plugin suitable for the corresponding file system object */
	int (*get_plugins) (const lnode * node, plugin_set * area);
	/* set a plugin suitable for the corresponding file system object */
	int (*set_plugins) (lnode * node, const plugin_set * area);
	/* true if @node1 and @node2 refer to the same object */
	int (*eq) (const lnode * node1, const lnode * node2);
} lnode_ops[LNODE_NR_TYPES] = {
	[LNODE_DENTRY] = {
		.key = lnode_dentry_key,
		.get_plugins = NULL,
		.set_plugins = NULL,
		.eq = NULL
	},
	[LNODE_INODE] = {
		.key = lnode_inode_key,
		.get_plugins = NULL,
		.set_plugins = NULL,
		.eq = lnode_inode_eq
	},
	/*
	[LNODE_PSEUDO] = {
		.key = NULL,
		.get_plugins = NULL,
		.set_plugins = NULL,
		.eq = NULL
	},
	*/
	[LNODE_LW] = {
		.key = lnode_lw_key,
		.get_plugins = NULL,
		.set_plugins = NULL,
		.eq = lnode_lw_eq
	}
};

/* hash table support */

/* compare two block numbers for equality. Used by hash-table macros */
/* Audited by: green(2002.06.15) */
static inline int
oid_eq(const oid_t * o1 /* first oid to compare */ ,
       const oid_t * o2 /* second oid to compare */ )
{
	return *o1 == *o2;
}

/* Hash znode by block number. Used by hash-table macros */
/* Audited by: green(2002.06.15) */
static inline __u32
oid_hash(ln_hash_table *table, const oid_t * o /* oid to hash */ )
{
	return *o & (LNODE_HTABLE_BUCKETS - 1);
}

/* The hash table definition */
#define KMALLOC(size) kmalloc((size), GFP_KERNEL)
#define KFREE(ptr, size) kfree(ptr)
TYPE_SAFE_HASH_DEFINE(ln, lnode, oid_t, h.oid, h.link, oid_hash, oid_eq);
#undef KFREE
#undef KMALLOC

ln_hash_table lnode_htable;
spinlock_t    lnode_guard = SPIN_LOCK_UNLOCKED;


/* true if @required lnode type is @compatible with @set lnode type. If lnode
   types are incompatible, then thread trying to obtain @required type of
   access will wait until all references (lnodes) of the @set type to the file
   system object are released.

   For example, thread trying to manipulate object through VFS (@required type
   is LNODE_INODE) will wait if object is currently manipulated through
   reiser4() call (that is, there are lnodes with type LNODE_LW).

*/
/* Audited by: green(2002.06.15) */
reiser4_internal int
lnode_compatible_type(lnode_type required /* required lnode type */ ,
		      lnode_type set /* lnode type already set */ )
{
	return !((set == LNODE_LW) && (required != LNODE_INODE));
}

/* initialise lnode module for @super. */
/* Audited by: green(2002.06.15) */
reiser4_internal int
lnodes_init(void)
{
	ln_hash_init(&lnode_htable, LNODE_HTABLE_BUCKETS, NULL);
	return 0;
}

/* free lnode resources associated with @super. */
/* Audited by: green(2002.06.15) */
reiser4_internal int
lnodes_done(void)
{
	ln_hash_done(&lnode_htable);
	return 0;
}

/* Acquire handle to file system object.

   First check whether there is already lnode for this oid in a hash table.
   If no---initialise @node and add it into the hash table. If hash table
   already contains lnode with such oid, and incompatible type, wait until
   said lnode is deleted. If compatible lnode is found in the hash table,
   increase its reference counter and return.




*/
/* Audited by: green(2002.06.15) */
reiser4_internal lnode *
lget(                 /*lnode * node ,  lnode to add to the hash table */
     lnode_type type /* lnode type */ , oid_t oid /* objectid */ )
{
	lnode *result;

	//	assert("nikita-1862", node != NULL);
	assert("nikita-1866", lnode_valid_type(type));

	spin_lock(&lnode_guard);
	/* check hash table */
	while ((result = ln_hash_find(&lnode_htable, &oid)) != 0) {
		if (!lnode_compatible_type(type, result->h.type)) {
			int ret;

			/* if lnode is of incompatible type, wait until all
			   incompatible users go away. For example, if we are
			   requesting lnode for VFS access (and our @type is
			   LNODE_INODE), wait until all reiser4() system call
			   manipulations with this object finish.
			*/
			ret = kcond_wait(&result->h.cvar, &lnode_guard, 1);
			if (ret != 0) {
				result = ERR_PTR(ret);
				break;
			}
		} else {
			/* compatible lnode found in the hash table. Just
			   return it. */
			++result->h.ref;
			break;
		}
	}
	if (result == NULL) {
		/* lnode wasn't found in the hash table, initialise @node and
		   add it into hash table. */
		result = ( lnode * ) kmalloc( sizeof( lnode ), GFP_KERNEL);
		xmemset(result, 0, sizeof( lnode ));
		result->h.type = type;
		result->h.oid = oid;
		kcond_init(&result->h.cvar);
		result->h.ref = 1;
		ln_hash_insert(&lnode_htable, result);
	}
	spin_unlock(&lnode_guard);
	return result;
}

/* release reference to file system object */
/* Audited by: green(2002.06.15) */
reiser4_internal void
lput(lnode * node /* lnode to release */ )
{
	assert("nikita-1864", node != NULL);
	assert("nikita-1961", lnode_valid_type(node->h.type));	/* man in
								 * a
								 * space */
	spin_lock(&lnode_guard);
	assert("nikita-1878", ln_hash_find(&lnode_htable, &node->h.oid) == node);
	if (--node->h.ref == 0) {
		ln_hash_remove(&lnode_htable, node);
		kcond_broadcast(&node->h.cvar);
		kfree(node);
	}
	spin_unlock(&lnode_guard);
}

reiser4_internal lnode *
lref(lnode * node)
{
	assert("nikita-3241", node != NULL);
	assert("nikita-3242", lnode_valid_type(node->h.type));

	spin_lock(&lnode_guard);
	++ node->h.ref;
	spin_unlock(&lnode_guard);
	return node;
}

/* true if @node1 and @node2 refer to the same object */
/* Audited by: green(2002.06.15) */
reiser4_internal int
lnode_eq(const lnode * node1 /* first node to compare */ ,
	 const lnode * node2 /* second node to compare */ )
{
	assert("nikita-1921", node1 != NULL);
	assert("nikita-1922", node2 != NULL);	/* Finnegans Wake started */

	if (node1->h.oid != node2->h.oid)
		return 0;
	else if (node1->h.type != node2->h.type)
		return 0;
	else
		return lnode_ops[node1->h.type].eq(node1, node2);
}

/* return key of object behind @node */
/* Audited by: green(2002.06.15) */
reiser4_internal reiser4_key *
lnode_key(const lnode * node /* lnode to query */ ,
	  reiser4_key * result /* result */ )
{
	assert("nikita-1849", node != NULL);
	assert("nikita-1855", lnode_valid_type(node->h.type));
	return lnode_ops[node->h.type].key(node, result);
}

/* return plugins of object behind @node */
/* Audited by: green(2002.06.15) */
reiser4_internal int
get_lnode_plugins(const lnode * node /* lnode to query */ ,
		  plugin_set * area /* result */ )
{
	assert("nikita-1853", node != NULL);
	assert("nikita-1858", lnode_valid_type(node->h.type));
	return lnode_ops[node->h.type].get_plugins(node, area);
}

/* set plugins of object behind @node */
/* Audited by: green(2002.06.15) */
reiser4_internal int
set_lnode_plugins(lnode * node /* lnode to modify */ ,
		  const plugin_set * area /* plugins to install */ )
{
	assert("nikita-1859", node != NULL);
	assert("nikita-1860", lnode_valid_type(node->h.type));
	return lnode_ops[node->h.type].set_plugins(node, area);
}

#if REISER4_DEBUG
/* true if @type is valid lnode type */
/* Audited by: green(2002.06.15) */
static int
lnode_valid_type(lnode_type type /* would-be lnode type */ )
{
	return type < LNODE_NR_TYPES;
}
#endif

/* return key of object behind dentry-based @node */
reiser4_internal reiser4_key *
lnode_dentry_key(const lnode * node /* lnode to query */ ,
		reiser4_key * result /* result */ )
{
	return build_sd_key(node->dentry.dentry->d_inode, result);
}



/* return key of object behind inode-based @node */
/* Audited by: green(2002.06.15) */
static reiser4_key *
lnode_inode_key(const lnode * node /* lnode to query */ ,
		reiser4_key * result /* result */ )
{
	return build_sd_key(node->inode.inode, result);
}

/* return key of object behind lighweight @node */
/* Audited by: green(2002.06.15) */
static reiser4_key *
lnode_lw_key(const lnode * node /* lnode to query */ ,
	     reiser4_key * result /* result */ )
{
	*result = node->lw.key;
	return result;
}

/* compare two inodes */
/* Audited by: green(2002.06.15) */
static int
lnode_inode_eq(const lnode * node1 /* first node to compare */ ,
	       const lnode * node2 /* second node to compare */ )
{
	assert("nikita-1923", node1 != NULL);
	assert("nikita-1924", node2 != NULL);

	assert("nikita-1927", node1->inode.inode != NULL);
	assert("nikita-1928", node2->inode.inode != NULL);

	return (node1->inode.inode == node2->inode.inode);

}

/* compare two lw objects */
/* Audited by: green(2002.06.15) */
static int
lnode_lw_eq(const lnode * node1 UNUSED_ARG	/* first node to
						 * compare */ ,
	    const lnode * node2 UNUSED_ARG	/* second node to
						 * compare */ )
{
	assert("nikita-1925", node1 != NULL);
	assert("nikita-1926", node2 != NULL);

	/* we only get there if oids are equal */
	assert("nikita-1929", node1->h.oid == node2->h.oid);
	assert("nikita-1930", keyeq(&node1->lw.key, &node2->lw.key));
	return 1;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
