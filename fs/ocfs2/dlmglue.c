/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmglue.c
 *
 * Code which implements an OCFS2 specific interface to our DLM.
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/crc32.h>
#include <linux/kthread.h>

#include <cluster/heartbeat.h>
#include <cluster/nodemanager.h>
#include <cluster/tcp.h>

#include <dlm/dlmapi.h>

#define MLOG_MASK_PREFIX ML_DLM_GLUE
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "slot_map.h"
#include "super.h"
#include "uptodate.h"
#include "vote.h"

#include "buffer_head_io.h"

static void ocfs2_inode_ast_func(void *opaque);
static void ocfs2_inode_bast_func(void *opaque,
				  int level);
static void ocfs2_super_ast_func(void *opaque);
static void ocfs2_super_bast_func(void *opaque,
				  int level);
static void ocfs2_rename_ast_func(void *opaque);
static void ocfs2_rename_bast_func(void *opaque,
				   int level);

/* so far, all locks have gotten along with the same unlock ast */
static void ocfs2_unlock_ast_func(void *opaque,
				  enum dlm_status status);
static int ocfs2_do_unblock_meta(struct inode *inode,
				 int *requeue);
static int ocfs2_unblock_meta(struct ocfs2_lock_res *lockres,
			      int *requeue);
static int ocfs2_unblock_data(struct ocfs2_lock_res *lockres,
			      int *requeue);
static int ocfs2_unblock_osb_lock(struct ocfs2_lock_res *lockres,
				  int *requeue);
typedef void (ocfs2_convert_worker_t)(struct ocfs2_lock_res *, int);
static int ocfs2_generic_unblock_lock(ocfs2_super *osb,
				      struct ocfs2_lock_res *lockres,
				      int *requeue,
				      ocfs2_convert_worker_t *worker);

struct ocfs2_lock_res_ops {
	void (*ast)(void *);
	void (*bast)(void *, int);
	void (*unlock_ast)(void *, enum dlm_status);
	int  (*unblock)(struct ocfs2_lock_res *, int *);
};

static struct ocfs2_lock_res_ops ocfs2_inode_meta_lops = {
	.ast		= ocfs2_inode_ast_func,
	.bast		= ocfs2_inode_bast_func,
	.unlock_ast	= ocfs2_unlock_ast_func,
	.unblock	= ocfs2_unblock_meta,
};

static void ocfs2_data_convert_worker(struct ocfs2_lock_res *lockres,
				      int blocking);

static struct ocfs2_lock_res_ops ocfs2_inode_data_lops = {
	.ast		= ocfs2_inode_ast_func,
	.bast		= ocfs2_inode_bast_func,
	.unlock_ast	= ocfs2_unlock_ast_func,
	.unblock	= ocfs2_unblock_data,
};

static struct ocfs2_lock_res_ops ocfs2_super_lops = {
	.ast		= ocfs2_super_ast_func,
	.bast		= ocfs2_super_bast_func,
	.unlock_ast	= ocfs2_unlock_ast_func,
	.unblock	= ocfs2_unblock_osb_lock,
};

static struct ocfs2_lock_res_ops ocfs2_rename_lops = {
	.ast		= ocfs2_rename_ast_func,
	.bast		= ocfs2_rename_bast_func,
	.unlock_ast	= ocfs2_unlock_ast_func,
	.unblock	= ocfs2_unblock_osb_lock,
};

static inline int ocfs2_is_inode_lock(struct ocfs2_lock_res *lockres)
{
	return lockres->l_type == OCFS2_LOCK_TYPE_META ||
		lockres->l_type == OCFS2_LOCK_TYPE_DATA;
}

static inline int ocfs2_is_super_lock(struct ocfs2_lock_res *lockres)
{
	return lockres->l_type == OCFS2_LOCK_TYPE_SUPER;
}

static inline int ocfs2_is_rename_lock(struct ocfs2_lock_res *lockres)
{
	return lockres->l_type == OCFS2_LOCK_TYPE_RENAME;
}

static inline ocfs2_super *ocfs2_lock_res_super(struct ocfs2_lock_res *lockres)
{
	BUG_ON(!ocfs2_is_super_lock(lockres)
	       && !ocfs2_is_rename_lock(lockres));

	return (ocfs2_super *) lockres->l_priv;
}

static inline struct inode *ocfs2_lock_res_inode(struct ocfs2_lock_res *lockres)
{
	BUG_ON(!ocfs2_is_inode_lock(lockres));

	return (struct inode *) lockres->l_priv;
}

static int ocfs2_lock_create(ocfs2_super *osb,
			     struct ocfs2_lock_res *lockres,
			     int level,
			     int flags);
static inline int ocfs2_may_continue_on_blocked_lock(struct ocfs2_lock_res *lockres,
						     int wanted);
static int ocfs2_cluster_lock(ocfs2_super *osb,
			      struct ocfs2_lock_res *lockres,
			      int level,
			      int lkm_flags,
			      ocfs2_lock_callback cb,
			      unsigned long cb_data);
static void ocfs2_cluster_unlock(ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres,
				 int level);
static inline void ocfs2_generic_handle_downconvert_action(struct ocfs2_lock_res *lockres);
static inline void ocfs2_generic_handle_convert_action(struct ocfs2_lock_res *lockres);
static inline void ocfs2_generic_handle_attach_action(struct ocfs2_lock_res *lockres);
static int ocfs2_generic_handle_bast(struct ocfs2_lock_res *lockres, int level);
static void ocfs2_schedule_blocked_lock(ocfs2_super *osb,
					struct ocfs2_lock_res *lockres);
static inline void ocfs2_recover_from_dlm_error(struct ocfs2_lock_res *lockres,
						int convert);
#define ocfs2_log_dlm_error(_func, _stat, _lockres) do {	\
	mlog(ML_ERROR, "Dlm error \"%s\" while calling %s on "	\
		"resource %s: %s\n", dlm_errname(_stat), _func,	\
		_lockres->l_name, dlm_errmsg(_stat));		\
} while (0)
static void ocfs2_vote_on_unlock(ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres);
static int ocfs2_meta_lock_update(struct inode *inode,
				  struct buffer_head **bh);
static void ocfs2_drop_osb_locks(ocfs2_super *osb);
static inline int ocfs2_highest_compat_lock_level(int level);
static int __ocfs2_downconvert_lock(ocfs2_super *osb,
				    struct ocfs2_lock_res *lockres,
				    int new_level,
				    int lvb);
static int __ocfs2_cancel_convert(ocfs2_super *osb,
				  struct ocfs2_lock_res *lockres);
static inline int ocfs2_can_downconvert_meta_lock(struct inode *inode,
						  struct ocfs2_lock_res *lockres,
						  int new_level);

static void ocfs2_build_lock_name(enum ocfs2_lock_type type,
				  u64 blkno,
				  u32 generation,
				  char *name)
{
	int len;

	mlog_entry_void();

	BUG_ON(type >= OCFS2_NUM_LOCK_TYPES);

	len = snprintf(name, OCFS2_LOCK_ID_MAX_LEN, "%c%s%016"MLFx64"%08x",
		       ocfs2_lock_type_char(type), OCFS2_LOCK_ID_PAD, blkno,
		       generation);

	BUG_ON(len != (OCFS2_LOCK_ID_MAX_LEN - 1));

	mlog(0, "built lock resource with name: %s\n", name);

	mlog_exit_void();
}

static void ocfs2_lock_res_init_common(struct ocfs2_lock_res *res,
				       enum ocfs2_lock_type type,
				       u64 blkno,
				       u32 generation,
				       struct ocfs2_lock_res_ops *ops,
				       void *priv)
{
	ocfs2_build_lock_name(type, blkno, generation, res->l_name);

	res->l_type          = type;
	res->l_ops           = ops;
	res->l_priv          = priv;

	res->l_level         = LKM_IVMODE;
	res->l_requested     = LKM_IVMODE;
	res->l_blocking      = LKM_IVMODE;
	res->l_action        = OCFS2_AST_INVALID;
	res->l_unlock_action = OCFS2_UNLOCK_INVALID;

	res->l_flags         = OCFS2_LOCK_INITIALIZED;
}

void ocfs2_lock_res_init_once(struct ocfs2_lock_res *res)
{
	/* This also clears out the lock status block */
	memset(res, 0, sizeof(struct ocfs2_lock_res));
	spin_lock_init(&res->l_lock);
	init_waitqueue_head(&res->l_event);
	INIT_LIST_HEAD(&res->l_blocked_list);
	INIT_LIST_HEAD(&res->l_flag_cb_list);
}

void ocfs2_inode_lock_res_init(struct ocfs2_lock_res *res,
			       enum ocfs2_lock_type type,
			       struct inode *inode)
{
	struct ocfs2_lock_res_ops *ops;

	BUG_ON(type != OCFS2_LOCK_TYPE_META &&
	       type != OCFS2_LOCK_TYPE_DATA);

	if (type == OCFS2_LOCK_TYPE_META)
		ops = &ocfs2_inode_meta_lops;
	else
		ops = &ocfs2_inode_data_lops;

	ocfs2_lock_res_init_common(res, type, OCFS2_I(inode)->ip_blkno,
				   inode->i_generation, ops, inode);
}

static void ocfs2_super_lock_res_init(struct ocfs2_lock_res *res,
				      ocfs2_super *osb)
{
	/* Superblock lockres doesn't come from a slab so we call init
	 * once on it manually.  */
	ocfs2_lock_res_init_once(res);
	ocfs2_lock_res_init_common(res, OCFS2_LOCK_TYPE_SUPER,
				   OCFS2_SUPER_BLOCK_BLKNO, 0,
				   &ocfs2_super_lops, osb);
}

