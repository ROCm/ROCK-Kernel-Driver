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

/*
 * Implementation for VFS_DOUNMOUNT.
 */
int
fs_dounmount(
	bhv_desc_t	*bdp,
	int		flags,
	vnode_t		*rootvp,
	cred_t		*cr)
{
	struct vfs	*vfsp = bhvtovfs(bdp);
	bhv_desc_t	*fbdp = vfsp->vfs_fbhv;
	int		error;

	/*
	 * Wait for sync to finish and lock vfsp.  This also sets the
	 * VFS_OFFLINE flag.  Once we do this we can give up reference
	 * the root vnode which we hold to avoid the another unmount
	 * ripping the vfs out from under us before we get to lock it.
	 * The VFS_DOUNMOUNT calling convention is that the reference
	 * on the rot vnode is released whether the call succeeds or
	 * fails.
	 */
	if (rootvp)
		VN_RELE(rootvp);

	/*
	 * Now invoke SYNC and UNMOUNT ops, using the PVFS versions is
	 * OK since we already have a behavior lock as a result of
	 * being in VFS_DOUNMOUNT.  It is necessary to do things this
	 * way since using the VFS versions would cause us to get the
	 * behavior lock twice which can cause deadlock as well as
	 * making the coding of vfs relocation unnecessarilty difficult
	 * by making relocations invoked by unmount occur in a different
	 * environment than those invoked by mount-update.
	 */
	PVFS_SYNC(fbdp, SYNC_ATTR|SYNC_DELWRI, cr, error);
	if (error == 0)
		PVFS_UNMOUNT(fbdp, flags, cr, error);
	return error;
}

/*
 * Stub for no-op vnode operations that return error status.
 */
int
fs_noerr()
{
	return 0;
}

/*
 * Operation unsupported under this file system.
 */
int
fs_nosys()
{
	return ENOSYS;
}

/*
 * Stub for inactive, strategy, and read/write lock/unlock.  Does nothing.
 */
/* ARGSUSED */
void
fs_noval()
{
}

/*
 * vnode pcache layer for vnode_tosspages.
 * 'last' parameter unused but left in for IRIX compatibility
 */
void
fs_tosspages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = LINVFS_GET_IP(vp);

	if (VN_CACHED(vp))
		truncate_inode_pages(ip->i_mapping, first);
}


/*
 * vnode pcache layer for vnode_flushinval_pages.
 * 'last' parameter unused but left in for IRIX compatibility
 */
void
fs_flushinval_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = LINVFS_GET_IP(vp);

	if (VN_CACHED(vp)) {
		filemap_fdatawait(ip->i_mapping);
		filemap_fdatawrite(ip->i_mapping);
		filemap_fdatawait(ip->i_mapping);

		truncate_inode_pages(ip->i_mapping, first);
	}
}

/*
 * vnode pcache layer for vnode_flush_pages.
 * 'last' parameter unused but left in for IRIX compatibility
 */
int
fs_flush_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	uint64_t	flags,
	int		fiopt)
{
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = LINVFS_GET_IP(vp);

	if (VN_CACHED(vp)) {
		filemap_fdatawait(ip->i_mapping);
		filemap_fdatawrite(ip->i_mapping);
		filemap_fdatawait(ip->i_mapping);
	}

	return 0;
}

