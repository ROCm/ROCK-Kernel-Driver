/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2.h
 *
 * Defines macros and structures used in OCFS2
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef OCFS2_H
#define OCFS2_H

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/workqueue.h>

#include "cluster/nodemanager.h"
#include "cluster/heartbeat.h"
#include "cluster/tcp.h"

#include "dlm/dlmapi.h"

#include "ocfs2_fs.h"
#include "endian.h"
#include "ocfs2_lockid.h"

struct ocfs2_extent_map {
	u32		em_clusters;
	struct rb_root	em_extents;
};

/* Most user visible OCFS2 inodes will have very few pieces of
 * metadata, but larger files (including bitmaps, etc) must be taken
 * into account when designing an access scheme. We allow a small
 * amount of inlined blocks to be stored on an array and grow the
 * structure into a rb tree when necessary. */
#define OCFS2_INODE_MAX_CACHE_ARRAY 2

struct ocfs2_caching_info {
	unsigned int		ci_num_cached;
	union {
		sector_t	ci_array[OCFS2_INODE_MAX_CACHE_ARRAY];
		struct rb_root	ci_tree;
	} ci_cache;
};

/* this limits us to 256 nodes
 * if we need more, we can do a kmalloc for the map */
#define OCFS2_NODE_MAP_MAX_NODES    256
struct ocfs2_node_map {
	u16 num_nodes;
	unsigned long map[BITS_TO_LONGS(OCFS2_NODE_MAP_MAX_NODES)];
};

enum ocfs2_ast_action {
	OCFS2_AST_INVALID = 0,
	OCFS2_AST_ATTACH,
	OCFS2_AST_CONVERT,
	OCFS2_AST_DOWNCONVERT,
};

/* actions for an unlockast function to take. */
enum ocfs2_unlock_action {
	OCFS2_UNLOCK_INVALID = 0,
	OCFS2_UNLOCK_CANCEL_CONVERT,
	OCFS2_UNLOCK_DROP_LOCK,
};

/* ocfs2_lock_res->l_flags flags. */
#define OCFS2_LOCK_ATTACHED      (0x00000001) /* have we initialized
					       * the lvb */
#define OCFS2_LOCK_BUSY          (0x00000002) /* we are currently in
					       * dlm_lock */
#define OCFS2_LOCK_BLOCKED       (0x00000004) /* blocked waiting to
					       * downconvert*/
#define OCFS2_LOCK_LOCAL         (0x00000008) /* newly created inode */
#define OCFS2_LOCK_NEEDS_REFRESH (0x00000010)
#define OCFS2_LOCK_REFRESHING    (0x00000020)
#define OCFS2_LOCK_INITIALIZED   (0x00000040) /* track initialization
					       * for shutdown paths */
#define OCFS2_LOCK_FREEING       (0x00000080) /* help dlmglue track
					       * when to skip queueing
					       * a lock because it's
					       * about to be
					       * dropped. */
#define OCFS2_LOCK_QUEUED        (0x00000100) /* queued for downconvert */

struct ocfs2_lock_res_ops;

typedef void (*ocfs2_lock_callback)(int status, unsigned long data);

struct ocfs2_lockres_flag_callback {
	struct list_head	fc_lockres_item;
	unsigned		fc_free_once_called:1;

	unsigned long		fc_flag_mask;
	unsigned long		fc_flag_goal;

	ocfs2_lock_callback	fc_cb;
	unsigned long		fc_data;
};

struct ocfs2_lock_res {
	void                    *l_priv;
	struct ocfs2_lock_res_ops *l_ops;
	spinlock_t               l_lock;

	struct list_head         l_blocked_list;
	struct list_head         l_flag_cb_list;

	enum ocfs2_lock_type     l_type;
	unsigned long		 l_flags;
	char                     l_name[OCFS2_LOCK_ID_MAX_LEN];
	int                      l_level;
	unsigned int             l_ro_holders;
	unsigned int             l_ex_holders;
	struct dlm_lockstatus    l_lksb;
	u32                      l_local_seq;

	/* used from AST/BAST funcs. */
	enum ocfs2_ast_action    l_action;
	enum ocfs2_unlock_action l_unlock_action;
	int                      l_requested;
	int                      l_blocking;

	wait_queue_head_t        l_event;
};

enum ocfs2_vol_state
{
	VOLUME_INIT = 0,
	VOLUME_MOUNTED,
	VOLUME_DISMOUNTED,
	VOLUME_DISABLED
};

