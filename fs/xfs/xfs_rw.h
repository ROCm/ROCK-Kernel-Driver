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
#ifndef	__XFS_RW_H__
#define	__XFS_RW_H__

struct bhv_desc;
struct bmapval;
struct xfs_buf;
struct cred;
struct uio;
struct vnode;
struct xfs_inode;
struct xfs_iocore;
struct xfs_mount;
struct xfs_trans;

/*
 * Maximum count of bmaps used by read and write paths.
 */
#define	XFS_MAX_RW_NBMAPS	4

/*
 * Counts of readahead buffers to use based on physical memory size.
 * None of these should be more than XFS_MAX_RW_NBMAPS.
 */
#define	XFS_RW_NREADAHEAD_16MB	2
#define	XFS_RW_NREADAHEAD_32MB	3
#define	XFS_RW_NREADAHEAD_K32	4
#define	XFS_RW_NREADAHEAD_K64	4

/*
 * Maximum size of a buffer that we\'ll map.  Making this
 * too big will degrade performance due to the number of
 * pages which need to be gathered.  Making it too small
 * will prevent us from doing large I/O\'s to hardware that
 * needs it.
 *
 * This is currently set to 512 KB.
 */
#define	XFS_MAX_BMAP_LEN_BB	1024
#define	XFS_MAX_BMAP_LEN_BYTES	524288

/*
 * Convert the given file system block to a disk block.
 * We have to treat it differently based on whether the
 * file is a real time file or not, because the bmap code
 * does.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_FSB_TO_DB)
xfs_daddr_t xfs_fsb_to_db(struct xfs_inode *ip, xfs_fsblock_t fsb);
#define	XFS_FSB_TO_DB(ip,fsb)	xfs_fsb_to_db(ip,fsb)
#else
#define	XFS_FSB_TO_DB(ip,fsb) \
		(((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME) ? \
		 (xfs_daddr_t)XFS_FSB_TO_BB((ip)->i_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((ip)->i_mount, (fsb)))
#endif

#define XFS_FSB_TO_DB_IO(io,fsb) \
		(((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)))

/*
 * Defines for the trace mechanisms in xfs_rw.c.
 */
#define	XFS_RW_KTRACE_SIZE	64
#define	XFS_STRAT_KTRACE_SIZE	64
#define	XFS_STRAT_GTRACE_SIZE	512

#define	XFS_READ_ENTER		1
#define	XFS_WRITE_ENTER		2
#define XFS_IOMAP_READ_ENTER	3
#define	XFS_IOMAP_WRITE_ENTER	4
#define	XFS_IOMAP_READ_MAP	5
#define	XFS_IOMAP_WRITE_MAP	6
#define	XFS_IOMAP_WRITE_NOSPACE	7
#define	XFS_ITRUNC_START	8
#define	XFS_ITRUNC_FINISH1	9
#define	XFS_ITRUNC_FINISH2	10
#define	XFS_CTRUNC1		11
#define	XFS_CTRUNC2		12
#define	XFS_CTRUNC3		13
#define	XFS_CTRUNC4		14
#define	XFS_CTRUNC5		15
#define	XFS_CTRUNC6		16
#define	XFS_BUNMAPI		17
#define	XFS_INVAL_CACHED	18
#define	XFS_DIORD_ENTER		19
#define	XFS_DIOWR_ENTER		20

#if defined(XFS_ALL_TRACE)
#define	XFS_RW_TRACE
#define	XFS_STRAT_TRACE
#endif

#if !defined(DEBUG)
#undef XFS_RW_TRACE
#undef XFS_STRAT_TRACE
#endif

/*
 * Prototypes for functions in xfs_rw.c.
 */

int
xfs_write_clear_setuid(
	struct xfs_inode	*ip);

int
xfs_bwrite(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp);

void
xfs_inval_cached_pages(
	struct vnode		*vp,
	struct xfs_iocore	*io,
	xfs_off_t		offset,
	int			write,
	int			relock);

int
xfs_bioerror(
	struct xfs_buf		*b);

int
xfs_bioerror_relse(
	struct xfs_buf		*b);

int
xfs_read_buf(
	struct xfs_mount	*mp,
	xfs_buftarg_t		*target,
	xfs_daddr_t		blkno,
	int			len,
	uint			flags,
	struct xfs_buf		**bpp);

void
xfs_ioerror_alert(
	char			*func,
	struct xfs_mount	*mp,
	xfs_buf_t		*bp,
	xfs_daddr_t		blkno);


/*
 * Prototypes for functions in xfs_vnodeops.c.
 */

int
xfs_rwlock(
	bhv_desc_t		*bdp,
	vrwlock_t		write_lock);

void
xfs_rwunlock(
	bhv_desc_t		*bdp,
	vrwlock_t		write_lock);

int
xfs_change_file_space(
	bhv_desc_t		*bdp,
	int			cmd,
	xfs_flock64_t		*bf,
	xfs_off_t		offset,
	cred_t			*credp,
	int			flags);

int
xfs_set_dmattrs(
	bhv_desc_t		*bdp,
	u_int			evmask,
	u_int16_t		state,
	cred_t			*credp);

#endif /* __XFS_RW_H__ */
