/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Statistics gathering. See stats.c for comments. */

#if !defined( __FS_REISER4_STATS_H__ )
#define __FS_REISER4_STATS_H__

#include "forward.h"
#include "reiser4.h"
#include "debug.h"
#include "statcnt.h"

/* for __u?? types */
#include <linux/types.h>
/* for struct super_block, etc */
#include <linux/fs.h>
/* for in_interrupt() */
#include <asm/hardirq.h>

#include <linux/sched.h>

#if REISER4_STATS

/* following macros update counters from &reiser4_stat below, which
   see */

#define ON_STATS(e) e
/* statistics gathering features. */

#define REISER4_STATS_STRICT (0)

/* statistical counters collected on each level of internal tree */
typedef struct reiser4_level_statistics {
	/* carries restarted due to deadlock avoidance algorithm */
	statcnt_t carry_restart;
	/* carries performed */
	statcnt_t carry_done;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it already in a carry set. */
	statcnt_t carry_left_in_carry;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it already in a memory. */
	statcnt_t carry_left_in_cache;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it is not in a memory. */
	statcnt_t carry_left_missed;
	/* how many times carry, trying to find left neighbor of a given node,
	   found that left neighbor either doesn't exist (we are at the left
	   border of the tree already), or that there is extent on the left.
	*/
	statcnt_t carry_left_not_avail;
	/* how many times carry, trying to find left neighbor of a given node,
	   gave this up to avoid deadlock */
	statcnt_t carry_left_refuse;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it already in a carry set. */
	statcnt_t carry_right_in_carry;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it already in a memory. */
	statcnt_t carry_right_in_cache;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it is not in a memory. */
	statcnt_t carry_right_missed;
	/* how many times carry, trying to find right neighbor of a given
	   node, found that right neighbor either doesn't exist (we are at the
	   right border of the tree already), or that there is extent on the
	   right.
	*/
	statcnt_t carry_right_not_avail;
	/* how many times insertion has to look into the left neighbor,
	   searching for the free space. */
	statcnt_t insert_looking_left;
	/* how many times insertion has to look into the right neighbor,
	   searching for the free space. */
	statcnt_t insert_looking_right;
	/* how many times insertion has to allocate new node, searching for
	   the free space. */
	statcnt_t insert_alloc_new;
	/* how many times insertion has to allocate several new nodes in a
	   row, searching for the free space. */
	statcnt_t insert_alloc_many;
	/* how many insertions were performed by carry. */
	statcnt_t insert;
	/* how many deletions were performed by carry. */
	statcnt_t delete;
	/* how many cuts were performed by carry. */
	statcnt_t cut;
	/* how many pastes (insertions into existing items) were performed by
	   carry. */
	statcnt_t paste;
	/* how many extent insertions were done by carry. */
	statcnt_t extent;
	/* how many paste operations were restarted as insert. */
	statcnt_t paste_restarted;
	/* how many updates of delimiting keys were performed by carry. */
	statcnt_t update;
	/* how many times carry notified parent node about updates in its
	   child. */
	statcnt_t modify;
	/* how many times node was found reparented at the time when its
	   parent has to be updated. */
	statcnt_t half_split_race;
	/* how many times new node was inserted into sibling list after
	   concurrent balancing modified right delimiting key if its left
	   neighbor.
	*/
	statcnt_t dk_vs_create_race;
	/* how many times insert or paste ultimately went into node different
	   from original target */
	statcnt_t track_lh;
	/* how many times sibling lookup required getting that high in a
	   tree */
	statcnt_t sibling_search;
	/* key was moved out of node while thread was waiting for the lock */
	statcnt_t cbk_key_moved;
	/* node was moved out of tree while thread was waiting for the lock */
	statcnt_t cbk_met_ghost;
	/* how many times vroot ("virtual root") optimization was used during
	 * tree lookup */
	statcnt_t object_lookup_start;
	struct {
		/* calls to jload() */
		statcnt_t jload;
		/* calls to jload() that found jnode already loaded */
		statcnt_t jload_already;
		/* calls to jload() that found page already in memory */
		statcnt_t jload_page;
		/* calls to jload() that found jnode with asynchronous io
		 * started */
		statcnt_t jload_async;
		/* calls to jload() that actually had to read data */
		statcnt_t jload_read;
		/* calls to jput() */
		statcnt_t jput;
		/* calls to jput() that released last reference */
		statcnt_t jputlast;
	} jnode;
	struct {
		/* calls to lock_znode() */
		statcnt_t lock;
		/* number of times loop inside lock_znode() was executed */
		statcnt_t lock_iteration;
		/* calls to lock_neighbor() */
		statcnt_t lock_neighbor;
		/* number of times loop inside lock_neighbor() was executed */
		statcnt_t lock_neighbor_iteration;
		/* read locks taken */
		statcnt_t lock_read;
		/* write locks taken */
		statcnt_t lock_write;
		/* low priority locks taken */
		statcnt_t lock_lopri;
		/* high priority locks taken */
		statcnt_t lock_hipri;
		/* how many requests for znode long term lock couldn't succeed
		 * immediately. */
		statcnt_t lock_contented;
		/* how many requests for znode long term lock managed to
		 * succeed immediately. */
		statcnt_t lock_uncontented;
		/* attempt to acquire a lock failed, because target node was
		 * dying */
		statcnt_t lock_dying;
		/* lock wasn't immediately available, due to incompatible lock
		 * mode */
		statcnt_t lock_cannot_lock;
		/* lock was immediately available (i.e., without wait) */
		statcnt_t lock_can_lock;
		/* no node capture was necessary when acquiring a lock */
		statcnt_t lock_no_capture;
		/* number of unlocks */
		statcnt_t unlock;
		/* number of times unlock decided to wake up sleeping
		 * requestors */
		statcnt_t wakeup;
		/* number of times requestors were actually found during wake
		 * up */
		statcnt_t wakeup_found;
		/* number of read-mode requestors found */
		statcnt_t wakeup_found_read;
		/* number of requestor queue items scanned during wake-up
		 * processing */
		statcnt_t wakeup_scan;
		/* number of requestors bundled into convoys */
		statcnt_t wakeup_convoy;
	} znode;
	struct {
		/* node lookup stats */
		struct {
			/* ->lookup() calls */
			statcnt_t calls;
			/* items in all nodes */
			statcnt_t items;
			/* "hops" of binary search */
			statcnt_t binary;
			/* iterations of sequential search */
			statcnt_t seq;
			/* how many times key sought for was found */
			statcnt_t found;
			/* average position where key was found */
			statcnt_t pos;
			/* average position where key was found relative to
			 * total number of items */
			statcnt_t posrelative;
			/* number of times key was found in the same position
			 * as in the previous lookup in this node */
			statcnt_t samepos;
			/* number of times key was found in the next position
			 * relative to the previous lookup in this node */
			statcnt_t nextpos;
		} lookup;
	} node;
	struct {
		/* reiser4_releasepage() stats */
		struct {
			/* for how many pages on this level ->releasepage()
			 * was called. */
			statcnt_t try;
			/* how many pages were released on this level */
			statcnt_t ok;
			/*
			 * how many times we failed to release a page,
			 * because...
			 */
			/* jnode pinned it in memory */
			statcnt_t loaded;
			/* it's coced page */
			statcnt_t copy;
			/* it has fake block number */
			statcnt_t fake;
			/* it is dirty */
			statcnt_t dirty;
			/* it is in the overwrite set */
			statcnt_t ovrwr;
			/* it is under writeback */
			statcnt_t writeback;
			/* it's anonymous page, and jnode is not yet captured
			 * into atom. */
			statcnt_t keepme;
			/* it's bitmap */
			statcnt_t bitmap;

			/* emergency flush was performed on this page/jnode,
			 * so it's ok to release */
			statcnt_t eflushed;
		} release;
		/* emergency flush statistics */
		struct {
			/* how many times emergency flush was invoked on this
			 * level */
			statcnt_t called;
			/* eflush was successful */
			statcnt_t ok;
			/* jnode ceased to be flushable after lock release */
			statcnt_t nolonger;
			/* new block number was needed for eflush */
			statcnt_t needs_block;
			/*
			 * eflush failed, because...
			 */
			/* jnode is loaded */
			statcnt_t loaded;
			/* jnode is in the flush queue */
			statcnt_t queued;
			/* jnode is protected (JNODE_PROTECTED bit is on) */
			statcnt_t protected;
			/* jnode heard banshee already */
			statcnt_t heard_banshee;
			/* jnode has no page */
			statcnt_t nopage;
			/* jnode is under writeback */
			statcnt_t writeback;
			/* jnode is bitmap */
			statcnt_t bitmap;
			/* jnode is crypto-compress cluster */
			statcnt_t clustered;
			/* jnode is already eflushed */
			statcnt_t eflushed;
		} eflush;
	} vm;
	/*
	 * non zero, if there is some other non-zero counter at this tree
	 * level. Used to suppress processing of higher tree levels, that
	 * don't exist on the underlying file system.
	 */
	statcnt_t total_hits_at_level;
	/* total time (in jiffies) threads sleep for the longterm locks on
	 * this level */
	statcnt_t time_slept;
} reiser4_level_stat;

