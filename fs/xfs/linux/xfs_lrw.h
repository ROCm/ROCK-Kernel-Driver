/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
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
 * or the like.  Any license provided herein, whether implied or
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

struct vnode;
struct bhv_desc;
struct xfs_mount;
struct xfs_iocore;
struct xfs_inode;
struct xfs_bmbt_irec;
struct page_buf_s;
struct page_buf_bmap_s;

#define XFS_IOMAP_READ_ENTER	3
/*
 * Maximum count of bmaps used by read and write paths.
 */
#define	XFS_MAX_RW_NBMAPS	4

extern int xfs_bmap(struct bhv_desc *, xfs_off_t, ssize_t, int,
			struct page_buf_bmap_s *, int *);
extern int xfsbdstrat(struct xfs_mount *, struct page_buf_s *);
extern int xfs_bdstrat_cb(struct page_buf_s *);

extern int xfs_zero_eof(struct vnode *, struct xfs_iocore *, xfs_off_t,
				xfs_fsize_t, xfs_fsize_t);
extern ssize_t xfs_read(struct bhv_desc *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
extern ssize_t xfs_write(struct bhv_desc *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
extern ssize_t xfs_sendfile(struct bhv_desc *, struct file *,
				loff_t *, int, size_t, read_actor_t,
				void *, struct cred *);

extern int xfs_iomap(struct xfs_iocore *, xfs_off_t, ssize_t, int,
				struct page_buf_bmap_s *, int *);
extern int xfs_iomap_write_direct(struct xfs_inode *, loff_t, size_t,
				int, struct xfs_bmbt_irec *, int *, int);
extern int xfs_iomap_write_delay(struct xfs_inode *, loff_t, size_t,
				int, struct xfs_bmbt_irec *, int *);
extern int xfs_iomap_write_allocate(struct xfs_inode *,
				struct xfs_bmbt_irec *, int *);
extern int xfs_iomap_write_unwritten(struct xfs_inode *, loff_t, size_t);

extern int xfs_dev_is_read_only(struct xfs_mount *, char *);

#define XFS_FSB_TO_DB_IO(io,fsb) \
		(((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)))

#endif	/* __XFS_LRW_H__ */
