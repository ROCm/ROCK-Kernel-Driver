/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Super-block functions. See super.c for details. */

#if !defined( __REISER4_SUPER_H__ )
#define __REISER4_SUPER_H__

#include "forward.h"
#include "debug.h"
#include "tree.h"
#include "context.h"
#include "log.h"
#include "lnode.h"
#include "entd.h"
#include "plugin/plugin.h"
#include "prof.h"
#include "wander.h"

#include "plugin/space/space_allocator.h"

#include "plugin/disk_format/disk_format40.h"
#include "plugin/security/perm.h"
#include "plugin/dir/dir.h"

#include "emergency_flush.h"

#include <linux/spinlock.h>
#include <linux/types.h>	/* for __u??, etc.  */
#include <linux/fs.h>		/* for struct super_block, etc.  */
#include <linux/list.h>		/* for struct list_head */
#include <linux/kobject.h>      /* for kobject */

/*
 * Flush algorithms parameters.
 */
typedef struct {
	unsigned relocate_threshold;
	unsigned relocate_distance;
	unsigned written_threshold;
	unsigned scan_maxnodes;
} flush_params;

typedef enum {
	/* True if this file system doesn't support hard-links (multiple
	   names) for directories: this is default UNIX behavior.

	   If hard-links on directoires are not allowed, file system is
	   Acyclic Directed Graph (modulo dot, and dotdot, of course).

	   This is used by reiser4_link().
	*/
	REISER4_ADG = 0,
	/* set if all nodes in internal tree have the same node layout plugin.
	   If so, znode_guess_plugin() will return tree->node_plugin in stead
	   of guessing plugin by plugin id stored in the node.
	*/
	REISER4_ONE_NODE_PLUGIN = 1,
	/* if set, bsd gid assignment is supported. */
	REISER4_BSD_GID = 2,
	/* [mac]_time are 32 bit in inode */
	REISER4_32_BIT_TIMES = 3,
	/* allow concurrent flushes */
	REISER4_MTFLUSH = 4,
	/* disable support for pseudo files. Don't treat regular files as
	 * directories. */
	REISER4_NO_PSEUDO = 5,
	/* load all bitmap blocks at mount time */
	REISER4_DONT_LOAD_BITMAP = 6
} reiser4_fs_flag;

#if REISER4_STATS

/*
 * Object to export per-level stat-counters through sysfs. See stats.[ch] and
 * kattr.[ch]
 */
typedef struct reiser4_level_stats_kobj {
	struct fs_kobject kobj;   /* file system kobject exported for each
				   * level */
	int level;                /* tree level */
} reiser4_level_stats_kobj;

#endif

/*
 * VFS related operation vectors.
 *
 * Usually file system has one instance of those, but in reiser4 we sometimes
 * want to be able to modify vectors on per-mount basis. For example, reiser4
 * needs ->open method to handle pseudo files correctly, but if file system is
 * mounted with "nopseudo" mount option, it's better to have ->open set to
 * NULL, as this makes sys_open() a little bit more efficient.
 *
 */
typedef struct object_ops {
	struct super_operations         super;
	struct file_operations          file;
	struct dentry_operations        dentry;
	struct address_space_operations as;

	struct inode_operations         regular;
	struct inode_operations         dir;
	struct inode_operations         symlink;
	struct inode_operations		special;

	struct export_operations        export;
} object_ops;

/* reiser4-specific part of super block

   Locking

   Fields immutable after mount:

    ->oid*
    ->space*
    ->default_[ug]id
    ->mkfs_id
    ->trace_flags
    ->debug_flags
    ->fs_flags
    ->df_plug
    ->optimal_io_size
    ->plug
    ->flush
    ->u (bad name)
    ->txnmgr
    ->ra_params
    ->fsuid
    ->journal_header
    ->journal_footer

   Fields protected by ->lnode_guard

    ->lnode_htable

   Fields protected by per-super block spin lock

    ->block_count
    ->blocks_used
    ->blocks_free
    ->blocks_free_committed
    ->blocks_grabbed
    ->blocks_fake_allocated_unformatted
    ->blocks_fake_allocated
    ->blocks_flush_reserved
    ->eflushed
    ->blocknr_hint_default

   After journal replaying during mount,

    ->last_committed_tx

   is protected by ->tmgr.commit_semaphore

   Invariants involving this data-type:

      [sb-block-counts]
      [sb-grabbed]
      [sb-fake-allocated]
*/
struct reiser4_super_info_data {
	/* guard spinlock which protects reiser4 super
	   block fields (currently blocks_free,
	   blocks_free_committed)
	*/
	reiser4_spin_data guard;