static void ocfs2_rename_lock_res_init(struct ocfs2_lock_res *res,
				       ocfs2_super *osb)
{
	/* Rename lockres doesn't come from a slab so we call init
	 * once on it manually.  */
	ocfs2_lock_res_init_once(res);
	ocfs2_lock_res_init_common(res, OCFS2_LOCK_TYPE_RENAME, 0, 0,
				   &ocfs2_rename_lops, osb);
}

void ocfs2_lock_res_free(struct ocfs2_lock_res *res)
{
	mlog_entry_void();

	if (!(res->l_flags & OCFS2_LOCK_INITIALIZED))
		return;

	mlog_bug_on_msg(!list_empty(&res->l_blocked_list),
			"Lockres %s is on the blocked list\n",
			res->l_name);
	mlog_bug_on_msg(!list_empty(&res->l_flag_cb_list),
			"Lockres %s has flag callbacks pending\n",
			res->l_name);
	mlog_bug_on_msg(spin_is_locked(&res->l_lock),
			"Lockres %s is locked\n",
			res->l_name);
	mlog_bug_on_msg(res->l_ro_holders,
			"Lockres %s has %u ro holders\n",
			res->l_name, res->l_ro_holders);
	mlog_bug_on_msg(res->l_ex_holders,
			"Lockres %s has %u ex holders\n",
			res->l_name, res->l_ex_holders);

	/* Need to clear out the lock status block for the dlm */
	memset(&res->l_lksb, 0, sizeof(res->l_lksb));

	res->l_flags = 0UL;
	mlog_exit_void();
}

static inline void ocfs2_inc_holders(struct ocfs2_lock_res *lockres,
				     int level)
{
	mlog_entry_void();

	BUG_ON(!lockres);

	switch(level) {
	case LKM_EXMODE:
		lockres->l_ex_holders++;
		break;
	case LKM_PRMODE:
		lockres->l_ro_holders++;
		break;
	default:
		BUG();
	}

	mlog_exit_void();
}

static inline void ocfs2_dec_holders(struct ocfs2_lock_res *lockres,
				     int level)
{
	mlog_entry_void();

	BUG_ON(!lockres);

	switch(level) {
	case LKM_EXMODE:
		BUG_ON(!lockres->l_ex_holders);
		lockres->l_ex_holders--;
		break;
	case LKM_PRMODE:
		BUG_ON(!lockres->l_ro_holders);
		lockres->l_ro_holders--;
		break;
	default:
		BUG();
	}
	mlog_exit_void();
}

/* WARNING: This function lives in a world where the only three lock
 * levels are EX, PR, and NL. It *will* have to be adjusted when more
 * lock types are added. */
static inline int ocfs2_highest_compat_lock_level(int level)
{
	int new_level = LKM_EXMODE;

	if (level == LKM_EXMODE)
		new_level = LKM_NLMODE;
	else if (level == LKM_PRMODE)
		new_level = LKM_PRMODE;
	return new_level;
}

/* XXX must be called with lockres->l_lock held */
static void lockres_set_flags(struct ocfs2_lock_res *lockres, unsigned long newflags)
{
	struct list_head *pos, *tmp;
	struct ocfs2_lockres_flag_callback *fcb;

	assert_spin_locked(&lockres->l_lock);

	lockres->l_flags = newflags;

	list_for_each_safe(pos, tmp, &lockres->l_flag_cb_list) {
		fcb = list_entry(pos, struct ocfs2_lockres_flag_callback,
				 fc_lockres_item);
		if ((lockres->l_flags & fcb->fc_flag_mask) !=
		    fcb->fc_flag_goal)
			continue;

		list_del_init(&fcb->fc_lockres_item);
		fcb->fc_cb(0, fcb->fc_data);
		if (fcb->fc_free_once_called)
			kfree(fcb);
	}
}

static void lockres_or_flags(struct ocfs2_lock_res *lockres, unsigned long or)
{
	lockres_set_flags(lockres, lockres->l_flags | or);
}
static void lockres_clear_flags(struct ocfs2_lock_res *lockres, unsigned long clear)
{
	lockres_set_flags(lockres, lockres->l_flags & ~clear);
}

static inline void ocfs2_generic_handle_downconvert_action(struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BUSY));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_ATTACHED));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));
	BUG_ON(lockres->l_blocking <= LKM_NLMODE);

	lockres->l_level = lockres->l_requested;
	if (lockres->l_level <=
	    ocfs2_highest_compat_lock_level(lockres->l_blocking)) {
		lockres->l_blocking = LKM_NLMODE;
		lockres_clear_flags(lockres, OCFS2_LOCK_BLOCKED);
	}
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);

	mlog_exit_void();
}

static inline void ocfs2_generic_handle_convert_action(struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BUSY));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_ATTACHED));

	/* Convert from RO to EX doesn't really need anything as our
	 * information is already up to data. Convert from NL to
	 * *anything* however should mark ourselves as needing an
	 * update */
	if (lockres->l_level == LKM_NLMODE)
		lockres_or_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	lockres->l_level = lockres->l_requested;
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);

	mlog_exit_void();
}

static inline void ocfs2_generic_handle_attach_action(struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	BUG_ON((!lockres->l_flags & OCFS2_LOCK_BUSY));
	BUG_ON(lockres->l_flags & OCFS2_LOCK_ATTACHED);

	if (lockres->l_requested > LKM_NLMODE &&
	    !(lockres->l_flags & OCFS2_LOCK_LOCAL))
		lockres_or_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	lockres->l_level = lockres->l_requested;
	lockres_or_flags(lockres, OCFS2_LOCK_ATTACHED);
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);

	mlog_exit_void();
}

static void ocfs2_inode_ast_func(void *opaque)
{
	struct ocfs2_lock_res *lockres = opaque;
	struct inode *inode;
	struct dlm_lockstatus *lksb;

	mlog_entry_void();

	inode = ocfs2_lock_res_inode(lockres);

	mlog(0, "AST fired for inode %"MLFu64", l_action = %u, type = %s\n",
	     OCFS2_I(inode)->ip_blkno, lockres->l_action,
	     (lockres->l_type == OCFS2_LOCK_TYPE_META) ? "Meta" : "Data");

	BUG_ON(!ocfs2_is_inode_lock(lockres));

	spin_lock(&lockres->l_lock);

	lksb = &(lockres->l_lksb);
	if (lksb->status != DLM_NORMAL) {
		mlog(ML_ERROR, "ocfs2_inode_ast_func: lksb status value of %u "
		     "on inode %"MLFu64"\n", lksb->status,
		     OCFS2_I(inode)->ip_blkno);
		spin_unlock(&lockres->l_lock);
		mlog_exit_void();
		return;
	}

	switch(lockres->l_action) {
	case OCFS2_AST_ATTACH:
		ocfs2_generic_handle_attach_action(lockres);
		lockres_clear_flags(lockres, OCFS2_LOCK_LOCAL);
		break;
	case OCFS2_AST_CONVERT:
		ocfs2_generic_handle_convert_action(lockres);
		break;
	case OCFS2_AST_DOWNCONVERT:
		ocfs2_generic_handle_downconvert_action(lockres);
		break;
	default:
		mlog(ML_ERROR, "lockres %s: ast fired with invalid action: %u "
		     "lockres flags = 0x%lx, unlock action: %u\n",
		     lockres->l_name, lockres->l_action, lockres->l_flags,
		     lockres->l_unlock_action);

		BUG();
	}

	/* data locking ignores refresh flag for now. */
	if (lockres->l_type == OCFS2_LOCK_TYPE_DATA)
		lockres_clear_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	/* set it to something invalid so if we get called again we
	 * can catch it. */
	lockres->l_action = OCFS2_AST_INVALID;
	spin_unlock(&lockres->l_lock);
	wake_up(&lockres->l_event);

	mlog_exit_void();
}

static int ocfs2_generic_handle_bast(struct ocfs2_lock_res *lockres,
				     int level)
{
	int needs_downconvert = 0;
	mlog_entry_void();

	assert_spin_locked(&lockres->l_lock);

	lockres_or_flags(lockres, OCFS2_LOCK_BLOCKED);

	if (level > lockres->l_blocking) {
		/* only schedule a downconvert if we haven't already scheduled
		 * one that goes low enough to satisfy the level we're
		 * blocking.  this also catches the case where we get
		 * duplicate BASTs */
		if (ocfs2_highest_compat_lock_level(level) <
		    ocfs2_highest_compat_lock_level(lockres->l_blocking))
			needs_downconvert = 1;

		lockres->l_blocking = level;
	}

	mlog_exit(needs_downconvert);
	return needs_downconvert;
}

static void ocfs2_generic_bast_func(ocfs2_super *osb,
				    struct ocfs2_lock_res *lockres,
				    int level)
{
	int needs_downconvert;

	mlog_entry_void();

	BUG_ON(level <= LKM_NLMODE);

	spin_lock(&lockres->l_lock);
	needs_downconvert = ocfs2_generic_handle_bast(lockres, level);
	if (needs_downconvert)
		ocfs2_schedule_blocked_lock(osb, lockres);
	spin_unlock(&lockres->l_lock);

	ocfs2_kick_vote_thread(osb);

	wake_up(&lockres->l_event);
	mlog_exit_void();
}

static void ocfs2_inode_bast_func(void *opaque, int level)
{
	struct ocfs2_lock_res *lockres = opaque;
	struct inode *inode;
	ocfs2_super *osb;

	mlog_entry_void();

	BUG_ON(!ocfs2_is_inode_lock(lockres));

	inode = ocfs2_lock_res_inode(lockres);
	osb = OCFS2_SB(inode->i_sb);

	mlog(0, "BAST fired for inode %"MLFu64", blocking = %d, level = %d "
	     "type = %s\n", OCFS2_I(inode)->ip_blkno, level,
	     lockres->l_level,
	     (lockres->l_type == OCFS2_LOCK_TYPE_META) ? "Meta" : "Data");

	ocfs2_generic_bast_func(osb, lockres, level);

	mlog_exit_void();
}

