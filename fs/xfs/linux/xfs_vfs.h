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
#ifndef __XFS_VFS_H__
#define __XFS_VFS_H__

#include <linux/vfs.h>

struct statfs;
struct vnode;
struct cred;
struct super_block;
struct fid;
struct dm_fcntl_vector;
struct xfs_mount_args;

typedef struct vfs {
	u_int		vfs_flag;	/* flags */
	fsid_t		vfs_fsid;	/* file system id */
	fsid_t		*vfs_altfsid;	/* An ID fixed for life of FS */
	bhv_head_t	vfs_bh;		/* head of vfs behavior chain */
	struct super_block *vfs_super;	/* pointer to super block structure */
} vfs_t;

#define vfs_fbhv	vfs_bh.bh_first		/* 1st on vfs behavior chain */
#define VFS_FOPS(vfsp)	\
	((vfsops_t *)((vfsp)->vfs_fbhv->bd_ops))/* ops for 1st behavior */


#define bhvtovfs(bdp)	((struct vfs *)BHV_VOBJ(bdp))
#define VFS_BHVHEAD(vfsp) (&(vfsp)->vfs_bh)


#define VFS_RDONLY	0x0001		/* read-only vfs */
#define VFS_GRPID	0x0002		/* group-ID assigned from directory */
#define VFS_DMI		0x0004		/* filesystem has the DMI enabled */

#define SYNC_ATTR	0x0001		/* sync attributes */
#define SYNC_CLOSE	0x0002		/* close file system down */
#define SYNC_DELWRI	0x0004		/* look at delayed writes */
#define SYNC_WAIT	0x0008		/* wait for i/o to complete */
#define SYNC_FSDATA	0x0020		/* flush fs data (e.g. superblocks) */
#define SYNC_BDFLUSH	0x0010		/* BDFLUSH is calling -- don't block */


typedef struct vfsops {
	int	(*vfs_mount)(struct vfs *, struct xfs_mount_args *,
					struct cred *);
					/* mount file system */
	int	(*vfs_unmount)(bhv_desc_t *, int, struct cred *);
					/* unmount file system */
	int	(*vfs_root)(bhv_desc_t *, struct vnode **);
					/* get root vnode */
	int	(*vfs_statvfs)(bhv_desc_t *, struct statfs *, struct vnode *);
					/* get file system statistics */
	int	(*vfs_sync)(bhv_desc_t *, int, struct cred *);
					/* flush files */
	int	(*vfs_vget)(bhv_desc_t *, struct vnode **, struct fid *);
					/* get vnode from fid */
	int	(*vfs_dmapi_mount)(struct vfs *, char *, char *);
					/* send dmapi mount event */
	int	(*vfs_dmapi_fsys_vector)(bhv_desc_t *,
					 struct dm_fcntl_vector *);
	void	(*vfs_init_vnode)(bhv_desc_t *, struct vnode *,
					bhv_desc_t *, int);
	void	(*vfs_force_shutdown)(bhv_desc_t *,
					int, char *, int);
} vfsops_t;

#define VFS_UNMOUNT(vfsp,f,cr, rv)	\
{	\
	rv = (*(VFS_FOPS(vfsp)->vfs_unmount))((vfsp)->vfs_fbhv, f, cr);	\
}
#define VFS_ROOT(vfsp, vpp, rv)		\
{	\
	rv = (*(VFS_FOPS(vfsp)->vfs_root))((vfsp)->vfs_fbhv, vpp);	\
}
#define VFS_STATVFS(vfsp, sp, vp, rv)	\
{	\
	rv = (*(VFS_FOPS(vfsp)->vfs_statvfs))((vfsp)->vfs_fbhv, sp, vp);\
}
#define VFS_SYNC(vfsp, flag, cr, rv) \
{	\
	rv = (*(VFS_FOPS(vfsp)->vfs_sync))((vfsp)->vfs_fbhv, flag, cr); \
}
#define VFS_VGET(vfsp, vpp, fidp, rv) \
{	\
	rv = (*(VFS_FOPS(vfsp)->vfs_vget))((vfsp)->vfs_fbhv, vpp, fidp);  \
}

#define VFS_INIT_VNODE(vfsp, vp, bhv, unlock) \
{	\
	(*(VFS_FOPS(vfsp)->vfs_init_vnode))((vfsp)->vfs_fbhv, vp, bhv, unlock);\
}

/* No behavior lock here */
#define VFS_FORCE_SHUTDOWN(vfsp, flags) \
	(*(VFS_FOPS(vfsp)->vfs_force_shutdown))((vfsp)->vfs_fbhv, flags, __FILE__, __LINE__);

#define VFS_DMAPI_FSYS_VECTOR(vfsp, df, rv) \
{	\
	rv = (*(VFS_FOPS(vfsp)->vfs_dmapi_fsys_vector))((vfsp)->vfs_fbhv, df);	      \
}


#define VFSOPS_DMAPI_MOUNT(vfs_op, vfsp, dir_name, fsname, rv) \
	rv = (*(vfs_op)->vfs_dmapi_mount)(vfsp, dir_name, fsname)
#define VFSOPS_MOUNT(vfs_op, vfsp, args, cr, rv) \
	rv = (*(vfs_op)->vfs_mount)(vfsp, args, cr)

#define VFS_REMOVEBHV(vfsp, bdp)\
{	\
	bhv_remove(VFS_BHVHEAD(vfsp), bdp); \
}

#define PVFS_UNMOUNT(bdp,f,cr, rv)	\
{	\
	rv = (*((vfsops_t *)(bdp)->bd_ops)->vfs_unmount)(bdp, f, cr);	\
}

#define PVFS_SYNC(bdp, flag, cr, rv) \
{	\
	rv = (*((vfsops_t *)(bdp)->bd_ops)->vfs_sync)(bdp, flag, cr);	\
}


static __inline vfs_t *
vfs_allocate(void)
{
	vfs_t	*vfsp;

	vfsp = kmalloc(sizeof(vfs_t), GFP_KERNEL);
	if (vfsp) {
		memset(vfsp, 0, sizeof(vfs_t));
		bhv_head_init(VFS_BHVHEAD(vfsp), "vfs");
	}
	return (vfsp);
}

static __inline void
vfs_deallocate(
	vfs_t		*vfsp)
{
	bhv_head_destroy(VFS_BHVHEAD(vfsp));
	kfree(vfsp);
}

/*
 * Called by fs dependent VFS_MOUNT code to link the VFS base file system
 * dependent behavior with the VFS virtual object.
 */
static __inline void
vfs_insertbhv(
	vfs_t		*vfsp,
	bhv_desc_t	*bdp,
	vfsops_t	*vfsops,
	void		*mount)
{
	/*
	 * Initialize behavior desc with ops and data and then
	 * attach it to the vfs.
	 */
	bhv_desc_init(bdp, mount, vfsp, vfsops);
	bhv_insert_initial(&vfsp->vfs_bh, bdp);
}

#endif	/* __XFS_VFS_H__ */