	/*
	 * object id manager
	 */
	/* next oid that will be returned by oid_allocate() */
	oid_t next_to_use;
	/* total number of used oids */
	oid_t oids_in_use;

	/* space manager plugin */
	reiser4_space_allocator space_allocator;

	/* reiser4 internal tree */
	reiser4_tree tree;

	/* default user id used for light-weight files without their own
	   stat-data. */
	uid_t default_uid;

	/* default group id used for light-weight files without their own
	   stat-data. */
	gid_t default_gid;

	/* mkfs identifier generated at mkfs time. */
	__u32 mkfs_id;
	/* amount of blocks in a file system */
	__u64 block_count;

	/* inviolable reserve */
	__u64 blocks_reserved;

	/* amount of blocks used by file system data and meta-data. */
	__u64 blocks_used;

	/* amount of free blocks. This is "working" free blocks counter. It is
	   like "working" bitmap, please see block_alloc.c for description. */
	__u64 blocks_free;

	/* free block count for fs committed state. This is "commit" version
	   of free block counter. */
	__u64 blocks_free_committed;

	/* number of blocks reserved for further allocation, for all threads. */
	__u64 blocks_grabbed;

	/* number of fake allocated unformatted blocks in tree. */
	__u64 blocks_fake_allocated_unformatted;

	/* number of fake allocated formatted blocks in tree. */
	__u64 blocks_fake_allocated;

	/* number of blocks reserved for flush operations. */
	__u64 blocks_flush_reserved;

        /* number of blocks reserved for cluster operations. */
	__u64 blocks_clustered;

	/* unique file-system identifier */
	/* does this conform to Andreas Dilger UUID stuff? */
	__u32 fsuid;

	/* per-fs tracing flags. Use reiser4_trace_flags enum to set
	   bits in it. */
	__u32 trace_flags;

	/* per-fs log flags. Use reiser4_log_flags enum to set
	   bits in it. */
	__u32 log_flags;
	__u32 oid_to_log;

	/* file where tracing goes (if enabled). */
	reiser4_log_file log_file;

	/* per-fs debugging flags. This is bitmask populated from
	   reiser4_debug_flags enum. */
	__u32 debug_flags;

	/* super block flags */

	/* file-system wide flags. See reiser4_fs_flag enum */
	unsigned long fs_flags;

	/* transaction manager */
	txn_mgr tmgr;

	/* ent thread */
	entd_context entd;

	/* fake inode used to bind formatted nodes */
	struct inode *fake;
	/* inode used to bind bitmaps (and journal heads) */
	struct inode *bitmap;
	/* inode used to bind copied on capture nodes */
	struct inode *cc;

	/* disk layout plugin */
	disk_format_plugin *df_plug;

	/* disk layout specific part of reiser4 super info data */
	union {
		format40_super_info format40;
	} u;

	/*
	 * value we return in st_blksize on stat(2).
	 */
	unsigned long optimal_io_size;

	/* parameters for the flush algorithm */
	flush_params flush;

	/* see emergency_flush.c for details */
	reiser4_spin_data eflush_guard;
	/* number of emergency flushed nodes */
	int               eflushed;
#if REISER4_USE_EFLUSH
	/* hash table used by emergency flush. Protected by ->eflush_guard */
	ef_hash_table     efhash_table;
#endif
	/* pointers to jnodes for journal header and footer */
	jnode *journal_header;
	jnode *journal_footer;

	journal_location jloc;

	/* head block number of last committed transaction */
	__u64 last_committed_tx;

	/* we remember last written location for using as a hint for
	   new block allocation */
	__u64 blocknr_hint_default;

	/* committed number of files (oid allocator state variable ) */
	__u64 nr_files_committed;

	ra_params_t ra_params;

	/* A semaphore for serializing cut tree operation if
	   out-of-free-space: the only one cut_tree thread is allowed to grab
	   space from reserved area (it is 5% of disk space) */
	struct semaphore delete_sema;
	/* task owning ->delete_sema */
	struct task_struct *delete_sema_owner;

	/* serialize semaphore */
	struct semaphore flush_sema;

	/* Diskmap's blocknumber */
	__u64 diskmap_block;

	/* What to do in case of error */
	int onerror;

	/* operations for objects on this file system */
	object_ops ops;

