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
#ifndef __XFS_SUPER_H__
#define __XFS_SUPER_H__

#ifdef CONFIG_FS_POSIX_ACL
# define XFS_ACL_STRING		"ACLs, "
#else
# define XFS_ACL_STRING
#endif

#ifdef CONFIG_XFS_DMAPI
# define XFS_DMAPI_STRING	"DMAPI, "
#else
# define XFS_DMAPI_STRING
#endif

#ifdef CONFIG_XFS_QUOTA
# define XFS_QUOTA_STRING	"quota, "
#else
# define XFS_QUOTA_STRING
#endif

#ifdef CONFIG_XFS_RT
# define XFS_RT_STRING		"realtime, "
#else
# define XFS_RT_STRING
#endif

#ifdef CONFIG_XFS_VNODE_TRACING
# define XFS_VNTRACE_STRING	"VN-trace, "
#else
# define XFS_VNTRACE_STRING
#endif

#ifdef XFSDEBUG
# define XFS_DBG_STRING		"debug"
#else
# define XFS_DBG_STRING		"no debug"
#endif

#define XFS_BUILD_OPTIONS	XFS_ACL_STRING XFS_DMAPI_STRING \
				XFS_RT_STRING \
				XFS_QUOTA_STRING XFS_VNTRACE_STRING \
				XFS_DBG_STRING /* DBG must be last */


#define LINVFS_GET_VFS(s) \
	(vfs_t *)((s)->s_fs_info)
#define LINVFS_SET_VFS(s, vfsp) \
	((s)->s_fs_info = vfsp)


struct xfs_mount_args;

extern void
linvfs_set_inode_ops(
	struct inode	*inode);

extern int
spectodevs(
	struct super_block *sb,
	struct xfs_mount_args *args,
	dev_t		*ddevp,
	dev_t		*logdevp,
	dev_t		*rtdevp);

#endif	/* __XFS_SUPER_H__ */
