/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Inode functions. */

#if !defined( __REISER4_INODE_H__ )
#define __REISER4_INODE_H__

#include "forward.h"
#include "debug.h"
#include "spin_macros.h"
#include "key.h"
#include "kcond.h"
#include "seal.h"
#include "plugin/plugin.h"
#include "plugin/cryptcompress.h"
#include "plugin/plugin_set.h"
#include "plugin/security/perm.h"
#include "plugin/pseudo/pseudo.h"
#include "vfs_ops.h"
#include "jnode.h"

#include <linux/types.h>	/* for __u?? , ino_t */
#include <linux/fs.h>		/* for struct super_block, struct
				 * rw_semaphore, etc  */
#include <linux/spinlock.h>
#include <asm/types.h>

/* reiser4-specific inode flags. They are "transient" and are not
   supposed to be stored on disk. Used to trace "state" of
   inode
*/
typedef enum {
	/* this is light-weight inode, inheriting some state from its
	   parent  */
	REISER4_LIGHT_WEIGHT = 0,
	/* stat data wasn't yet created */
	REISER4_NO_SD = 1,
	/* internal immutable flag. Currently is only used
	    to avoid race condition during file creation.
	    See comment in create_object(). */
	REISER4_IMMUTABLE = 2,
	/* inode was read from storage */
	REISER4_LOADED = 3,
	/* this bit is set for symlinks. inode->u.generic_ip points to target
	   name of symlink. */
	REISER4_GENERIC_PTR_USED = 4,
	/* set if size of stat-data item for this inode is known. If this is
	 * set we can avoid recalculating size of stat-data on each update. */
	REISER4_SDLEN_KNOWN   = 5,
	/* reiser4_inode->crypt points to the crypto stat */
	REISER4_CRYPTO_STAT_LOADED = 6,
	/* reiser4_inode->cluster_shift makes sense */
	REISER4_CLUSTER_KNOWN = 7,
	/* cryptcompress_inode_data points to the secret key */
	REISER4_SECRET_KEY_INSTALLED = 8,
	/* File (possibly) has pages corresponding to the tail items, that
	 * were created by ->readpage. It is set by mmap_unix_file() and
	 * sendfile_unix_file(). This bit is inspected by write_unix_file and
	 * kill-hook of tail items. It is never cleared once set. This bit is
	 * modified and inspected under i_sem. */
	REISER4_HAS_MMAP = 9,
	/* file was partially converted. It's body consists of a mix of tail
	 * and extent items. */
	REISER4_PART_CONV = 10,
} reiser4_file_plugin_flags;

/* state associated with each inode.
   reiser4 inode.

   NOTE-NIKITA In 2.5 kernels it is not necessary that all file-system inodes
   be of the same size. File-system allocates inodes by itself through
   s_op->allocate_inode() method. So, it is possible to adjust size of inode
   at the time of its creation.


   Invariants involving parts of this data-type:

      [inode->eflushed]

*/

typedef struct reiser4_inode reiser4_inode;
/* return pointer to reiser4-specific part of inode */
static inline reiser4_inode *
reiser4_inode_data(const struct inode * inode /* inode queried */);

#include "plugin/file/file.h"

#if BITS_PER_LONG == 64

#define REISER4_INO_IS_OID (1)
typedef struct {;
} oid_hi_t;

/* BITS_PER_LONG == 64 */
#else

#define REISER4_INO_IS_OID (0)
typedef __u32 oid_hi_t;

/* BITS_PER_LONG == 64 */
#endif

struct reiser4_inode {
	/* spin lock protecting fields of this structure. */
	reiser4_spin_data guard;
	/* object plugins */
	plugin_set *pset;
	/* plugins set for inheritance */
	plugin_set *hset;
	/* high 32 bits of object id */
	oid_hi_t oid_hi;
	/* seal for stat-data */
	seal_t sd_seal;
	/* locality id for this file */
	oid_t locality_id;
#if REISER4_LARGE_KEY
	__u64 ordering;
#endif
	/* coord of stat-data in sealed node */
	coord_t sd_coord;
	/* bit-mask of stat-data extentions used by this file */
	__u64 extmask;
	/* bitmask of non-default plugins for this inode */
	__u16 plugin_mask;
	/* cluster parameter for crypto and compression */
	__u8 cluster_shift;
	/* secret key parameter for crypto */
	crypto_stat_t *crypt;