	/* dir_cursor_info see plugin/dir/dir.[ch] for more details */
	d_cursor_info d_info;

#if REISER4_USE_SYSFS
	/* kobject representing this file system. It is visible as
	 * /sys/fs/reiser4/<devname>. All other kobjects for this file system
	 * (statistical counters, tunables, etc.) are below it in sysfs
	 * hierarchy. */
	struct fs_kobject kobj;
#endif
#if REISER4_STATS
	/* Statistical counters. reiser4_stat is empty data-type unless
	   REISER4_STATS is set. See stats.[ch] for details. */
	reiser4_stat *stats;
	/* kobject for statistical counters. Visible as
	 * /sys/fs/reiser4/<devname>/stats */
	struct fs_kobject stats_kobj;
	/* kobjects for per-level statistical counters. Each level is visible
	   as /sys/fs/reiser4/<devname>/stats-NN */
	reiser4_level_stats_kobj level[REISER4_MAX_ZTREE_HEIGHT];
#endif
#ifdef CONFIG_REISER4_BADBLOCKS
	/* Alternative master superblock offset (in bytes) */
	unsigned long altsuper;
#endif
#if REISER4_LOG
	/* last disk block IO was performed against by this file system. Used
	 * by tree tracing code to track seeks. */
	reiser4_block_nr last_touched;
#endif
#if REISER4_DEBUG
	/* minimum used blocks value (includes super blocks, bitmap blocks and
	 * other fs reserved areas), depends on fs format and fs size. */
	__u64 min_blocks_used;
	/* amount of space allocated by kmalloc. For debugging. */
	int kmalloc_allocated;

	/*
	 * when debugging is on, all jnodes (including znodes, bitmaps, etc.)
	 * are kept on a list anchored at sbinfo->all_jnodes. This list is
	 * protected by sbinfo->all_guard spin lock. This lock should be taken
	 * with _irq modifier, because it is also modified from interrupt
	 * contexts (by RCU).
	 */

	spinlock_t all_guard;
	/* list of all jnodes */
	struct list_head all_jnodes;

	/*XXX debugging code */
	__u64 eflushed_unformatted; /* number of eflushed unformatted nodes */
	__u64 unalloc_extent_pointers; /* number of unallocated extent pointers in the tree */
#endif
	struct repacker * repacker;
	struct page * status_page;
	struct bio * status_bio;
};



extern reiser4_super_info_data *get_super_private_nocheck(const struct
							  super_block *super);

extern struct super_operations reiser4_super_operations;

/* Return reiser4-specific part of super block */
static inline reiser4_super_info_data *
get_super_private(const struct super_block * super)
{
	assert("nikita-447", super != NULL);

	return (reiser4_super_info_data *) super->s_fs_info;
}

/* "Current" super-block: main super block used during current system
   call. Reference to this super block is stored in reiser4_context. */
static inline struct super_block *
reiser4_get_current_sb(void)
{
	return get_current_context()->super;
}

/* Reiser4-specific part of "current" super-block: main super block used
   during current system call. Reference to this super block is stored in
   reiser4_context. */
static inline reiser4_super_info_data *
get_current_super_private(void)
{
	return get_super_private(reiser4_get_current_sb());
}

static inline ra_params_t *
get_current_super_ra_params(void)
{
	return &(get_current_super_private()->ra_params);
}

/*
 * true, if file system on @super is read-only
 */
static inline int rofs_super(struct super_block *super)
{
	return super->s_flags & MS_RDONLY;
}

/*
 * true, if @tree represents read-only file system
 */
static inline int rofs_tree(reiser4_tree *tree)
{
	return rofs_super(tree->super);
}

/*
 * true, if file system where @inode lives on, is read-only
 */
static inline int rofs_inode(struct inode *inode)
{
	return rofs_super(inode->i_sb);
}

/*
 * true, if file system where @node lives on, is read-only
 */
static inline int rofs_jnode(jnode *node)
{
	return rofs_tree(jnode_get_tree(node));
}

extern __u64 reiser4_current_block_count(void);

extern void build_object_ops(struct super_block *super, object_ops *ops);

#define REISER4_SUPER_MAGIC 0x52345362 	/* (*(__u32 *)"R4Sb"); */

#define spin_ordering_pred_super(private) (1)
SPIN_LOCK_FUNCTIONS(super, reiser4_super_info_data, guard);

#define spin_ordering_pred_super_eflush(private) (1)
SPIN_LOCK_FUNCTIONS(super_eflush, reiser4_super_info_data, eflush_guard);

/*
 * lock reiser4-specific part of super block
 */