static void ocfs2_generic_ast_func(struct ocfs2_lock_res *lockres,
				   int ignore_refresh)
{
	struct dlm_lockstatus *lksb = &lockres->l_lksb;

	spin_lock(&lockres->l_lock);

	if (lksb->status != DLM_NORMAL) {
		mlog(ML_ERROR, "lockres %s: lksb status value of %u!\n",
		     lockres->l_name, lksb->status);
		spin_unlock(&lockres->l_lock);
		return;
	}

	switch(lockres->l_action) {
	case OCFS2_AST_ATTACH:
		ocfs2_generic_handle_attach_action(lockres);
		break;
	case OCFS2_AST_CONVERT:
		ocfs2_generic_handle_convert_action(lockres);
		break;
	case OCFS2_AST_DOWNCONVERT:
		ocfs2_generic_handle_downconvert_action(lockres);
		break;
	default:
		BUG();
	}

	if (ignore_refresh)
		lockres_clear_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	/* set it to something invalid so if we get called again we
	 * can catch it. */
	lockres->l_action = OCFS2_AST_INVALID;
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);
}

static void ocfs2_super_ast_func(void *opaque)
{
	struct ocfs2_lock_res *lockres = opaque;

	mlog_entry_void();
	mlog(0, "Superblock AST fired\n");

	BUG_ON(!ocfs2_is_super_lock(lockres));
	ocfs2_generic_ast_func(lockres, 0);

	mlog_exit_void();
}

static void ocfs2_super_bast_func(void *opaque,
				  int level)
{
	struct ocfs2_lock_res *lockres = opaque;
	ocfs2_super *osb;

	mlog_entry_void();
	mlog(0, "Superblock BAST fired\n");

	BUG_ON(!ocfs2_is_super_lock(lockres));
       	osb = ocfs2_lock_res_super(lockres);
	ocfs2_generic_bast_func(osb, lockres, level);

	mlog_exit_void();
}

static void ocfs2_rename_ast_func(void *opaque)
{
	struct ocfs2_lock_res *lockres = opaque;

	mlog_entry_void();

	mlog(0, "Rename AST fired\n");

	BUG_ON(!ocfs2_is_rename_lock(lockres));

	ocfs2_generic_ast_func(lockres, 1);

	mlog_exit_void();
}

static void ocfs2_rename_bast_func(void *opaque,
				   int level)
{
	struct ocfs2_lock_res *lockres = opaque;
	ocfs2_super *osb;

	mlog_entry_void();

	mlog(0, "Rename BAST fired\n");

	BUG_ON(!ocfs2_is_rename_lock(lockres));

	osb = ocfs2_lock_res_super(lockres);
	ocfs2_generic_bast_func(osb, lockres, level);

	mlog_exit_void();
}

static inline void ocfs2_recover_from_dlm_error(struct ocfs2_lock_res *lockres,
						int convert)
{
	mlog_entry_void();
	spin_lock(&lockres->l_lock);
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
	if (convert)
		lockres->l_action = OCFS2_AST_INVALID;
	else
		lockres->l_unlock_action = OCFS2_UNLOCK_INVALID;
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);
	mlog_exit_void();
}

/* Note: If we detect another process working on the lock (i.e.,
 * OCFS2_LOCK_BUSY), we'll bail out returning 0. It's up to the caller
 * to do the right thing in that case.
 */
static int ocfs2_lock_create(ocfs2_super *osb,
			     struct ocfs2_lock_res *lockres,
			     int level,
			     int flags)
{
	int ret = 0;
	enum dlm_status status;

	mlog_entry_void();

	mlog(0, "lock %s, level = %d, flags = %d\n", lockres->l_name, level,
	     flags);

	spin_lock(&lockres->l_lock);
	if ((lockres->l_flags & OCFS2_LOCK_ATTACHED) ||
	    (lockres->l_flags & OCFS2_LOCK_BUSY)) {
		spin_unlock(&lockres->l_lock);
		goto bail;
	}

	lockres->l_action = OCFS2_AST_ATTACH;
	lockres->l_requested = level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	spin_unlock(&lockres->l_lock);

	status = dlmlock(osb->dlm,
			 level,
			 &lockres->l_lksb,
			 flags,
			 lockres->l_name,
			 lockres->l_ops->ast,
			 lockres,
			 lockres->l_ops->bast);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmlock", status, lockres);
		ret = -EINVAL;
		ocfs2_recover_from_dlm_error(lockres, 1);
	}

	mlog(0, "lock %s, successfull return from dlmlock\n", lockres->l_name);

bail:
	mlog_exit(ret);
	return ret;
}

static inline int ocfs2_check_wait_flag(struct ocfs2_lock_res *lockres,
					int flag)
{
	int ret;

	spin_lock(&lockres->l_lock);
	ret = lockres->l_flags & flag;
	spin_unlock(&lockres->l_lock);

	return ret;
}

static inline void ocfs2_wait_on_busy_lock(struct ocfs2_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !ocfs2_check_wait_flag(lockres, OCFS2_LOCK_BUSY));
}

static inline void ocfs2_wait_on_refreshing_lock(struct ocfs2_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !ocfs2_check_wait_flag(lockres, OCFS2_LOCK_REFRESHING));
}

static void lockres_add_flag_callback(struct ocfs2_lock_res *lockres,
				      struct ocfs2_lockres_flag_callback *fcb,
				      unsigned long mask, unsigned long goal)
{
	BUG_ON(!list_empty(&fcb->fc_lockres_item));
	BUG_ON(fcb->fc_cb == NULL);

	assert_spin_locked(&lockres->l_lock);

	list_add_tail(&fcb->fc_lockres_item, &lockres->l_flag_cb_list);
	fcb->fc_flag_mask = mask;
	fcb->fc_flag_goal = goal;
}

/* predict what lock level we'll be dropping down to on behalf
 * of another node, and return true if the currently wanted
 * level will be compatible with it. */
static inline int ocfs2_may_continue_on_blocked_lock(struct ocfs2_lock_res *lockres,
						     int wanted)
{
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));

	return wanted <= ocfs2_highest_compat_lock_level(lockres->l_blocking);
}

/* these are generic and could be used elsewhere */
struct ocfs2_status_completion {
	int			sc_status;
	struct completion	sc_complete;
};

static void ocfs2_status_completion_cb(int rc, unsigned long data)
{
	struct ocfs2_status_completion *sc;

	sc = (struct ocfs2_status_completion *)data;
	sc->sc_status = rc;
	complete(&sc->sc_complete);
}

static int ocfs2_wait_for_status_completion(struct ocfs2_status_completion *sc)
{
	wait_for_completion(&sc->sc_complete);
	/* Re-arm the completion in case we want to wait on it again */
	INIT_COMPLETION(sc->sc_complete);
	return sc->sc_status;
}

static void ocfs2_init_fcb(struct ocfs2_lockres_flag_callback *fcb,
			   ocfs2_lock_callback cb,
			   unsigned long cb_data,
			   int stack_allocated)
{
	fcb->fc_cb = cb;
	fcb->fc_data = cb_data;
	fcb->fc_free_once_called = !stack_allocated;
	INIT_LIST_HEAD(&fcb->fc_lockres_item);
}

/* Init a stack allocated FCB and an ocfs2_status_completion together. */
static void ocfs2_init_completion_fcb(struct ocfs2_lockres_flag_callback *fcb,
				      struct ocfs2_status_completion *sc)
{
	init_completion(&sc->sc_complete);
	ocfs2_init_fcb(fcb, ocfs2_status_completion_cb, (unsigned long) sc, 1);
}

