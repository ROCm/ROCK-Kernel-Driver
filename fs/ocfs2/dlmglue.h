/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmglue.h
 *
 * description here
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


#ifndef DLMGLUE_H
#define DLMGLUE_H

/*
 * LVB Sequence number rules:
 * local seq and lvb seq are initialized to zero.
 *
 * Note that the lvb is basically invalid until the 1st EX downconvert
 * as he's the only guy that can set it valid. This is ok though as PR
 * holders would have to do an I/O under lock anyway.
 *
 * NL->PR:
 * NL->EX:
 * If LVB is valid:
 *   if local seq == lvb seq, then we are up to date with the contents.
 *   otherwise, we take the slow path to get up to date and then set our
 *   local seq to the lvb seq.
 *
 * PR->NL:
 * If LVB is valid:
 *   We increment our local seq. -- this allows up to
 *   one set of changes to the lvb before we considers ourselves
 *   invalid.
 *
 * PR->EX:
 *   Do nothing.
 *
 * EX->NL:
 * EX->PR:
 * Set the LVB as valid.
 * Populate the LVB contents (this is lock type specific)
 * Increment the LVB seq.
 * Set my local seq to the LVB seq.
 * if (EX->NL)
 *   do an additional increment of my local seq.
 */
struct ocfs2_lvb {
	__be32 lvb_seq;
};
struct ocfs2_meta_lvb {
	struct ocfs2_lvb lvb;
	__be32       lvb_trunc_clusters;
	__be32       lvb_iclusters;
	__be32       lvb_iuid;
	__be32       lvb_igid;
	__be16       lvb_imode;
	__be16       lvb_inlink;
	__be64       lvb_iatime_packed;
	__be64       lvb_ictime_packed;
	__be64       lvb_imtime_packed;
	__be32       lvb_isize_off;
	__be32       lvb_reserved[3];
};

int ocfs2_dlm_init(ocfs2_super *osb);
void ocfs2_dlm_shutdown(ocfs2_super *osb);
void ocfs2_lock_res_init_once(struct ocfs2_lock_res *res);
void ocfs2_inode_lock_res_init(struct ocfs2_lock_res *res,
			       enum ocfs2_lock_type type,
			       struct inode *inode);
void ocfs2_lock_res_free(struct ocfs2_lock_res *res);
int ocfs2_create_new_inode_locks(struct inode *inode);
int ocfs2_drop_inode_locks(struct inode *inode);
int ocfs2_data_lock(struct inode *inode,
		    int write);
void ocfs2_data_unlock(struct inode *inode,
		       int write);
/* don't wait on recovery. */
#define OCFS2_META_LOCK_RECOVERY	(0x01)
/* Instruct the dlm not to queue ourselves on the other node. */
#define OCFS2_META_LOCK_NOQUEUE		(0x02)
int ocfs2_meta_lock_full(struct inode *inode,
			 ocfs2_journal_handle *handle,
			 struct buffer_head **ret_bh,
			 int ex,
			 int flags,
			 ocfs2_lock_callback cb,
			 unsigned long cb_data);
/* 99% of the time we don't want to supply any additional flags --
 * those are for very specific cases only. */
#define ocfs2_meta_lock(i, h, b, e) ocfs2_meta_lock_full(i, h, b, e, 0, NULL, 0)
void ocfs2_meta_unlock(struct inode *inode,
		       int ex);
int ocfs2_super_lock(ocfs2_super *osb,
		     int ex);
void ocfs2_super_unlock(ocfs2_super *osb,
			int ex);
int ocfs2_rename_lock(ocfs2_super *osb);
void ocfs2_rename_unlock(ocfs2_super *osb);
void ocfs2_mark_lockres_freeing(struct ocfs2_lock_res *lockres);

/* for the vote thread */
void ocfs2_process_blocked_lock(ocfs2_super *osb,
				struct ocfs2_lock_res *lockres);

void ocfs2_meta_lvb_set_trunc_clusters(struct inode *inode,
				       unsigned int trunc_clusters);

#endif	/* DLMGLUE_H */