static inline void reiser4_spin_lock_sb(reiser4_super_info_data *sbinfo)
{
	spin_lock_super(sbinfo);
}

/*
 * unlock reiser4-specific part of super block
 */
static inline void reiser4_spin_unlock_sb(reiser4_super_info_data *sbinfo)
{
	spin_unlock_super(sbinfo);
}

/*
 * lock emergency flush data-structures for super block @s
 */
static inline void spin_lock_eflush(const struct super_block * s)
{
	reiser4_super_info_data * sbinfo = get_super_private (s);
	spin_lock_super_eflush(sbinfo);
}

/*
 * unlock emergency flush data-structures for super block @s
 */
static inline void spin_unlock_eflush(const struct super_block * s)
{
	reiser4_super_info_data * sbinfo = get_super_private (s);
	spin_unlock_super_eflush(sbinfo);
}


extern __u64 flush_reserved        ( const struct super_block*);
extern void  set_flush_reserved    ( const struct super_block*, __u64 nr );
extern int reiser4_is_set(const struct super_block *super, reiser4_fs_flag f);
extern long statfs_type(const struct super_block *super);
extern int reiser4_blksize(const struct super_block *super);
extern __u64 reiser4_block_count(const struct super_block *super);
extern void reiser4_set_block_count(const struct super_block *super, __u64 nr);
extern __u64 reiser4_data_blocks(const struct super_block *super);
extern void reiser4_set_data_blocks(const struct super_block *super, __u64 nr);
extern __u64 reiser4_free_blocks(const struct super_block *super);
extern void reiser4_set_free_blocks(const struct super_block *super, __u64 nr);
extern void reiser4_inc_free_blocks(const struct super_block *super);
extern __u32 reiser4_mkfs_id(const struct super_block *super);
extern void reiser4_set_mkfs_id(const struct super_block *super, __u32 id);

extern __u64 reiser4_free_committed_blocks(const struct super_block *super);
extern void reiser4_set_free_committed_blocks(const struct super_block *super, __u64 nr);

extern __u64 reiser4_grabbed_blocks(const struct super_block *);
extern void reiser4_set_grabbed_blocks(const struct super_block *, __u64 nr);
extern __u64 reiser4_fake_allocated(const struct super_block *);
extern void reiser4_set_fake_allocated(const struct super_block *, __u64 nr);
extern __u64 reiser4_fake_allocated_unformatted(const struct super_block *);
extern void reiser4_set_fake_allocated_unformatted(const struct super_block *, __u64 nr);
extern __u64 reiser4_clustered_blocks(const struct super_block *);
extern void reiser4_set_clustered_blocks(const struct super_block *, __u64 nr);

extern long reiser4_reserved_blocks(const struct super_block *super, uid_t uid, gid_t gid);

extern reiser4_space_allocator *get_space_allocator(const struct super_block
						    *super);
extern reiser4_oid_allocator *get_oid_allocator(const struct super_block
						*super);
extern struct inode *get_super_fake(const struct super_block *super);
extern struct inode *get_cc_fake(const struct super_block *super);
extern reiser4_tree *get_tree(const struct super_block *super);
extern int is_reiser4_super(const struct super_block *super);

extern int reiser4_blocknr_is_sane(const reiser4_block_nr *blk);
extern int reiser4_blocknr_is_sane_for(const struct super_block *super,
				       const reiser4_block_nr *blk);

/* Maximal possible object id. */
#define  ABSOLUTE_MAX_OID ((oid_t)~0)

#define OIDS_RESERVED  ( 1 << 16 )
int oid_init_allocator(struct super_block *, oid_t nr_files, oid_t next);
oid_t oid_allocate(struct super_block *);
int oid_release(struct super_block *, oid_t);
oid_t oid_next(const struct super_block *);
void oid_count_allocated(void);
void oid_count_released(void);
long oids_used(const struct super_block *);
long oids_free(const struct super_block *);


#if REISER4_DEBUG_OUTPUT
void print_fs_info(const char *prefix, const struct super_block *);
#else
#define print_fs_info(p,s) noop
#endif

#if REISER4_DEBUG

void inc_unalloc_unfm_ptr(void);
void dec_unalloc_unfm_ptrs(int nr);
void inc_unfm_ef(void);
void dec_unfm_ef(void);

#else

#define inc_unalloc_unfm_ptr() noop
#define dec_unalloc_unfm_ptrs(nr) noop
#define inc_unfm_ef() noop
#define dec_unfm_ef() noop

#endif


/* __REISER4_SUPER_H__ */
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