static int ocfs2_cluster_lock(ocfs2_super *osb,
			      struct ocfs2_lock_res *lockres,
			      int level,
			      int lkm_flags,
			      ocfs2_lock_callback cb,
			      unsigned long cb_data)
{
	struct ocfs2_lockres_flag_callback sync_fcb, *fcb;
	struct ocfs2_status_completion sc;
	enum dlm_status status;
	int ret;
	int catch_signals = !(osb->s_mount_opt & OCFS2_MOUNT_NOINTR);
	int sync = 1;

	mlog_entry_void();

	if (cb != NULL) {
		fcb = kmalloc(sizeof(*fcb), GFP_NOFS);
		if (fcb == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		ocfs2_init_fcb(fcb, cb, cb_data, 0);

		/* A callback passed in means we'll assume async
		 * behavior - no waiting on dlm operations will be
		 * done here and the allocated fcb will call the
		 * callback when done. */
		sync = 0;
	} else {
		/* No callback passed which means the caller wants
		 * synchronous behavior - we avoid kmalloc and use a
		 * stack allocated fcb for this. The status completion
		 * helpers defined above come in handy here. */
		fcb = &sync_fcb;
		ocfs2_init_completion_fcb(fcb, &sc);
	}

again:
	if (catch_signals && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	spin_lock(&lockres->l_lock);

	mlog_bug_on_msg(lockres->l_flags & OCFS2_LOCK_FREEING,
			"Cluster lock called on freeing lockres %s! flags 0x%lx\n",
			lockres->l_name, lockres->l_flags);

	/* We only compare against the currently granted level
	 * here. If the lock is blocked waiting on a downconvert,
	 * we'll get caught below. */
	if (lockres->l_flags & OCFS2_LOCK_BUSY &&
	    level > lockres->l_level) {
		/* is someone sitting in dlm_lock? If so, wait on
		 * them. */
		lockres_add_flag_callback(lockres, fcb, OCFS2_LOCK_BUSY, 0);
		ret = -EIOCBRETRY;
		goto unlock;
	}

	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
		/* lock has not been created yet. */
		spin_unlock(&lockres->l_lock);

		ret = ocfs2_lock_create(osb, lockres, LKM_NLMODE, 0);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
		goto again;
	}

	if (lockres->l_flags & OCFS2_LOCK_BLOCKED &&
	    !ocfs2_may_continue_on_blocked_lock(lockres, level)) {
		/* is the lock is currently blocked on behalf of
		 * another node */
		lockres_add_flag_callback(lockres, fcb, OCFS2_LOCK_BLOCKED, 0);
		ret = -EIOCBRETRY;
		goto unlock;
	}

	if (level > lockres->l_level) {
		if (lockres->l_action != OCFS2_AST_INVALID)
			mlog(ML_ERROR, "lockres %s has action %u pending\n",
			     lockres->l_name, lockres->l_action);

		lockres->l_action = OCFS2_AST_CONVERT;
		lockres->l_requested = level;
		lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
		spin_unlock(&lockres->l_lock);

		BUG_ON(level == LKM_IVMODE);
		BUG_ON(level == LKM_NLMODE);

		mlog(0, "lock %s, convert from %d to level = %d\n",
		     lockres->l_name, lockres->l_level, level);

		/* call dlm_lock to upgrade lock now */
		status = dlmlock(osb->dlm,
				 level,
				 &lockres->l_lksb,
				 lkm_flags|LKM_CONVERT|LKM_VALBLK,
				 lockres->l_name,
				 lockres->l_ops->ast,
				 lockres,
				 lockres->l_ops->bast);
		if (status != DLM_NORMAL) {
			if ((lkm_flags & LKM_NOQUEUE) &&
			    (status == DLM_NOTQUEUED))
				ret = -EAGAIN;
			else {
				ocfs2_log_dlm_error("dlmlock", status,
						    lockres);
				ret = -EINVAL;
			}
			ocfs2_recover_from_dlm_error(lockres, 1);
			goto out;
		}

		mlog(0, "lock %s, successfull return from dlmlock\n",
		     lockres->l_name);

		/* At this point we've gone inside the dlm and need to
		 * complete our work regardless. */
		catch_signals = 0;

		/* wait for busy to clear and carry on */
		goto again;
	}

	/* Ok, if we get here then we're good to go. */
	ocfs2_inc_holders(lockres, level);

	ret = 0;
unlock:
	spin_unlock(&lockres->l_lock);
out:
	/* Non-async callers will always wait here for dlm operations
	 * to complete. We must be careful to re-initialize the
	 * completion before looping back. */
	if (ret == -EIOCBRETRY && sync) {
		ret = ocfs2_wait_for_status_completion(&sc);
		if (ret == 0)
			goto again;
		mlog_errno(ret);
	}

	/* Only free the async fcb on error. */
	if (ret && ret != -EIOCBRETRY && !sync) {
		mlog_bug_on_msg(!list_empty(&fcb->fc_lockres_item),
				"Lockres %s, freeing flag callback in use\n",
				lockres->l_name);
		kfree(fcb);
	}

	mlog_exit(ret);
	return ret;
}

static void ocfs2_cluster_unlock(ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres,
				 int level)
{
	mlog_entry_void();
	spin_lock(&lockres->l_lock);
	ocfs2_dec_holders(lockres, level);
	ocfs2_vote_on_unlock(osb, lockres);
	spin_unlock(&lockres->l_lock);
	mlog_exit_void();
}

/* Grants us an EX lock on the data and metadata resources, skipping
 * the normal cluster directory lookup. Use this ONLY on newly created
 * inodes which other nodes can't possibly see, and which haven't been
 * hashed in the inode hash yet. This can give us a good performance
 * increase as it'll skip the network broadcast normally associated
 * with creating a new lock resource. */
int ocfs2_create_new_inode_locks(struct inode *inode)
{
	int status;
	ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_lock_res *lockres;

	BUG_ON(!inode);
	BUG_ON(!ocfs2_inode_is_new(inode));

	mlog_entry_void();

	mlog(0, "Inode %"MLFu64"\n", OCFS2_I(inode)->ip_blkno);

	/* NOTE: That we don't increment any of the holder counts, nor
	 * do we add anything to a journal handle. Since this is
	 * supposed to be a new inode which the cluster doesn't know
	 * about yet, there is no need to.  As far as the LVB handling
	 * is concerned, this is basically like acquiring an EX lock
	 * on a resource which has an invalid one -- we'll set it
	 * valid when we release the EX. */

	lockres = &OCFS2_I(inode)->ip_meta_lockres;

	spin_lock(&lockres->l_lock);
	BUG_ON(lockres->l_flags & OCFS2_LOCK_ATTACHED);
	lockres_or_flags(lockres, OCFS2_LOCK_LOCAL);
	spin_unlock(&lockres->l_lock);

	status = ocfs2_lock_create(osb, lockres, LKM_EXMODE, LKM_LOCAL);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	lockres = &OCFS2_I(inode)->ip_data_lockres;

	spin_lock(&lockres->l_lock);
	BUG_ON(lockres->l_flags & OCFS2_LOCK_ATTACHED);
	lockres_or_flags(lockres, OCFS2_LOCK_LOCAL);
	spin_unlock(&lockres->l_lock);

	status = ocfs2_lock_create(osb, lockres, LKM_EXMODE, LKM_LOCAL);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = 0;
bail:
	mlog_exit(status);
	return status;
}

int ocfs2_data_lock(struct inode *inode,
		    int write)
{
	int status = 0, level;
	struct ocfs2_lock_res *lockres;

	BUG_ON(!inode);

	mlog_entry_void();

	mlog(0, "inode %"MLFu64" take %s DATA lock\n",
	     OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	/* We'll allow faking a readonly data lock for
	 * rodevices. */
	if (ocfs2_is_hard_readonly(OCFS2_SB(inode->i_sb))) {
		if (write) {
			status = -EROFS;
			mlog_errno(status);
		}
		goto out;
	}

	lockres = &OCFS2_I(inode)->ip_data_lockres;

	level = write ? LKM_EXMODE : LKM_PRMODE;

	status = ocfs2_cluster_lock(OCFS2_SB(inode->i_sb), lockres, level, 0,
				    NULL, 0);
	if (status < 0)
		mlog_errno(status);

out:
	mlog_exit(status);
	return status;
}

static void ocfs2_vote_on_unlock(ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres)
{
	int kick = 0;

	mlog_entry_void();

	/* If we know that another node is waiting on our lock, kick
	 * the vote thread * pre-emptively when we reach a release
	 * condition. */
	if (lockres->l_flags & OCFS2_LOCK_BLOCKED) {
		switch(lockres->l_blocking) {
		case LKM_EXMODE:
			if (!lockres->l_ex_holders && !lockres->l_ro_holders)
				kick = 1;
			break;
		case LKM_PRMODE:
			if (!lockres->l_ex_holders)
				kick = 1;
			break;
		default:
			BUG();
		}
	}

	if (kick)
		ocfs2_kick_vote_thread(osb);

	mlog_exit_void();
}

void ocfs2_data_unlock(struct inode *inode,
		       int write)
{
	int level = write ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_data_lockres;

	mlog_entry_void();

	mlog(0, "inode %"MLFu64" drop %s DATA lock\n",
	     OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	if (!ocfs2_is_hard_readonly(OCFS2_SB(inode->i_sb)))
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres, level);

	mlog_exit_void();
}

#define OCFS2_SEC_BITS   34
#define OCFS2_SEC_SHIFT  (64 - 34)
#define OCFS2_NSEC_MASK  ((1ULL << OCFS2_SEC_SHIFT) - 1)

/* LVB only has room for 64 bits of time here so we pack it for
 * now. */
static u64 ocfs2_pack_timespec(struct timespec *spec)
{
	u64 res;
	u64 sec = spec->tv_sec;
	u32 nsec = spec->tv_nsec;

	res = (sec << OCFS2_SEC_SHIFT) | (nsec & OCFS2_NSEC_MASK);

	return res;
}

/* Call this with the lockres locked. I am reasonably sure we don't
 * need ip_lock in this function as anyone who would be changing those
 * values is supposed to be blocked in ocfs2_meta_lock right now. */
static void __ocfs2_stuff_meta_lvb(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_meta_lockres;
	struct ocfs2_meta_lvb *lvb;

	mlog_entry_void();

	lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	/* Setting this to zero will ensure that old versions of the
	 * LVB code don't trust our information. */
	lvb->lvb_old_seq   = cpu_to_be32(0);
	lvb->lvb_version   = cpu_to_be32(OCFS2_LVB_VERSION);

	lvb->lvb_isize     = cpu_to_be64(i_size_read(inode));
	lvb->lvb_iclusters = cpu_to_be32(oi->ip_clusters);
	lvb->lvb_iuid      = cpu_to_be32(inode->i_uid);
	lvb->lvb_igid      = cpu_to_be32(inode->i_gid);
	lvb->lvb_imode     = cpu_to_be16(inode->i_mode);
	lvb->lvb_inlink    = cpu_to_be16(inode->i_nlink);
	lvb->lvb_iatime_packed  =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_atime));
	lvb->lvb_ictime_packed =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_ctime));
	lvb->lvb_imtime_packed =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_mtime));

	mlog_meta_lvb(0, lockres);

	mlog_exit_void();
}

static void ocfs2_unpack_timespec(struct timespec *spec,
				  u64 packed_time)
{
	spec->tv_sec = packed_time >> OCFS2_SEC_SHIFT;
	spec->tv_nsec = packed_time & OCFS2_NSEC_MASK;
}

