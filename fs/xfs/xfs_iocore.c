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

#include <xfs.h>


static xfs_fsize_t
xfs_size_fn(
	xfs_inode_t	*ip)
{
	return (ip->i_d.di_size);
}

xfs_ioops_t	xfs_iocore_xfs = {
	.xfs_bmapi_func		= (xfs_bmapi_t) xfs_bmapi,
	.xfs_bmap_eof_func	= (xfs_bmap_eof_t) xfs_bmap_eof,
	.xfs_ilock		= (xfs_lock_t) xfs_ilock,
	.xfs_ilock_demote	= (xfs_lock_demote_t) xfs_ilock_demote,
	.xfs_ilock_nowait	= (xfs_lock_nowait_t) xfs_ilock_nowait,
	.xfs_unlock		= (xfs_unlk_t) xfs_iunlock,
	.xfs_size_func		= (xfs_size_t) xfs_size_fn,
	.xfs_lastbyte		= (xfs_lastbyte_t) xfs_file_last_byte,
};

void
xfs_iocore_inode_reinit(
	xfs_inode_t	*ip)
{
	xfs_iocore_t	*io = &ip->i_iocore;

	io->io_flags = XFS_IOCORE_ISXFS;
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		io->io_flags |= XFS_IOCORE_RT;
	}

	io->io_dmevmask = ip->i_d.di_dmevmask;
	io->io_dmstate = ip->i_d.di_dmstate;
}

void
xfs_iocore_inode_init(
	xfs_inode_t	*ip)
{
	xfs_iocore_t	*io = &ip->i_iocore;
	xfs_mount_t	*mp = ip->i_mount;

	io->io_mount = mp;
#ifdef DEBUG
	io->io_lock = &ip->i_lock;
	io->io_iolock = &ip->i_iolock;
#endif

	io->io_obj = (void *)ip;

	xfs_iocore_inode_reinit(ip);
}

