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
#ifndef __XFS_UTILS_H__
#define __XFS_UTILS_H__

#define IRELE(ip)	VN_RELE(XFS_ITOV(ip))
#define IHOLD(ip)	VN_HOLD(XFS_ITOV(ip))
#define ITRACE(ip)	vn_trace_ref(XFS_ITOV(ip), __FILE__, __LINE__, \
				(inst_t *)__return_address)

struct bhv_desc;
struct cred;
struct vnode;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

extern int
xfs_rename(
	struct bhv_desc *src_dir_bdp,
	struct dentry	*src_dentry,
	struct vnode	*target_dir_vp,
	struct dentry	*target_dentry,
	struct cred	*credp);

extern int
xfs_get_dir_entry(
	struct dentry		*dentry,
	xfs_inode_t		**ipp);

extern int
xfs_dir_lookup_int(
	struct bhv_desc		*dir_bdp,
	uint			lock_mode,
	struct dentry		*dentry,
	xfs_ino_t		*inum,
	struct xfs_inode	**ipp);

extern int
xfs_truncate_file(
	struct xfs_mount	*mp,
	struct xfs_inode	*ip);

extern int
xfs_dir_ialloc(
	struct xfs_trans	**tpp,
	struct xfs_inode	*dp,
	mode_t			mode,
	nlink_t			nlink,
	dev_t			rdev,
	struct cred		*credp,
	prid_t			prid,
	int			okalloc,
	struct xfs_inode	**ipp,
	int			*committed);

extern int
xfs_droplink(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip);

extern int
xfs_bumplink(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip);

extern void
xfs_bump_ino_vers2(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip);

#endif /* XFS_UTILS_H */