/*
 * hash table statistics. Such object is added to each type safe hash table
 * instance (see fs/reiser4/type_safe_hash.h).
 */
typedef struct tshash_stat {
	statcnt_t lookup;  /* number of lookup calls */
	statcnt_t insert;  /* number of insert calls */
	statcnt_t remove;  /* number of remove calls */
	statcnt_t scanned; /* total number of items inspected during all
			    * operations. This can be used to estimate average
			    * hash-chain depth. */
} tshash_stat;

#define TSHASH_LOOKUP(stat) ({ if(stat) statcnt_inc(&stat->lookup); })
#define TSHASH_INSERT(stat) ({ if(stat) statcnt_inc(&stat->insert); })
#define TSHASH_REMOVE(stat) ({ if(stat) statcnt_inc(&stat->remove); })
#define TSHASH_SCANNED(stat) ({ if(stat) statcnt_inc(&stat->scanned); })

/* set of statistics counter. This is embedded into super-block when
   REISER4_STATS is on. */
typedef struct reiser4_statistics {
	struct {
		/* calls to coord_by_key */
		statcnt_t cbk;
		/* calls to coord_by_key that found requested key */
		statcnt_t cbk_found;
		/* calls to coord_by_key that didn't find requested key */
		statcnt_t cbk_notfound;
		/* number of times calls to coord_by_key restarted */
		statcnt_t cbk_restart;
		/* calls to coord_by_key that found key in coord cache */
		statcnt_t cbk_cache_hit;
		/* calls to coord_by_key that didn't find key in coord
		   cache */
		statcnt_t cbk_cache_miss;
		/* cbk cache search found wrong node */
		statcnt_t cbk_cache_wrong_node;
		/* search for key in coord cache raced against parallel
		   balancing and lose. This should be rare. If not,
		   update cbk_cache_search() according to comment
		   therewithin.
		*/
		statcnt_t cbk_cache_race;
		/*
		 * statistics for vroot ("virtual root") optimization of tree
		 * lookup.
		 */
		/*
		 * vroot usage failed, because...
		 */
		/* given object has no vroot set */
		statcnt_t object_lookup_novroot;
		/* vroot changed due to race with balancing */
		statcnt_t object_lookup_moved;
		/* object is not fitted into its vroot any longer */
		statcnt_t object_lookup_outside;
		/* failed to lock vroot */
		statcnt_t object_lookup_cannotlock;

		/* tree traversal had to be re-started due to vroot failure */
		statcnt_t object_lookup_restart;

		/* number of times coord of child in its parent, cached
		   in a former, was reused. */
		statcnt_t pos_in_parent_hit;
		/* number of time binary search for child position in
		   its parent had to be redone. */
		statcnt_t pos_in_parent_miss;
		/* number of times position of child in its parent was
		   cached in the former */
		statcnt_t pos_in_parent_set;
		/* how many times carry() was skipped by doing "fast
		   insertion path". See
		   fs/reiser4/plugin/node/node.h:->fast_insert() method.
		*/
		statcnt_t fast_insert;
		/* how many times carry() was skipped by doing "fast
		   paste path". See
		   fs/reiser4/plugin/node/node.h:->fast_paste() method.
		*/
		statcnt_t fast_paste;
		/* how many times carry() was skipped by doing "fast
		   cut path". See
		   fs/reiser4/plugin/node/node.h:->cut_insert() method.
		*/
		statcnt_t fast_cut;
		/* children reparented due to shifts at the parent level */
		statcnt_t reparenting;
		/* right delimiting key is not exact */
		statcnt_t rd_key_skew;
		statcnt_t check_left_nonuniq;
		statcnt_t left_nonuniq_found;
	} tree;
	reiser4_level_stat level[REISER4_MAX_ZTREE_HEIGHT];
	/* system call statistics. Indicates how many times given system (or,
	 * sometimes, internal kernel function) was
	 * invoked. Self-explanatory. */
	struct {
		statcnt_t open;
		statcnt_t lookup;
		statcnt_t create;
		statcnt_t mkdir;
		statcnt_t symlink;
		statcnt_t mknod;
		statcnt_t rename;
		statcnt_t readlink;
		statcnt_t follow_link;
		statcnt_t setattr;
		statcnt_t getattr;
		statcnt_t read;
		statcnt_t write;
		statcnt_t truncate;
		statcnt_t statfs;
		statcnt_t bmap;
		statcnt_t link;
		statcnt_t llseek;
		statcnt_t readdir;
		statcnt_t ioctl;
		statcnt_t mmap;
		statcnt_t unlink;
		statcnt_t rmdir;
		statcnt_t alloc_inode;
		statcnt_t destroy_inode;
		statcnt_t delete_inode;
		statcnt_t write_super;
		statcnt_t private_data_alloc; /* allocations of either per
					       * struct dentry or per struct
					       * file data */
	} vfs_calls;
	struct {
		/* readdir stats */
		struct {
			/* calls to readdir */
			statcnt_t calls;
			/* rewinds to the beginning of directory */
			statcnt_t reset;
			/* partial rewinds to the left */
			statcnt_t rewind_left;
			/* rewind to left that was completely within sequence
			 * of duplicate keys */
			statcnt_t left_non_uniq;
			/* restarts of rewinds to the left due to hi/lo
			 * priority locking */
			statcnt_t left_restart;
			/* rewinds to the right */
			statcnt_t rewind_right;
			/* how many times readdir position has to be adjusted
			 * due to directory modification. Large readdir
			 * comment in plugin/dir/dir.c */
			statcnt_t adjust_pos;
			/* how many times adjustment was on the left of
			 * current readdir position */
			statcnt_t adjust_lt;
			/* how many times adjustment was on the right of
			 * current readdir position */
			statcnt_t adjust_gt;
			/* how many times adjustment was exactly on the
			 * current readdir position */
			statcnt_t adjust_eq;
		} readdir;
	} dir;

	/* statistics of unix file plugin */
	struct {

		struct {
			statcnt_t readpage_calls;
			statcnt_t writepage_calls;
		} page_ops;

		/* number of tail conversions */
		statcnt_t tail2extent;
		statcnt_t extent2tail;

		/* find_next_item statistic */
		statcnt_t find_file_item;
		statcnt_t find_file_item_via_seal;
		statcnt_t find_file_item_via_right_neighbor;
		statcnt_t find_file_item_via_cbk;

	} file;
	struct {
		/* how many unformatted nodes were read */
		statcnt_t unfm_block_reads;

		/* extent_write seals and unlock znode before locking/capturing
		   page which is to be modified. After page is locked/captured
		   it validates a seal. Number of found broken seals is stored
		   here
		*/
		statcnt_t broken_seals;

		/* extent_write calls balance_dirty_pages after it modifies
		   every page. Before that it seals node it currently holds
		   and uses seal_validate to lock it again. This field stores
		   how many times balance_dirty_pages broke that seal and
		   caused to repease search tree traversal
		*/
		statcnt_t bdp_caused_repeats;
		/* how many times extent_write could not write a coord and had
		 * to ask for research */
		statcnt_t repeats;
	} extent;
	struct { /* stats on tail items */
		/* tail_write calls balance_dirty_pages after every call to
		   insert_flow. Before that it seals node it currently holds
		   and uses seal_validate to lock it again. This field stores
		   how many times balance_dirty_pages broke that seal and
		   caused to repease search tree traversal
		*/
		statcnt_t bdp_caused_repeats;
	} tail;
	/* transaction manager stats */
	struct {
		/* jiffies, spent in atom_wait_event() */
		statcnt_t slept_in_wait_event;
		/* jiffies, spent in capture_fuse_wait (wait for atom state
		 * change) */
		statcnt_t slept_in_wait_atom;
		/* number of commits */
		statcnt_t commits;
		/*number of post commit writes */
		statcnt_t post_commit_writes;
		/* jiffies, spent in commits and post commit writes */
		statcnt_t time_spent_in_commits;
		/* how many times attempt to write a flush queue ended up with
		 * an empty bio */
		statcnt_t empty_bio;
		/* how many times ->writepage kicked ktxnmged to start commit
		 * of an atom */
		statcnt_t commit_from_writepage;

		/*
		 * fs/txnmgrd.c:try_capture_block() stats
		 */

		/* atoms of node and transaction handle are the same
		 * already */
		statcnt_t capture_equal;
		/* node and handle both belong to atoms */
		statcnt_t capture_both;
		/* only node belongs to atom */
		statcnt_t capture_block;
		/* only handle belongs to atom */
		statcnt_t capture_txnh;
		/* neither node nor handle belong to atom */
		statcnt_t capture_none;

		/*
		 * how many times some transaction manager activity had to be
		 * re-started, because...
		 */
		struct {
			/* new atom was created */
			statcnt_t atom_begin;
			/* commit_current_atom() found atom in use */
			statcnt_t cannot_commit;
			/* committer had to wait */
			statcnt_t should_wait;
			/* jnode_flush was invoked several times in a row */
			statcnt_t flush;
			/* fuse_not_fused_lock_owners() fused atoms */
			statcnt_t fuse_lock_owners_fused;
			/* fuse_not_fused_lock_owners() has to restart */
			statcnt_t fuse_lock_owners;
			/* trylock failed on atom */
			statcnt_t trylock_throttle;
			/* atom trylock failed in capture_assign_block() */
			statcnt_t assign_block;
			/* atom trylock failed in capture_assign_txnh() */
			statcnt_t assign_txnh;
			/* capture_fuse_wait() was called in non-blocking
			 * mode */
			statcnt_t fuse_wait_nonblock;
			/* capture_fuse_wait() had to sleep */
			statcnt_t fuse_wait_slept;
			/* capture_init_fusion() failed to try-lock node
			 * atom */
			statcnt_t init_fusion_atomf;
			/* capture_init_fusion() failed to try-lock handle
			 * atom */
			statcnt_t init_fusion_atomh;
			/* capture_init_fusion_locked() slept during fusion */
			statcnt_t init_fusion_fused;
		} restart;
	} txnmgr;
	struct {
		/* how many nodes were squeezed to left neighbor completely */
		statcnt_t squeezed_completely;
		/* how many times nodes with unallocated children are written */
		statcnt_t flushed_with_unallocated;
		/* how many leaves were squeezed to left */
		statcnt_t squeezed_leaves;
		/* how many items were squeezed on leaf level */
		statcnt_t squeezed_leaf_items;
		/* how mnay bytes were squeezed on leaf level */
		statcnt_t squeezed_leaf_bytes;
		/* how many times jnode_flush was called */
		statcnt_t flush;
		/* how many nodes were scanned by scan_left() */
		statcnt_t left;
		/* how many nodes were scanned by scan_right() */
		statcnt_t right;
		/* an overhead of MTFLUSH semaphore */
		statcnt_t slept_in_mtflush_sem;
	} flush;
	struct {
		/* how many carry objects were allocated */
		statcnt_t alloc;
		/* how many "extra" carry objects were allocated by
		   kmalloc. */
		statcnt_t kmalloc;
	} pool;
	struct {
		/* seals that were found pristine */
		statcnt_t perfect_match;
		/* how many times node under seal was out of cache */
		statcnt_t out_of_cache;
	} seal;
	/* hash tables stats. See tshash_stat above. */
	struct {
		/* for the hash table of znodes with real block numbers */
		tshash_stat znode;
		/* for the hash table of znodes with fake block numbers */
		tshash_stat zfake;
		/* for the hash table of jnodes */
		tshash_stat jnode;
		/* for the hash table of lnodes */
		tshash_stat lnode;
		/* for the hash table of eflush_node_t's */
		tshash_stat eflush;
	} hashes;
	struct {
		/* how many times block was allocated without having valid
		 * preceder. */
		statcnt_t nohint;
	} block_alloc;
	/* how many non-unique keys were scanned into tree */
	statcnt_t non_uniq;

	/* page_common_writeback stats */
	struct {
		/* calls to ->writepage() */
		statcnt_t calls;
		/* ->writepage() failed to allocate jnode for the page */
		statcnt_t no_jnode;
		/* emergency flush succeed */
		statcnt_t written;
		/* emergency flush failed */
		statcnt_t not_written;
	} pcwb;

	/* stat of copy on capture requests */
	struct {
		statcnt_t calls;
		/* satisfied requests */
		statcnt_t ok_uber;
		statcnt_t ok_nopage;
		statcnt_t ok_clean;
		statcnt_t ok_ovrwr;
		statcnt_t ok_reloc;
		/* refused copy on capture requests */
		statcnt_t forbidden;
		statcnt_t writeback;
		statcnt_t flush_queued;
		statcnt_t dirty;
		statcnt_t eflush;
		statcnt_t scan_race;
		statcnt_t atom_changed;
		statcnt_t coc_race;
		statcnt_t coc_wait;
	} coc;

	statcnt_t pages_dirty;
	statcnt_t pages_clean;
} reiser4_stat;


