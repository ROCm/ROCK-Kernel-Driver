/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_bmap.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_inode_item.h"
#include <dmapi.h>
#include <dmapi_kern.h>
#include "xfs_dm.h"

/*
 * DMAPI behavior module routines
 */

STATIC int
xfs_dm_mount(
	struct bhv_desc		*bhv,
	struct xfs_mount_args	*args,
	struct cred		*cr)
{
	struct xfs_inode	*rootip;
	struct vnode		*rootvp;
	struct vfs		*vfsp = bhvtovfs(bhv);
	int			error = 0;

	/* Returns positive errors to XFS */

	PVFS_MOUNT(BHV_NEXT(bhv), args, cr, error);
	if (error)
		return error;

	if (args->flags & XFSMNT_DMAPI) {
		VFS_ROOT(vfsp, &rootvp, error);
		if (!error) {
			rootip = xfs_vtoi(rootvp);
			VN_RELE(rootvp);
			if (rootip != NULL) {
			    vfsp->vfs_flag |= VFS_DMI;
			    error = dm_send_mount_event(vfsp->vfs_super,
					DM_RIGHT_NULL, NULL,
					DM_RIGHT_NULL, LINVFS_GET_IP(rootvp),
					DM_RIGHT_NULL,
					args->mtpt, args->fsname);
			    error = -error; /* DMAPI returns negative errs */
			}
		}
	}

	return error;
}

#define MNTOPT_DMAPI	"dmapi"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_XDSM	"xdsm"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_DMI	"dmi"		/* DMI enabled (DMAPI / XDSM) */

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

	/* Returns positive errors to XFS */

	while ((this_char = strsep(&local_options, ",")) != NULL) {
		length = strlen(this_char);
		if (local_options)
			length++;

		if (!strcmp(this_char, MNTOPT_DMAPI)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_XDSM)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_DMI)) {
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
	if (!error && (args->flags & XFSMNT_DMAPI) && (*args->mtpt == '\0')) {
		printk("XFS: %s option needs the mount point option as well\n",
			MNTOPT_DMAPI);
		error = EINVAL;
	}
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

	/* Returns positive errors to XFS */

	if (vfsp->vfs_flag & VFS_DMI)
		seq_puts(m, "," MNTOPT_DMAPI);

	PVFS_SHOWARGS(BHV_NEXT(bhv), m, error);
	return error;
}

struct bhv_vfsops xfs_dmops = { {
	BHV_IDENTITY_INIT(VFS_BHV_DM, VFS_POSITION_DM),
	.vfs_mount		= xfs_dm_mount,
	.vfs_parseargs		= xfs_dm_parseargs,
	.vfs_showargs		= xfs_dm_showargs,
	.vfs_dmapiops		= xfs_dm_get_fsys_vector, },
};

static int __init
xfs_dm_init(void)
{
	static char	message[] __initdata =
		KERN_INFO "SGI XFS Data Management API subsystem\n";

	printk(message);
	vfs_bhv_set_custom(&xfs_dmops, &xfs_dmcore_xfs);
	bhv_module_init(XFS_DMOPS, THIS_MODULE, &xfs_dmops);
	dmapi_register(&xfs_fs_type, &xfs_dmapiops);
	return 0;
}

static void __exit
xfs_dm_exit(void)
{
	dmapi_unregister(&xfs_fs_type);
	bhv_module_exit(XFS_DMOPS);
	vfs_bhv_clr_custom(&xfs_dmops);
}

module_init(xfs_dm_init);
module_exit(xfs_dm_exit);