static void ocfs2_refresh_inode_from_lvb(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_meta_lockres;
	struct ocfs2_meta_lvb *lvb;

	mlog_entry_void();

	mlog_meta_lvb(0, lockres);

	lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	/* We're safe here without the lockres lock... */
	spin_lock(&oi->ip_lock);
	oi->ip_clusters = be32_to_cpu(lvb->lvb_iclusters);
	i_size_write(inode, be64_to_cpu(lvb->lvb_isize));

	/* fast-symlinks are a special case */
	if (S_ISLNK(inode->i_mode) && !oi->ip_clusters)
		inode->i_blocks = 0;
	else
		inode->i_blocks =
			ocfs2_align_bytes_to_sectors(i_size_read(inode));

	inode->i_uid     = be32_to_cpu(lvb->lvb_iuid);
	inode->i_gid     = be32_to_cpu(lvb->lvb_igid);
	inode->i_mode    = be16_to_cpu(lvb->lvb_imode);
	inode->i_nlink   = be16_to_cpu(lvb->lvb_inlink);
	ocfs2_unpack_timespec(&inode->i_atime,
			      be64_to_cpu(lvb->lvb_iatime_packed));
	ocfs2_unpack_timespec(&inode->i_mtime,
			      be64_to_cpu(lvb->lvb_imtime_packed));
	ocfs2_unpack_timespec(&inode->i_ctime,
			      be64_to_cpu(lvb->lvb_ictime_packed));
	spin_unlock(&oi->ip_lock);

	mlog_exit_void();
}

static inline int ocfs2_meta_lvb_is_trustable(struct ocfs2_lock_res *lockres)
{
	struct ocfs2_meta_lvb *lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	/* Old OCFS2 versions stored a "sequence" in the lvb to
	 * determine whether the information could be trusted. We
	 * don't want to use an lvb populated from a node running the
	 * old code, so check that sequence is not set. */
	if (!lvb->lvb_old_seq &&
	    be32_to_cpu(lvb->lvb_version) == OCFS2_LVB_VERSION)
		return 1;
	return 0;
}

/* Determine whether a lock resource needs to be refreshed, and
 * arbitrate who gets to refresh it.
 *
 *   0 means no refresh needed.
 *
 *   > 0 means you need to refresh this and you MUST call
 *   ocfs2_complete_lock_res_refresh afterwards. */
static int ocfs2_should_refresh_lock_res(struct ocfs2_lock_res *lockres)
{

	int status = 0;
	mlog_entry_void();

refresh_check:
	spin_lock(&lockres->l_lock);
	if (!(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH)) {
		spin_unlock(&lockres->l_lock);
		goto bail;
	}

	if (lockres->l_flags & OCFS2_LOCK_REFRESHING) {
		spin_unlock(&lockres->l_lock);

		ocfs2_wait_on_refreshing_lock(lockres);
		goto refresh_check;
	}

	/* Ok, I'll be the one to refresh this lock. */
	lockres_or_flags(lockres, OCFS2_LOCK_REFRESHING);
	spin_unlock(&lockres->l_lock);

	status = 1;
bail:
	mlog_exit(status);
	return status;
}

/* If status is non zero, I'll mark it as not being in refresh
 * anymroe, but i won't clear the needs refresh flag. */
static inline void ocfs2_complete_lock_res_refresh(struct ocfs2_lock_res *lockres,
						   int status)
{
	mlog_entry_void();

	spin_lock(&lockres->l_lock);
	lockres_clear_flags(lockres, OCFS2_LOCK_REFRESHING);
	if (!status)
		lockres_clear_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);

	mlog_exit_void();
}

/* may or may not return a bh if it went to disk. */
static int ocfs2_meta_lock_update(struct inode *inode,
				  struct buffer_head **bh)
{
	int status = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres;
	ocfs2_dinode *fe;

	mlog_entry_void();

	spin_lock(&oi->ip_lock);
	if (oi->ip_flags & OCFS2_INODE_DELETED) {
		mlog(0, "Orphaned inode %"MLFu64" was deleted while we "
		     "were waiting on a lock. ip_flags = 0x%x\n",
		     oi->ip_blkno, oi->ip_flags);
		spin_unlock(&oi->ip_lock);
		status = -ENOENT;
		goto bail;
	}
	spin_unlock(&oi->ip_lock);

	lockres = &oi->ip_meta_lockres;

	if (!ocfs2_should_refresh_lock_res(lockres))
		goto bail;

	/* This will discard any caching information we might have had
	 * for the inode metadata. */
	ocfs2_metadata_cache_purge(inode);

	/* will do nothing for inode types that don't use the extent
	 * map (directories, bitmap files, etc) */
	ocfs2_extent_map_trunc(inode, 0);

	if (ocfs2_meta_lvb_is_trustable(lockres)) {
		mlog(0, "Trusting LVB on inode %"MLFu64"\n",
		     oi->ip_blkno);
		ocfs2_refresh_inode_from_lvb(inode);
	} else {
		/* Boo, we have to go to disk. */
		/* read bh, cast, ocfs2_refresh_inode */
		status = ocfs2_read_block(OCFS2_SB(inode->i_sb), oi->ip_blkno,
					  bh, OCFS2_BH_CACHED, inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail_refresh;
		}
		fe = (ocfs2_dinode *) (*bh)->b_data;

		/* This is a good chance to make sure we're not
		 * locking an invalid object.
		 *
		 * We bug on a stale inode here because we checked
		 * above whether it was wiped from disk. The wiping
		 * node provides a guarantee that we receive that
		 * message and can mark the inode before dropping any
		 * locks associated with it. */
		if (!OCFS2_IS_VALID_DINODE(fe)) {
			OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
			status = -EIO;
			goto bail_refresh;
		}
		mlog_bug_on_msg(inode->i_generation !=
				le32_to_cpu(fe->i_generation),
				"Invalid dinode %"MLFu64" disk generation: %u "
				"inode->i_generation: %u\n",
				oi->ip_blkno, le32_to_cpu(fe->i_generation),
				inode->i_generation);
		mlog_bug_on_msg(le64_to_cpu(fe->i_dtime) ||
				!(fe->i_flags & cpu_to_le32(OCFS2_VALID_FL)),
				"Stale dinode %"MLFu64" dtime: %"MLFu64" "
				"flags: 0x%x\n", oi->ip_blkno,
				le64_to_cpu(fe->i_dtime),
				le32_to_cpu(fe->i_flags));

		ocfs2_refresh_inode(inode, fe);
	}

#ifdef OCFS2_DELETE_INODE_WORKAROUND
	/* We might as well check this here - since the inode is now
	 * locked, an up to date view will indicate whether this was
	 * never actually orphaned -- i_nlink should be zero for an
	 * orphaned inode. */
	spin_lock(&oi->ip_lock);
	if (inode->i_nlink &&
	    oi->ip_flags & OCFS2_INODE_MAYBE_ORPHANED) {
		mlog(0, "Inode %"MLFu64": clearing maybe_orphaned flag\n",
		     oi->ip_blkno);
		oi->ip_flags &= ~OCFS2_INODE_MAYBE_ORPHANED;
	}
	spin_unlock(&oi->ip_lock);
#endif

	status = 0;
bail_refresh:
	ocfs2_complete_lock_res_refresh(lockres, status);
bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_assign_bh(struct inode *inode,
			   struct buffer_head **ret_bh,
			   struct buffer_head *passed_bh)
{
	int status;

	if (passed_bh) {
		/* Ok, the update went to disk for us, use the
		 * returned bh. */
		*ret_bh = passed_bh;
		get_bh(*ret_bh);

		return 0;
	}

	status = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				  OCFS2_I(inode)->ip_blkno,
				  ret_bh,
				  OCFS2_BH_CACHED,
				  inode);
	if (status < 0)
		mlog_errno(status);

	return status;
}

/*
 * returns < 0 error if the callback will never be called, otherwise
 * the result of the lock will be communicated via the callback.
 */