	union {
		readdir_list_head readdir_list;
		struct list_head not_used;
	} lists;
	/* per-inode flags. Filled by values of reiser4_file_plugin_flags */
	unsigned long flags;
	union {
		/* fields specific to unix_file plugin */
		unix_file_info_t unix_file_info;
		/* fields specific to cryptcompress plugin */
		cryptcompress_info_t cryptcompress_info;
		/* fields specific to pseudo file plugin */
		pseudo_info_t pseudo_info;
	} file_plugin_data;
	struct rw_semaphore coc_sem; /* filemap_nopage takes it for read, copy_on_capture - for write. Under this it
			       tries to unmap page for which it is called. This prevents process from using page which
			       was copied on capture */
	/* tree of jnodes. Jnodes in this tree are distinguished by radix tree
	   tags */
	struct radix_tree_root jnodes_tree;
#if REISER4_DEBUG
	/* list of jnodes. Number of jnodes in this list is the above jnodes field */
	inode_jnodes_list_head jnodes_list;

	/* numbers of eflushed jnodes of each type in the above tree */
	int anonymous_eflushed;
	int captured_eflushed;
	/* number of unformatted node jnodes of this file in jnode hash table */
	unsigned long nr_jnodes;
#endif

	/* block number of virtual root for this object. See comment above
	 * fs/reiser4/search.c:handle_vroot() */
	reiser4_block_nr vroot;
	struct semaphore loading;
};


#define I_JNODES (512)	/* inode state bit. Set when in hash table there are more than 0 jnodes of unformatted nodes of
			   an inode */

typedef struct reiser4_inode_object {
	/* private part */
	reiser4_inode p;
	/* generic fields not specific to reiser4, but used by VFS */
	struct inode vfs_inode;
} reiser4_inode_object;

/* return pointer to the reiser4 specific portion of @inode */
static inline reiser4_inode *
reiser4_inode_data(const struct inode * inode /* inode queried */)
{
	assert("nikita-254", inode != NULL);
	return &container_of(inode, reiser4_inode_object, vfs_inode)->p;
}

static inline struct inode *
inode_by_reiser4_inode(const reiser4_inode *r4_inode /* inode queried */)
{
       return &container_of(r4_inode, reiser4_inode_object, p)->vfs_inode;
}

/*
 * reiser4 inodes are identified by 64bit object-id (oid_t), but in struct
 * inode ->i_ino field is of type ino_t (long) that can be either 32 or 64
 * bits.
 *
 * If ->i_ino is 32 bits we store remaining 32 bits in reiser4 specific part
 * of inode, otherwise whole oid is stored in i_ino.
 *
 * Wrappers below ([sg]et_inode_oid()) are used to hide this difference.
 */

#define OID_HI_SHIFT (sizeof(ino_t) * 8)

#if REISER4_INO_IS_OID

static inline oid_t
get_inode_oid(const struct inode *inode)
{
	return inode->i_ino;
}

static inline void
set_inode_oid(struct inode *inode, oid_t oid)
{
	inode->i_ino = oid;
}

/* REISER4_INO_IS_OID */
#else

static inline oid_t
get_inode_oid(const struct inode *inode)
{
	return
		((__u64)reiser4_inode_data(inode)->oid_hi << OID_HI_SHIFT) |
		inode->i_ino;
}

static inline void
set_inode_oid(struct inode *inode, oid_t oid)
{
	assert("nikita-2519", inode != NULL);
	inode->i_ino = (ino_t)(oid);
	reiser4_inode_data(inode)->oid_hi = (oid) >> OID_HI_SHIFT;
	assert("nikita-2521", get_inode_oid(inode) == (oid));
}

/* REISER4_INO_IS_OID */
#endif

static inline oid_t
get_inode_locality(const struct inode *inode)
{
	return reiser4_inode_data(inode)->locality_id;
}

#if REISER4_LARGE_KEY
static inline __u64 get_inode_ordering(const struct inode *inode)
{
	return reiser4_inode_data(inode)->ordering;
}

static inline void set_inode_ordering(const struct inode *inode, __u64 ordering)
{
	reiser4_inode_data(inode)->ordering = ordering;
}

#else

#define get_inode_ordering(inode) (0)
#define set_inode_ordering(inode, val) noop

#endif

/* return inode in which @uf_info is embedded */
static inline struct inode *
unix_file_info_to_inode(const unix_file_info_t *uf_info)
{
	return &container_of(uf_info, reiser4_inode_object,
			     p.file_plugin_data.unix_file_info)->vfs_inode;
}

/* ordering predicate for inode spin lock: only jnode lock can be held */
#define spin_ordering_pred_inode_object(inode)			\
	( lock_counters() -> rw_locked_dk == 0 ) &&		\
	( lock_counters() -> rw_locked_tree == 0 ) &&		\
	( lock_counters() -> spin_locked_txnh == 0 ) &&		\
	( lock_counters() -> rw_locked_zlock == 0 ) &&	\
	( lock_counters() -> spin_locked_jnode == 0 ) &&	\
	( lock_counters() -> spin_locked_atom == 0 ) &&		\
	( lock_counters() -> spin_locked_ktxnmgrd == 0 ) &&	\
	( lock_counters() -> spin_locked_txnmgr == 0 )

SPIN_LOCK_FUNCTIONS(inode_object, reiser4_inode, guard);

