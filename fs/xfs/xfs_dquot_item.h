/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_DQUOT_ITEM_H__
#define __XFS_DQUOT_ITEM_H__

/*
 * These are the structures used to lay out dquots and quotaoff
 * records on the log. Quite similar to those of inodes.
 */

/*
 * log format struct for dquots.
 * The first two fields must be the type and size fitting into
 * 32 bits : log_recovery code assumes that.
 */
typedef struct xfs_dq_logformat {
	__uint16_t		qlf_type;      /* dquot log item type */
	__uint16_t		qlf_size;      /* size of this item */
	xfs_dqid_t		qlf_id;	       /* usr/grp id number : 32 bits */
	__int64_t		qlf_blkno;     /* blkno of dquot buffer */
	__int32_t		qlf_len;       /* len of dquot buffer */
	__uint32_t		qlf_boffset;   /* off of dquot in buffer */
} xfs_dq_logformat_t;

/*
 * log format struct for QUOTAOFF records.
 * The first two fields must be the type and size fitting into
 * 32 bits : log_recovery code assumes that.
 * We write two LI_QUOTAOFF logitems per quotaoff, the last one keeps a pointer
 * to the first and ensures that the first logitem is taken out of the AIL
 * only when the last one is securely committed.
 */
typedef struct xfs_qoff_logformat {
	unsigned short		qf_type;	/* quotaoff log item type */
	unsigned short		qf_size;	/* size of this item */
	unsigned int		qf_flags;	/* USR and/or GRP */
	char			qf_pad[12];	/* padding for future */
} xfs_qoff_logformat_t;


#ifdef __KERNEL__

struct xfs_dquot;
struct xfs_trans;
struct xfs_mount;
typedef struct xfs_dq_logitem {
	xfs_log_item_t		 qli_item;	   /* common portion */
	struct xfs_dquot	*qli_dquot;	   /* dquot ptr */
	xfs_lsn_t		 qli_flush_lsn;	   /* lsn at last flush */
	unsigned short		 qli_pushbuf_flag; /* one bit used in push_ail */
#ifdef DEBUG
	uint64_t		 qli_push_owner;
#endif
	xfs_dq_logformat_t	 qli_format;	   /* logged structure */
} xfs_dq_logitem_t;


typedef struct xfs_qoff_logitem {
	xfs_log_item_t		 qql_item;	/* common portion */
	struct xfs_qoff_logitem *qql_start_lip; /* qoff-start logitem, if any */
	xfs_qoff_logformat_t	 qql_format;	/* logged structure */
} xfs_qoff_logitem_t;


extern void		   xfs_qm_dquot_logitem_init(struct xfs_dquot *);
extern xfs_qoff_logitem_t *xfs_qm_qoff_logitem_init(struct xfs_mount *,
						    xfs_qoff_logitem_t *, uint);
extern xfs_qoff_logitem_t *xfs_trans_get_qoff_item(struct xfs_trans *,
						   xfs_qoff_logitem_t *, uint);
extern void		   xfs_trans_log_quotaoff_item(struct xfs_trans *,
						       xfs_qoff_logitem_t *);

#endif	/* __KERNEL__ */

#endif	/* __XFS_DQUOT_ITEM_H__ */