int ocfs2_meta_lock_full(struct inode *inode,
			 ocfs2_journal_handle *handle,
			 struct buffer_head **ret_bh,
			 int ex,
			 int flags,
			 ocfs2_lock_callback cb,
			 unsigned long cb_data)
{
	int status, level, dlm_flags, acquired;
	struct ocfs2_lock_res *lockres;
	ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *local_bh = NULL;

	BUG_ON(!inode);

	mlog_entry_void();

	mlog(0, "inode %"MLFu64", take %s META lock\n",
	     OCFS2_I(inode)->ip_blkno,
	     ex ? "EXMODE" : "PRMODE");

	status = 0;
	acquired = 0;
	/* We'll allow faking a readonly metadata lock for
	 * rodevices. */
	if (ocfs2_is_hard_readonly(osb)) {
		if (ex)
			status = -EROFS;
		goto bail;
	}

	if (!(flags & OCFS2_META_LOCK_RECOVERY))
		wait_event(osb->recovery_event,
			   ocfs2_node_map_is_empty(osb, &osb->recovery_map));

	acquired = 0;
	lockres = &OCFS2_I(inode)->ip_meta_lockres;
	level = ex ? LKM_EXMODE : LKM_PRMODE;
	dlm_flags = 0;
	if (flags & OCFS2_META_LOCK_NOQUEUE)
		dlm_flags |= LKM_NOQUEUE;

	status = ocfs2_cluster_lock(osb, lockres, level, dlm_flags, cb,
				    cb_data);
	if (status < 0) {
		if (status != -EAGAIN && status != -EIOCBRETRY)
			mlog_errno(status);
		goto bail;
	}

	/* Notify the error cleanup path to drop the cluster lock. */
	acquired = 1;

	/* We wait twice because a node may have died while we were in
	 * the lower dlm layers. The second time though, we've
	 * committed to owning this lock so we don't allow signals to
	 * abort the operation. */
	if (!(flags & OCFS2_META_LOCK_RECOVERY))
		wait_event(osb->recovery_event,
			   ocfs2_node_map_is_empty(osb, &osb->recovery_map));

	/* This is fun. The caller may want a bh back, or it may
	 * not. ocfs2_meta_lock_update definitely wants one in, but
	 * may or may not read one, depending on what's in the
	 * LVB. The result of all of this is that we've *only* gone to
	 * disk if we have to, so the complexity is worthwhile. */
	status = ocfs2_meta_lock_update(inode, &local_bh);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail;
	}

	if (ret_bh) {
		status = ocfs2_assign_bh(inode, ret_bh, local_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	if (handle) {
		status = ocfs2_handle_add_lock(handle, inode);
		if (status < 0)
			mlog_errno(status);
	}

bail:
	if (status < 0) {
		if (ret_bh && (*ret_bh)) {
			brelse(*ret_bh);
			*ret_bh = NULL;
		}
		if (acquired)
			ocfs2_meta_unlock(inode, ex);
	}

	if (local_bh)
		brelse(local_bh);

	mlog_exit(status);
	return status;
}

void ocfs2_meta_unlock(struct inode *inode,
		       int ex)
{
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_meta_lockres;

	mlog_entry_void();

	mlog(0, "inode %"MLFu64" drop %s META lock\n",
	     OCFS2_I(inode)->ip_blkno,
	     ex ? "EXMODE" : "PRMODE");

	if (!ocfs2_is_hard_readonly(OCFS2_SB(inode->i_sb)))
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres, level);

	mlog_exit_void();
}

int ocfs2_super_lock(ocfs2_super *osb,
		     int ex)
{
	int status;
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &osb->osb_super_lockres;
	struct buffer_head *bh;
	struct ocfs2_slot_info *si = osb->slot_info;

	mlog_entry_void();

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	status = ocfs2_cluster_lock(osb, lockres, level, 0, NULL, 0);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* The super block lock path is really in the best position to
	 * know when resources covered by the lock need to be
	 * refreshed, so we do it here. Of course, making sense of
	 * everything is up to the caller :) */
	status = ocfs2_should_refresh_lock_res(lockres);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	if (status) {
		bh = si->si_bh;
		status = ocfs2_read_block(osb, bh->b_blocknr, &bh, 0,
					  si->si_inode);
		if (status == 0)
			ocfs2_update_slot_info(si);

		ocfs2_complete_lock_res_refresh(lockres, status);

		if (status < 0)
			mlog_errno(status);
	}
bail:
	mlog_exit(status);
	return status;
}

void ocfs2_super_unlock(ocfs2_super *osb,
			int ex)
{
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &osb->osb_super_lockres;

	ocfs2_cluster_unlock(osb, lockres, level);
}

int ocfs2_rename_lock(ocfs2_super *osb)
{
	int status;
	struct ocfs2_lock_res *lockres = &osb->osb_rename_lockres;

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	status = ocfs2_cluster_lock(osb, lockres, LKM_EXMODE, 0, NULL, 0);
	if (status < 0)
		mlog_errno(status);

	return status;
}

void ocfs2_rename_unlock(ocfs2_super *osb)
{
	struct ocfs2_lock_res *lockres = &osb->osb_rename_lockres;

	ocfs2_cluster_unlock(osb, lockres, LKM_EXMODE);
}

int ocfs2_dlm_init(ocfs2_super *osb)
{
	int status;
	u32 dlm_key;
	struct dlm_ctxt *dlm;

	mlog_entry_void();

	/* launch vote thread */
	osb->vote_task = kthread_run(ocfs2_vote_thread, osb, "ocfs2vote-%d",
				     osb->osb_id);
	if (IS_ERR(osb->vote_task)) {
		status = PTR_ERR(osb->vote_task);
		osb->vote_task = NULL;
		mlog_errno(status);
		goto bail;
	}

	/* used by the dlm code to make message headers unique, each
	 * node in this domain must agree on this. */
	dlm_key = crc32_le(0, osb->uuid_str, strlen(osb->uuid_str));

	/* for now, uuid == domain */
	dlm = dlm_register_domain(osb->uuid_str, dlm_key);
	if (IS_ERR(dlm)) {
		status = PTR_ERR(dlm);
		mlog_errno(status);
		goto bail;
	}

	ocfs2_super_lock_res_init(&osb->osb_super_lockres, osb);
	ocfs2_rename_lock_res_init(&osb->osb_rename_lockres, osb);

	dlm_register_eviction_cb(dlm, &osb->osb_eviction_cb);

	osb->dlm = dlm;

	status = 0;
bail:

	mlog_exit(status);
	return status;
}

void ocfs2_dlm_shutdown(ocfs2_super *osb)
{
	mlog_entry_void();

	dlm_unregister_eviction_cb(&osb->osb_eviction_cb);

	ocfs2_drop_osb_locks(osb);

	if (osb->vote_task) {
		kthread_stop(osb->vote_task);
		osb->vote_task = NULL;
	}

	ocfs2_lock_res_free(&osb->osb_super_lockres);
	ocfs2_lock_res_free(&osb->osb_rename_lockres);

	dlm_unregister_domain(osb->dlm);
	osb->dlm = NULL;

	mlog_exit_void();
}

static void ocfs2_unlock_ast_func(void *opaque, enum dlm_status status)
{
	struct ocfs2_lock_res *lockres = opaque;

	mlog_entry_void();

	mlog(0, "UNLOCK AST called on lock %s, action = %d\n", lockres->l_name,
	     lockres->l_unlock_action);

	spin_lock(&lockres->l_lock);
	/* We tried to cancel a convert request, but it was already
	 * granted. All we want to do here is clear our unlock
	 * state. The wake_up call done at the bottom is redundant
	 * (__ocfs2_cancel_convert doesn't sleep on this) but doesn't
	 * hurt anything anyway */
	if (status == DLM_CANCELGRANT &&
	    lockres->l_unlock_action == OCFS2_UNLOCK_CANCEL_CONVERT) {
		mlog(0, "Got cancelgrant for %s\n", lockres->l_name);

		/* We don't clear the busy flag in this case as it
		 * should have been cleared by the ast which the dlm
		 * has called. */
		goto complete_unlock;
	}

	if (status != DLM_NORMAL) {
		mlog(ML_ERROR, "Dlm passes status %d for lock %s, "
		     "unlock_action %d\n", status, lockres->l_name,
		     lockres->l_unlock_action);
		spin_unlock(&lockres->l_lock);
		return;
	}

	switch(lockres->l_unlock_action) {
	case OCFS2_UNLOCK_CANCEL_CONVERT:
		mlog(0, "Cancel convert success for %s\n", lockres->l_name);
		lockres->l_action = OCFS2_AST_INVALID;
		break;
	case OCFS2_UNLOCK_DROP_LOCK:
		lockres->l_level = LKM_IVMODE;
		break;
	default:
		BUG();
	}

	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
complete_unlock:
	lockres->l_unlock_action = OCFS2_UNLOCK_INVALID;
	spin_unlock(&lockres->l_lock);

	wake_up(&lockres->l_event);

	mlog_exit_void();
}

/* BEWARE: called with lockres lock, and always drops it. Caller
 * should not be calling us with a busy lock... */
static int __ocfs2_drop_lock(ocfs2_super *osb,
			     struct ocfs2_lock_res *lockres)
{
	int ret = 0;
	enum dlm_status status;

	if (lockres->l_flags & OCFS2_LOCK_BUSY)
		mlog(ML_ERROR, "destroying busy lock: \"%s\"\n",
		     lockres->l_name);
	if (lockres->l_flags & OCFS2_LOCK_BLOCKED)
		mlog(0, "destroying blocked lock: \"%s\"\n", lockres->l_name);

	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
		spin_unlock(&lockres->l_lock);
		goto bail;
	}

	lockres_clear_flags(lockres, OCFS2_LOCK_ATTACHED);

	/* make sure we never get here while waiting for an ast to
	 * fire. */
	BUG_ON(lockres->l_action != OCFS2_AST_INVALID);

	/* is this necessary? */
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	lockres->l_unlock_action = OCFS2_UNLOCK_DROP_LOCK;
	spin_unlock(&lockres->l_lock);

	mlog(0, "lock %s\n", lockres->l_name);

	status = dlmunlock(osb->dlm,
			   &lockres->l_lksb,
			   LKM_VALBLK,
			   lockres->l_ops->unlock_ast,
			   lockres);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmunlock", status, lockres);
		mlog(ML_ERROR, "lockres flags: %lu\n", lockres->l_flags);
		dlm_print_one_lock(lockres->l_lksb.lockid);
		BUG();
	}
	mlog(0, "lock %s, successfull return from dlmunlock\n",
	     lockres->l_name);

	ocfs2_wait_on_busy_lock(lockres);
bail:
	mlog_exit(ret);
	return ret;
}

typedef void (ocfs2_pre_drop_cb_t)(struct ocfs2_lock_res *, void *);

struct drop_lock_cb {
	ocfs2_pre_drop_cb_t	*drop_func;
	void			*drop_data;
};

static int ocfs2_drop_lock(ocfs2_super *osb,
			   struct ocfs2_lock_res *lockres,
			   struct drop_lock_cb *dcb)
{
	/* We didn't get anywhere near actually using this lockres. */
	if (!(lockres->l_flags & OCFS2_LOCK_INITIALIZED))
		return 0;