struct ocfs2_alloc_stats
{
	atomic_t moves;
	atomic_t local_data;
	atomic_t bitmap_data;
	atomic_t bg_allocs;
	atomic_t bg_extends;
};

enum ocfs2_local_alloc_state
{
	OCFS2_LA_UNUSED = 0,
	OCFS2_LA_ENABLED,
	OCFS2_LA_DISABLED
};

enum ocfs2_mount_options
{
	OCFS2_MOUNT_HB_OK   = 1 << 0,	/* Heartbeat started */
	OCFS2_MOUNT_BARRIER = 1 << 1,	/* Use block barriers */
#ifdef OCFS2_ORACORE_WORKAROUNDS
	OCFS2_MOUNT_COMPAT_OCFS = 1 << 30, /* ocfs1 compatibility mode */
#endif
};

struct _ocfs2_journal;
typedef struct _ocfs2_journal_handle ocfs2_journal_handle;

typedef struct _ocfs2_super
{
	u32 osb_id;		/* id used by the proc interface */
	struct task_struct *commit_task;
	struct super_block *sb;
	struct inode *root_inode;
	struct inode *sys_root_inode;
	struct inode *system_inodes[NUM_SYSTEM_INODES];

	struct ocfs2_slot_info *slot_info;

	spinlock_t node_map_lock;
	struct ocfs2_node_map mounted_map;
	struct ocfs2_node_map recovery_map;
	struct ocfs2_node_map umount_map;

	u32 num_clusters;
	u64 root_blkno;
	u64 system_dir_blkno;
	u64 bitmap_blkno;
	u32 bitmap_cpg;
	u8 *uuid;
	char *uuid_str;
	u8 *vol_label;
	u64 first_cluster_group_blkno;
	u32 fs_generation;

	u32 s_feature_compat;
	u32 s_feature_incompat;
	u32 s_feature_ro_compat;

	spinlock_t s_next_gen_lock;
	u32 s_next_generation;

	unsigned long s_mount_opt;

	u16 max_slots;
	u16 num_nodes;
	s16 node_num;
	s16 slot_num;
	int s_sectsize_bits;
	int s_clustersize;
	int s_clustersize_bits;
	struct proc_dir_entry *proc_sub_dir; /* points to /proc/fs/ocfs2/<maj_min> */

	atomic_t vol_state;
	struct semaphore recovery_lock;
	struct task_struct *recovery_thread_task;
	int disable_recovery;
	wait_queue_head_t checkpoint_event;
	atomic_t needs_checkpoint;
	struct _ocfs2_journal *journal;

	enum ocfs2_local_alloc_state local_alloc_state;
	struct buffer_head *local_alloc_bh;

	/* Next two fields are for local node slot recovery during
	 * mount. */
	int dirty;
	ocfs2_dinode *local_alloc_copy;

	struct ocfs2_alloc_stats alloc_stats;
	char dev_str[20];		/* "major,minor" of the device */

	struct dlm_ctxt *dlm;
	struct ocfs2_lock_res osb_super_lockres;
	struct ocfs2_lock_res osb_rename_lockres;
	struct dlm_eviction_cb osb_eviction_cb;

	wait_queue_head_t recovery_event;

	spinlock_t vote_task_lock;
	struct task_struct *vote_task;
	wait_queue_head_t vote_event;
	unsigned long vote_wake_sequence;
	unsigned long vote_work_sequence;

	struct list_head blocked_lock_list;
	unsigned long blocked_lock_count;

	struct list_head vote_list;
	int vote_count;

	u32 net_key;
	spinlock_t net_response_lock;
	unsigned int net_response_ids;
	struct list_head net_response_list;

	struct o2hb_callback_func osb_hb_up;
	struct o2hb_callback_func osb_hb_down;

	struct list_head	osb_net_handlers;

	/* see ocfs2_ki_dtor() */
	struct work_struct		osb_okp_teardown_work;
	struct ocfs2_kiocb_private	*osb_okp_teardown_next;
	atomic_t			osb_okp_pending;
	wait_queue_head_t		osb_okp_pending_wq;

	wait_queue_head_t		osb_mount_event;

	/* Truncate log info */
	struct inode			*osb_tl_inode;
	struct buffer_head		*osb_tl_bh;
	struct work_struct		osb_truncate_log_wq;
} ocfs2_super;