extern ino_t oid_to_ino(oid_t oid) __attribute__ ((const));
extern ino_t oid_to_uino(oid_t oid) __attribute__ ((const));

extern reiser4_tree *tree_by_inode(const struct inode *inode);

#if REISER4_DEBUG
extern void inode_invariant(const struct inode *inode);
#else
#define inode_invariant(inode) noop
#endif

#define spin_lock_inode(inode)			\
({						\
	LOCK_INODE(reiser4_inode_data(inode));	\
	inode_invariant(inode);			\
})

#define spin_unlock_inode(inode)			\
({							\
	inode_invariant(inode);				\
	UNLOCK_INODE(reiser4_inode_data(inode));	\
})

extern znode *inode_get_vroot(struct inode *inode);
extern void   inode_set_vroot(struct inode *inode, znode *vroot);
extern void   inode_clean_vroot(struct inode *inode);

extern int reiser4_max_filename_len(const struct inode *inode);
extern int max_hash_collisions(const struct inode *dir);
extern void reiser4_unlock_inode(struct inode *inode);
extern int is_reiser4_inode(const struct inode *inode);
extern int setup_inode_ops(struct inode *inode, reiser4_object_create_data *);
extern struct inode *reiser4_iget(struct super_block *super, const reiser4_key * key, int silent);
extern void reiser4_iget_complete (struct inode * inode);
extern int reiser4_inode_find_actor(struct inode *inode, void *opaque);
extern int get_reiser4_inode_by_key (struct inode **, const reiser4_key *);


extern void inode_set_flag(struct inode *inode, reiser4_file_plugin_flags f);
extern void inode_clr_flag(struct inode *inode, reiser4_file_plugin_flags f);
extern int inode_get_flag(const struct inode *inode, reiser4_file_plugin_flags f);

/*  has inode been initialized? */
static inline int
is_inode_loaded(const struct inode *inode /* inode queried */ )
{
	assert("nikita-1120", inode != NULL);
	return inode_get_flag(inode, REISER4_LOADED);
}

extern file_plugin *inode_file_plugin(const struct inode *inode);
extern dir_plugin *inode_dir_plugin(const struct inode *inode);
extern perm_plugin *inode_perm_plugin(const struct inode *inode);
extern formatting_plugin *inode_formatting_plugin(const struct inode *inode);
extern hash_plugin *inode_hash_plugin(const struct inode *inode);
extern fibration_plugin *inode_fibration_plugin(const struct inode *inode);
extern crypto_plugin *inode_crypto_plugin(const struct inode *inode);
extern digest_plugin *inode_digest_plugin(const struct inode *inode);
extern compression_plugin *inode_compression_plugin(const struct inode *inode);
extern item_plugin *inode_sd_plugin(const struct inode *inode);
extern item_plugin *inode_dir_item_plugin(const struct inode *inode);

extern void inode_set_plugin(struct inode *inode,
			     reiser4_plugin * plug, pset_member memb);
extern void reiser4_make_bad_inode(struct inode *inode);

extern void inode_set_extension(struct inode *inode, sd_ext_bits ext);
extern void inode_check_scale(struct inode *inode, __u64 old, __u64 new);

/*
 * update field @field in inode @i to contain value @value.
 */
#define INODE_SET_FIELD(i, field, value)		\
({							\
	struct inode *__i;				\
	typeof(value) __v;				\
							\
	__i = (i);					\
	__v = (value);					\
	inode_check_scale(__i, __i->field, __v);	\
	__i->field = __v;				\
})

#define INODE_INC_FIELD(i, field)				\
({								\
	struct inode *__i;					\
								\
	__i = (i);						\
	inode_check_scale(__i, __i->field, __i->field + 1);	\
	++ __i->field;						\
})

#define INODE_DEC_FIELD(i, field)				\
({								\
	struct inode *__i;					\
								\
	__i = (i);						\
	inode_check_scale(__i, __i->field, __i->field - 1);	\
	-- __i->field;						\
})

/* See comment before readdir_common() for description. */
static inline readdir_list_head *
get_readdir_list(const struct inode *inode)
{
	return &reiser4_inode_data(inode)->lists.readdir_list;
}

extern void init_inode_ordering(struct inode *inode,
				reiser4_object_create_data *crd, int create);

static inline struct radix_tree_root *
jnode_tree_by_inode(struct inode *inode)
{
	return &reiser4_inode_data(inode)->jnodes_tree;
}

static inline struct radix_tree_root *
jnode_tree_by_reiser4_inode(reiser4_inode *r4_inode)
{
	return &r4_inode->jnodes_tree;
}

#if REISER4_DEBUG_OUTPUT
extern void print_inode(const char *prefix, const struct inode *i);
#else
#define print_inode(p, i) noop
#endif

/* __REISER4_INODE_H__ */
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