#define get_current_stat() 					\
	(get_super_private_nocheck(reiser4_get_current_sb())->stats)

/* Macros to gather statistical data. If REISER4_STATS is disabled, they
   are preprocessed to nothing.
*/

#define	reiser4_stat(sb, cnt) (&get_super_private_nocheck(sb)->stats->cnt)

#define	reiser4_stat_inc_at(sb, counter)					\
	statcnt_inc(&get_super_private_nocheck(sb)->stats->counter)

#define	reiser4_stat_inc(counter)				\
	ON_CONTEXT(statcnt_inc(&get_current_stat()->counter))

#define reiser4_stat_add(counter, delta) 			\
	ON_CONTEXT(statcnt_add(&get_current_stat()->counter, delta))

#define	reiser4_stat_inc_at_level(lev, stat)					\
({										\
	int __level;								\
										\
	__level = (lev);        						\
	if (__level >= 0) {							\
		if(__level < REISER4_MAX_ZTREE_HEIGHT) {			\
			reiser4_stat_inc(level[__level]. stat);			\
			reiser4_stat_inc(level[__level]. total_hits_at_level);	\
		}								\
	}									\
})

#define	reiser4_stat_add_at_level(lev, stat, value)				\
({										\
	int level;								\
										\
	level = (lev);          						\
	if (level >= 0) {							\
		if(level < REISER4_MAX_ZTREE_HEIGHT) {				\
			reiser4_stat_add(level[level]. stat , value );		\
			reiser4_stat_inc(level[level]. total_hits_at_level);	\
		}								\
	}									\
})