	spin_lock(&lockres->l_lock);

	mlog_bug_on_msg(!(lockres->l_flags & OCFS2_LOCK_FREEING),
			"lockres %s, flags 0x%lx\n",
			lockres->l_name, lockres->l_flags);

	while (lockres->l_flags & OCFS2_LOCK_BUSY) {
		mlog(0, "waiting on busy lock \"%s\": flags = %lx, action = "
		     "%u, unlock_action = %u\n",
		     lockres->l_name, lockres->l_flags, lockres->l_action,
		     lockres->l_unlock_action);

		spin_unlock(&lockres->l_lock);

		/* XXX: Today we just wait on any busy
		 * locks... Perhaps we need to cancel converts in the
		 * future? */
		ocfs2_wait_on_busy_lock(lockres);

		spin_lock(&lockres->l_lock);
	}

	if (dcb)
		dcb->drop_func(lockres, dcb->drop_data);

	/* This will drop the spinlock for us. Dur de dur, at least we
	 * keep the ugliness in one place :) */
	return  __ocfs2_drop_lock(osb, lockres);
}

/* Mark the lockres as being dropped. It will no longer be
 * queued if blocking, but we still may have to wait on it
 * being dequeued from the vote thread before we can consider
 * it safe to drop. 
 *
 * You can *not* attempt to call cluster_lock on this lockres anymore. */
void ocfs2_mark_lockres_freeing(struct ocfs2_lock_res *lockres)
{
	int status;
	struct ocfs2_status_completion sc;
	struct ocfs2_lockres_flag_callback fcb;

	ocfs2_init_completion_fcb(&fcb, &sc);

	spin_lock(&lockres->l_lock);
	lockres->l_flags |= OCFS2_LOCK_FREEING;
	while (lockres->l_flags & OCFS2_LOCK_QUEUED) {
		lockres_add_flag_callback(lockres, &fcb, OCFS2_LOCK_QUEUED, 0);
		spin_unlock(&lockres->l_lock);

		mlog(0, "Waiting on lockres %s\n", lockres->l_name);

		status = ocfs2_wait_for_status_completion(&sc);
		if (status)
			mlog_errno(status);

		spin_lock(&lockres->l_lock);
	}
	spin_unlock(&lockres->l_lock);
}

static void ocfs2_drop_osb_locks(ocfs2_super *osb)
{
	int status;

	mlog_entry_void();

	ocfs2_mark_lockres_freeing(&osb->osb_super_lockres);

	status = ocfs2_drop_lock(osb, &osb->osb_super_lockres, NULL);
	if (status < 0)
		mlog_errno(status);

	ocfs2_mark_lockres_freeing(&osb->osb_rename_lockres);

	status = ocfs2_drop_lock(osb, &osb->osb_rename_lockres, NULL);
	if (status < 0)
		mlog_errno(status);

	mlog_exit(status);
}

static void ocfs2_meta_pre_drop(struct ocfs2_lock_res *lockres, void *data)
{
	struct inode *inode = data;

	/* the metadata lock requires a bit more work as we have an
	 * LVB to worry about. */
	if (lockres->l_flags & OCFS2_LOCK_ATTACHED &&
	    lockres->l_level == LKM_EXMODE &&
	    !(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH))
		__ocfs2_stuff_meta_lvb(inode);
}

int ocfs2_drop_inode_locks(struct inode *inode)
{
	int status, err;
	struct drop_lock_cb meta_dcb = { ocfs2_meta_pre_drop, inode, };

	mlog_entry_void();

	/* No need to call ocfs2_mark_lockres_freeing here -
	 * ocfs2_clear_inode has done it for us. */

	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_data_lockres,
			      NULL);
	if (err < 0)
		mlog_errno(err);

	status = err;

	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_meta_lockres,
			      &meta_dcb);
	if (err < 0)
		mlog_errno(err);
	if (err < 0 && !status)
		status = err;

	mlog_exit(status);
	return status;
}

/* called with the spinlock held, and WILL drop it. */
static int __ocfs2_downconvert_lock(ocfs2_super *osb,
				    struct ocfs2_lock_res *lockres,
				    int new_level,
				    int lvb)
{
	int ret, flags = LKM_CONVERT;
	enum dlm_status status;

	mlog_entry_void();

	BUG_ON(lockres->l_blocking <= LKM_NLMODE);

	if (lockres->l_level <= new_level) {
		mlog(ML_ERROR, "lockres->l_level (%u) <= new_level (%u)\n",
		     lockres->l_level, new_level);
		BUG();
	}

	mlog(0, "lock %s, new_level = %d, l_blocking = %d, lvb = %d\n",
	     lockres->l_name, new_level, lockres->l_blocking, lvb);

	lockres->l_action = OCFS2_AST_DOWNCONVERT;
	lockres->l_requested = new_level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	spin_unlock(&lockres->l_lock);

	if (lvb)
		flags |= LKM_VALBLK;

	status = dlmlock(osb->dlm,
			 new_level,
			 &lockres->l_lksb,
			 flags,
			 lockres->l_name,
			 lockres->l_ops->ast,
			 lockres,
			 lockres->l_ops->bast);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmlock", status, lockres);
		ret = -EINVAL;
		ocfs2_recover_from_dlm_error(lockres, 1);
		goto bail;
	}

	ret = 0;
bail:
	mlog_exit(ret);
	return ret;
}

/* called with the spinlock held, and WILL drop it. */
static int __ocfs2_cancel_convert(ocfs2_super *osb,
				  struct ocfs2_lock_res *lockres)
{
	int ret;
	enum dlm_status status;

	mlog_entry_void();

	mlog(0, "lock %s\n", lockres->l_name);

	/* were we in a convert when we got the bast fire? */
	BUG_ON(lockres->l_action != OCFS2_AST_CONVERT &&
	       lockres->l_action != OCFS2_AST_DOWNCONVERT);
	/* set things up for the unlockast to know to just
	 * clear out the ast_action and unset busy, etc. */
	lockres->l_unlock_action = OCFS2_UNLOCK_CANCEL_CONVERT;

	mlog_bug_on_msg(!(lockres->l_flags & OCFS2_LOCK_BUSY),
			"lock %s, invalid flags: 0x%lx\n",
			lockres->l_name, lockres->l_flags);
	spin_unlock(&lockres->l_lock);

	ret = 0;
	status = dlmunlock(osb->dlm,
			   &lockres->l_lksb,
			   LKM_CANCEL,
			   lockres->l_ops->unlock_ast,
			   lockres);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmunlock", status, lockres);
		ret = -EINVAL;
		ocfs2_recover_from_dlm_error(lockres, 0);
	}

	mlog(0, "lock %s return from dlmunlock\n", lockres->l_name);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_cancel_convert(ocfs2_super *osb,
				struct ocfs2_lock_res *lockres)
{
	assert_spin_locked(&lockres->l_lock);

	if (lockres->l_unlock_action == OCFS2_UNLOCK_CANCEL_CONVERT) {
		/* If we're already trying to cancel a lock conversion
		 * then just drop the spinlock and allow the caller to
		 * requeue this lock. */
		spin_unlock(&lockres->l_lock);

		mlog(0, "Lockres %s, skip convert\n", lockres->l_name);
		return 0;
	}

	/* this will drop the spinlock for us. */
	return __ocfs2_cancel_convert(osb, lockres);
}

static inline int ocfs2_can_downconvert_meta_lock(struct inode *inode,
						  struct ocfs2_lock_res *lockres,
						  int new_level)
{
	int ret;

	mlog_entry_void();

	BUG_ON(new_level != LKM_NLMODE && new_level != LKM_PRMODE);

	if (lockres->l_flags & OCFS2_LOCK_REFRESHING) {
		ret = 0;
		mlog(0, "lockres %s currently being refreshed -- backing "
		     "off!\n", lockres->l_name);
	} else if (new_level == LKM_PRMODE)
		ret = !lockres->l_ex_holders &&
			ocfs2_inode_fully_checkpointed(inode);
	else /* Must be NLMODE we're converting to. */
		ret = !lockres->l_ro_holders && !lockres->l_ex_holders &&
			ocfs2_inode_fully_checkpointed(inode);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_do_unblock_meta(struct inode *inode,
				 int *requeue)
{
	int new_level;
	int set_lvb = 0;
	int ret = 0;
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_meta_lockres;
	ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry_void();

	spin_lock(&lockres->l_lock);

	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));

	mlog(0, "l_level=%d, l_blocking=%d\n", lockres->l_level,
	     lockres->l_blocking);

	BUG_ON(lockres->l_level != LKM_EXMODE &&
	       lockres->l_level != LKM_PRMODE);

	if (lockres->l_flags & OCFS2_LOCK_BUSY) {
		*requeue = 1;
		ret = ocfs2_cancel_convert(osb, lockres);
		if (ret < 0)
			mlog_errno(ret);
		goto leave;
	}

	new_level = ocfs2_highest_compat_lock_level(lockres->l_blocking);

	mlog(0, "l_level=%d, l_blocking=%d, new_level=%d\n",
	     lockres->l_level, lockres->l_blocking, new_level);

	if (ocfs2_can_downconvert_meta_lock(inode, lockres, new_level)) {
		if (lockres->l_level == LKM_EXMODE)
			set_lvb = 1;

		/* If the lock hasn't been refreshed yet (rare), then
		 * our memory inode values are old and we skip
		 * stuffing the lvb. There's no need to actually clear
		 * out the lvb here as it's value is still valid. */
		if (!(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH)) {
			if (set_lvb)
				__ocfs2_stuff_meta_lvb(inode);
		} else
			mlog(0, "lockres %s: downconverting stale lock!\n",
			     lockres->l_name);

		mlog(0, "calling __ocfs2_downconvert_lock with "
		     "l_level=%d, l_blocking=%d, new_level=%d\n",
		     lockres->l_level, lockres->l_blocking,
		     new_level);
		ret = __ocfs2_downconvert_lock(osb, lockres, new_level,
					       set_lvb);
		goto leave;
	}
	if (!ocfs2_inode_fully_checkpointed(inode))
		ocfs2_start_checkpoint(osb);

	*requeue = 1;
	spin_unlock(&lockres->l_lock);
	ret = 0;
