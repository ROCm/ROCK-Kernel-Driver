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

#define MNTOPT_DMAPI	"dmapi"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_XDSM	"xdsm"		/* DMI enabled (DMAPI / XDSM) */

STATIC int
xfs_dm_parseargs(
	struct bhv_desc		*bhv,
	char			*options,
	struct xfs_mount_args	*args,
	int			update)
{
	size_t			length;
	char			*local_options = options;
	char			*this_char;
	int			error;

	while ((this_char = strsep(&local_options, ",")) != NULL) {
		length = strlen(this_char);
		if (local_options)
			length++;

		if (!strcmp(this_char, MNTOPT_DMAPI)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_XDSM)) {
			args->flags |= XFSMNT_DMAPI;
		} else {
			if (local_options)
				*(local_options-1) = ',';
			continue;
		}

		while (length--)
			*this_char++ = ',';
	}

	PVFS_PARSEARGS(BHV_NEXT(bhv), options, args, update, error);
	if (!error && (args->flags & XFSMNT_DMAPI) && (*args->mtpt == '\0'))
		error = EINVAL;
	if (!error && !update && !(args->flags & XFSMNT_DMAPI))
		bhv_remove_vfsops(bhvtovfs(bhv), VFS_POSITION_DM);
	return error;
}

STATIC int
xfs_dm_showargs(
	struct bhv_desc		*bhv,
	struct seq_file		*m)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	int			error;

	if (vfsp->vfs_flag & VFS_DMI)
		seq_puts(m, "," MNTOPT_DMAPI);

	PVFS_SHOWARGS(BHV_NEXT(bhv), m, error);
	return error;
}

STATIC int
xfs_dm_mount(
	struct bhv_desc		*bhv,
	struct xfs_mount_args	*args,
	struct cred		*cr)
{
	struct bhv_desc		*rootbdp;
	struct vnode		*rootvp;
	struct vfs		*vfsp;
	int			error = 0;

	PVFS_MOUNT(BHV_NEXT(bhv), args, cr, error);
	if (error)
		return error;

	if (args->flags & XFSMNT_DMAPI) {
		vfsp = bhvtovfs(bhv);
		VFS_ROOT(vfsp, &rootvp, error);
		if (!error) {
			vfsp->vfs_flag |= VFS_DMI;
			rootbdp = vn_bhv_lookup_unlocked(
					VN_BHV_HEAD(rootvp), &xfs_vnodeops);
			VN_RELE(rootvp);
			error = dm_send_mount_event(vfsp, DM_RIGHT_NULL, NULL,
					DM_RIGHT_NULL, rootbdp, DM_RIGHT_NULL,
					args->mtpt, args->fsname);
		}
	}

	return error;
}


vfsops_t xfs_dmops_xfs = {
	BHV_IDENTITY_INIT(VFS_BHV_DM, VFS_POSITION_DM),
	.vfs_mount		= xfs_dm_mount,
	.vfs_parseargs		= xfs_dm_parseargs,
	.vfs_showargs		= xfs_dm_showargs,
	.vfs_dmapiops		= xfs_dm_get_fsys_vector,
};