#define	reiser4_stat_level_inc(l, stat)			\
	reiser4_stat_inc_at_level((l)->level_no, stat)


struct kobject;
extern int reiser4_populate_kattr_level_dir(struct kobject * kobj);
extern int reiser4_stat_init(reiser4_stat ** stats);
extern void reiser4_stat_done(reiser4_stat ** stats);

/* REISER4_STATS */
#else

#define ON_STATS(e) noop

#define	reiser4_stat(sb, cnt) ((void *)NULL)
#define	reiser4_stat_inc(counter)  noop
#define reiser4_stat_add(counter, delta) noop

#define	reiser4_stat_inc_at(sb, counter) noop
#define	reiser4_stat_inc_at_level(lev, stat) noop
#define reiser4_stat_add_at_level(lev, stat, cnt) noop
#define	reiser4_stat_level_inc(l, stat) noop

typedef struct {
} reiser4_stat;

typedef struct tshash_stat {
} tshash_stat;

#define TSHASH_LOOKUP(stat) noop
#define TSHASH_INSERT(stat) noop
#define TSHASH_REMOVE(stat) noop
#define TSHASH_SCANNED(stat) noop

#define reiser4_populate_kattr_level_dir(kobj, i) (0)
#define reiser4_stat_init(s) (0)
#define reiser4_stat_done(s) noop

#endif

extern int reiser4_populate_kattr_dir(struct kobject * kobj);


/* __FS_REISER4_STATS_H__ */
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