leave:
	mlog_exit(ret);
	return ret;
}

static int ocfs2_generic_unblock_lock(ocfs2_super *osb,
				      struct ocfs2_lock_res *lockres,
				      int *requeue,
				      ocfs2_convert_worker_t *worker)
{
	int blocking;
	int new_level;
	int ret = 0;

	mlog_entry_void();

	spin_lock(&lockres->l_lock);

	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));

recheck:
	if (lockres->l_flags & OCFS2_LOCK_BUSY) {
		*requeue = 1;
		ret = ocfs2_cancel_convert(osb, lockres);
		if (ret < 0)
			mlog_errno(ret);
		goto leave;
	}

	/* if we're blocking an exclusive and we have *any* holders,
	 * then requeue. */
	if ((lockres->l_blocking == LKM_EXMODE)
	    && (lockres->l_ex_holders || lockres->l_ro_holders)) {
		spin_unlock(&lockres->l_lock);
		*requeue = 1;
		ret = 0;
		goto leave;
	}

	/* If it's a PR we're blocking, then only
	 * requeue if we've got any EX holders */
	if (lockres->l_blocking == LKM_PRMODE &&
	    lockres->l_ex_holders) {
		spin_unlock(&lockres->l_lock);
		*requeue = 1;
		ret = 0;
		goto leave;
	}

	/* If we get here, then we know that there are no more
	 * incompatible holders (and anyone asking for an incompatible
	 * lock is blocked). We can now downconvert the lock */
	if (!worker)
		goto downconvert;

	/* Some lockres types want to do a bit of work before
	 * downconverting a lock. Allow that here. The worker function
	 * may sleep, so we save off a copy of what we're blocking as
	 * it may change while we're not holding the spin lock. */
	blocking = lockres->l_blocking;
	spin_unlock(&lockres->l_lock);

	worker(lockres, blocking);

	spin_lock(&lockres->l_lock);
	if (blocking != lockres->l_blocking) {
		/* If this changed underneath us, then we can't drop
		 * it just yet. */
		goto recheck;
	}

downconvert:
	*requeue = 0;
	new_level = ocfs2_highest_compat_lock_level(lockres->l_blocking);

	ret = __ocfs2_downconvert_lock(osb, lockres, new_level, 0);
leave:
	mlog_exit(ret);
	return ret;
}

static void ocfs2_data_convert_worker(struct ocfs2_lock_res *lockres,
				      int blocking)
{
	struct inode *inode;
	struct address_space *mapping;

	mlog_entry_void();

       	inode = ocfs2_lock_res_inode(lockres);
	mapping = inode->i_mapping;

	if (filemap_fdatawrite(mapping)) {
		mlog(ML_ERROR, "Could not sync inode %"MLFu64" for downconvert!",
		     OCFS2_I(inode)->ip_blkno);
	}
	sync_mapping_buffers(mapping);
	if (blocking == LKM_EXMODE) {
		truncate_inode_pages(mapping, 0);
		unmap_mapping_range(mapping, 0, 0, 0);
	} else {
		/* We only need to wait on the I/O if we're not also
		 * truncating pages because truncate_inode_pages waits
		 * for us above. We don't truncate pages if we're
		 * blocking anything < EXMODE because we want to keep
		 * them around in that case. */
		filemap_fdatawait(mapping);
	}

	mlog_exit_void();
}

int ocfs2_unblock_data(struct ocfs2_lock_res *lockres,
		       int *requeue)
{
	int status;
	struct inode *inode;
	ocfs2_super *osb;

	mlog_entry_void();

	inode = ocfs2_lock_res_inode(lockres);
	osb = OCFS2_SB(inode->i_sb);

	mlog(0, "unblock inode %"MLFu64"\n", OCFS2_I(inode)->ip_blkno);

	status = ocfs2_generic_unblock_lock(osb,
					    lockres,
					    requeue,
					    ocfs2_data_convert_worker);
	if (status < 0)
		mlog_errno(status);

	mlog(0, "inode %"MLFu64", requeue = %d\n",
	     OCFS2_I(inode)->ip_blkno, *requeue);

	mlog_exit(status);
	return status;
}

int ocfs2_unblock_meta(struct ocfs2_lock_res *lockres,
		       int *requeue)
{
	int status;
	struct inode *inode;

	mlog_entry_void();

       	inode = ocfs2_lock_res_inode(lockres);

	mlog(0, "unblock inode %"MLFu64"\n", OCFS2_I(inode)->ip_blkno);

	status = ocfs2_do_unblock_meta(inode, requeue);
	if (status < 0)
		mlog_errno(status);

	mlog(0, "inode %"MLFu64", requeue = %d\n",
	     OCFS2_I(inode)->ip_blkno, *requeue);

	mlog_exit(status);
	return status;
}

/* Generic unblock function for any lockres whose private data is an
 * ocfs2_super pointer. */
static int ocfs2_unblock_osb_lock(struct ocfs2_lock_res *lockres,
				  int *requeue)
{
	int status;
	ocfs2_super *osb;

	mlog_entry_void();

	mlog(0, "Unblock lockres %s\n", lockres->l_name);

	osb = ocfs2_lock_res_super(lockres);

	status = ocfs2_generic_unblock_lock(osb,
					    lockres,
					    requeue,
					    NULL);
	if (status < 0)
		mlog_errno(status);

	mlog_exit(status);
	return status;
}

void ocfs2_process_blocked_lock(ocfs2_super *osb,
				struct ocfs2_lock_res *lockres)
{
	int status;
	int requeue = 0;

	/* Our reference to the lockres in this function can be
	 * considered valid until we remove the OCFS2_LOCK_QUEUED
	 * flag. */

	mlog_entry_void();

	BUG_ON(!lockres);
	BUG_ON(!lockres->l_ops);
	BUG_ON(!lockres->l_ops->unblock);

	mlog(0, "lockres %s blocked.\n", lockres->l_name);

	/* Detect whether a lock has been marked as going away while
	 * the vote thread was processing other things. A lock can
	 * still be marked with OCFS2_LOCK_FREEING after this check,
	 * but short circuiting here will still save us some
	 * performance. */
	spin_lock(&lockres->l_lock);
	if (lockres->l_flags & OCFS2_LOCK_FREEING)
		goto unqueue;
	spin_unlock(&lockres->l_lock);

	status = lockres->l_ops->unblock(lockres, &requeue);
	if (status < 0)
		mlog_errno(status);

	spin_lock(&lockres->l_lock);
unqueue:
	if (lockres->l_flags & OCFS2_LOCK_FREEING || !requeue) {
		lockres_clear_flags(lockres, OCFS2_LOCK_QUEUED);
	} else
		ocfs2_schedule_blocked_lock(osb, lockres);

	mlog(0, "lockres %s, requeue = %s.\n", lockres->l_name,
	     requeue ? "yes" : "no");
	spin_unlock(&lockres->l_lock);

	mlog_exit_void();
}

static void ocfs2_schedule_blocked_lock(ocfs2_super *osb,
					struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	assert_spin_locked(&lockres->l_lock);

	if (lockres->l_flags & OCFS2_LOCK_FREEING) {
		/* Do not schedule a lock for downconvert when it's on
		 * the way to destruction - any nodes wanting access
		 * to the resource will get it soon. */
		mlog(0, "Lockres %s won't be scheduled: flags 0x%lx\n",
		     lockres->l_name, lockres->l_flags);
		return;
	}

	lockres_or_flags(lockres, OCFS2_LOCK_QUEUED);

	spin_lock(&osb->vote_task_lock);
	if (list_empty(&lockres->l_blocked_list)) {
		list_add_tail(&lockres->l_blocked_list,
			      &osb->blocked_lock_list);
		osb->blocked_lock_count++;
	}
	spin_unlock(&osb->vote_task_lock);

	mlog_exit_void();
}

/* This aids in debugging situations where a bad LVB might be involved. */
void ocfs2_dump_meta_lvb_info(u64 level,
			      const char *function,
			      unsigned int line,
			      struct ocfs2_lock_res *lockres)
{
	struct ocfs2_meta_lvb *lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	mlog(level, "LVB information for %s (called from %s:%u):\n",
	     lockres->l_name, function, line);
	mlog(level, "old_seq: %u, version: %u, clusters: %u\n",
	     be32_to_cpu(lvb->lvb_old_seq), be32_to_cpu(lvb->lvb_version),
	     be32_to_cpu(lvb->lvb_iclusters));
	mlog(level, "size: %"MLFu64", uid %u, gid %u, mode 0x%x\n",
	     be64_to_cpu(lvb->lvb_isize), be32_to_cpu(lvb->lvb_iuid),
	     be32_to_cpu(lvb->lvb_igid), be16_to_cpu(lvb->lvb_imode));
	mlog(level, "nlink %u, atime_packed 0x%"MLFx64", "
	     "ctime_packed 0x%"MLFx64", mtime_packed 0x%"MLFx64"\n",
	     be16_to_cpu(lvb->lvb_inlink), be64_to_cpu(lvb->lvb_iatime_packed),
	     be64_to_cpu(lvb->lvb_ictime_packed),
	     be64_to_cpu(lvb->lvb_imtime_packed));
}
