/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
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

#ifndef __XFS_LRW_H__
#define __XFS_LRW_H__

#define XFS_IOMAP_READ_ENTER	3
/*
 * Maximum count of bmaps used by read and write paths.
 */
#define XFS_MAX_RW_NBMAPS	4

extern int xfs_bmap (bhv_desc_t *, xfs_off_t, ssize_t, int, struct cred *, pb_bmap_t *, int *);
extern int xfs_strategy (bhv_desc_t *, xfs_off_t, ssize_t, int, struct cred *, pb_bmap_t *, int *);
extern int xfsbdstrat (struct xfs_mount *, struct xfs_buf *);
extern int xfs_bdstrat_cb (struct xfs_buf *);

extern int xfs_zero_eof (vnode_t *, struct xfs_iocore *, xfs_off_t,
				xfs_fsize_t, xfs_fsize_t, struct pm *);
extern ssize_t xfs_read (
	struct bhv_desc		*bdp,
	struct file		*filp,
	const struct iovec	*iovp,
	unsigned long		segs,
	loff_t			*offp,
	struct cred		*credp);

extern ssize_t xfs_write (
	struct bhv_desc		*bdp,
	struct file		*filp,
	const struct iovec	*iovp,
	unsigned long		segs,
	loff_t			*offp,
	struct cred		*credp);

extern int xfs_recover_read_only (xlog_t *);
extern int xfs_quotacheck_read_only (xfs_mount_t *);

extern void XFS_log_write_unmount_ro (bhv_desc_t *);

#define XFS_FSB_TO_DB_IO(io,fsb) \
		(((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)))

#endif	/* __XFS_LRW_H__ */
