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


STATIC struct file_operations *
xfs_dm_get_invis_ops(
	struct inode *ip)
{
	return &linvfs_invis_file_operations;
}

STATIC int
xfs_dm_fh_to_inode(
	struct super_block	*sb,
	struct inode		**ip,
	dm_fid_t		*dmfid)
{
	vnode_t	*vp = NULL;
	vfs_t	*vfsp = LINVFS_GET_VFS(sb);
	int	error;
	fid_t	fid;

	/* Returns negative errors to DMAPI */

	*ip = NULL;
	memcpy(&fid, dmfid, sizeof(*dmfid));
	if (fid.fid_len) {	/* file object handle */
		VFS_VGET(vfsp, &vp, &fid, error);
	}
	else {			/* filesystem handle */
		VFS_ROOT(vfsp, &vp, error);
	}
	if(vp && (error == 0))
		*ip = LINVFS_GET_IP(vp);
	return -error; /* Return negative error to DMAPI */
}

STATIC int
xfs_dm_inode_to_fh(
	struct inode		*ip,
	dm_fid_t		*dmfid,
	dm_fsid_t		*dmfsid)
{
	vnode_t	*vp = LINVFS_GET_VP(ip);
	int	error;
	fid_t	fid;

	/* Returns negative errors to DMAPI */

	if (vp->v_vfsp->vfs_altfsid == NULL)
		return -EINVAL;
	VOP_FID2(vp, &fid, error);
	if (error)
		return -error; /* Return negative error to DMAPI */

	memcpy(dmfid, &fid, sizeof(*dmfid));
	memcpy(dmfsid, vp->v_vfsp->vfs_altfsid, sizeof(*dmfsid));
	return 0;
}

STATIC int
xfs_dm_get_dmapiops(
	struct super_block	*sb,
	void			*addr)
{
	vfs_t	*vfsp = LINVFS_GET_VFS(sb);
	int error;

	/* Returns negative errors to DMAPI */

	VFS_DMAPIOPS(vfsp, (caddr_t)addr, error);
	return -error; /* Return negative error to DMAPI */
}

STATIC void
xfs_dm_get_fsid(
	struct super_block	*sb,
	dm_fsid_t		*fsid)
{
	vfs_t	*vfsp = LINVFS_GET_VFS(sb);
	memcpy(fsid, vfsp->vfs_altfsid, sizeof(*fsid));
}

/*
 * Filesystem operations accessed by the DMAPI core.
 */
struct filesystem_dmapi_operations xfs_dmapiops = {
	.get_fsys_vector	= xfs_dm_get_dmapiops,
	.fh_to_inode		= xfs_dm_fh_to_inode,
	.get_invis_ops		= xfs_dm_get_invis_ops,
	.inode_to_fh		= xfs_dm_inode_to_fh,
	.get_fsid		= xfs_dm_get_fsid,
};