#define OCFS2_SB(sb)	    ((ocfs2_super *)(sb)->s_fs_info)
#define OCFS2_MAX_OSB_ID             65536

/* Helps document which BUG's should really just force the file system
 * to go readonly */
#define OCFS2_BUG_ON_RO(x) BUG_ON((x))

#define OCFS2_IS_VALID_DINODE(ptr)					\
	(!strcmp((ptr)->i_signature, OCFS2_INODE_SIGNATURE))

#define OCFS2_BUG_ON_INVALID_DINODE(__di)	do {			\
	mlog_bug_on_msg(!OCFS2_IS_VALID_DINODE((__di)),			\
		"Dinode # %"MLFu64" has bad signature %.*s\n",		\
		(__di)->i_blkno, 7,					\
		(__di)->i_signature);					\
} while (0);

#define OCFS2_IS_VALID_EXTENT_BLOCK(ptr)				\
	(!strcmp((ptr)->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE))

#define OCFS2_BUG_ON_INVALID_EXTENT_BLOCK(__eb)	do {			\
	mlog_bug_on_msg(!OCFS2_IS_VALID_EXTENT_BLOCK((__eb)),		\
		"Extent Block # %"MLFu64" has bad signature %.*s\n",	\
		(__eb)->h_blkno, 7,					\
		(__eb)->h_signature);					\
} while (0);

#define OCFS2_IS_VALID_GROUP_DESC(ptr)					\
	(!strcmp((ptr)->bg_signature, OCFS2_GROUP_DESC_SIGNATURE))

#define OCFS2_BUG_ON_INVALID_GROUP_DESC(__gd)	do {			\
	mlog_bug_on_msg(!OCFS2_IS_VALID_GROUP_DESC((__gd)),		\
		"Group Descriptor # %"MLFu64" has bad signature %.*s\n",\
		(__gd)->bg_blkno, 7,					\
		(__gd)->bg_signature);					\
} while (0);

static inline unsigned long ino_from_blkno(struct super_block *sb,
					   u64 blkno)
{
	return (unsigned long)(blkno & (u64)ULONG_MAX);
}

static inline u64 ocfs2_clusters_to_blocks(struct super_block *sb,
					   u32 clusters)
{
	int c_to_b_bits = OCFS2_SB(sb)->s_clustersize_bits -
		sb->s_blocksize_bits;

	return (u64)clusters << c_to_b_bits;
}

static inline u32 ocfs2_blocks_to_clusters(struct super_block *sb,
					   u64 blocks)
{
	int b_to_c_bits = OCFS2_SB(sb)->s_clustersize_bits -
		sb->s_blocksize_bits;

	return (u32)(blocks >> b_to_c_bits);
}

static inline unsigned int ocfs2_clusters_for_bytes(struct super_block *sb,
						    u64 bytes)
{
	int cl_bits = OCFS2_SB(sb)->s_clustersize_bits;
	unsigned int clusters;

	bytes += OCFS2_SB(sb)->s_clustersize - 1;
	/* OCFS2 just cannot have enough clusters to overflow this */
	clusters = (unsigned int)(bytes >> cl_bits);

	return clusters;
}

static inline u64 ocfs2_clusters_to_bytes(struct super_block *sb,
					  u32 clusters)
{
	return (u64)clusters << OCFS2_SB(sb)->s_clustersize_bits;
}

static inline u64 ocfs2_align_bytes_to_clusters(struct super_block *sb,
						u64 bytes)
{
	int cl_bits = OCFS2_SB(sb)->s_clustersize_bits;
	unsigned int clusters;

	clusters = ocfs2_clusters_for_bytes(sb, bytes);
	return (u64)clusters << cl_bits;
}

static inline unsigned long ocfs2_align_bytes_to_blocks(struct super_block *sb,
							u64 bytes)
{
	bytes += sb->s_blocksize - 1;
	return (unsigned long)(bytes >> sb->s_blocksize_bits);
}

static inline unsigned long ocfs2_align_bytes_to_sectors(u64 bytes)
{
	return (unsigned long)((bytes + 511) >> 9);
}

#define ocfs2_set_bit ext2_set_bit
#define ocfs2_clear_bit ext2_clear_bit
#define ocfs2_test_bit ext2_test_bit
#define ocfs2_find_next_zero_bit ext2_find_next_zero_bit
#endif  /* OCFS2_H */

