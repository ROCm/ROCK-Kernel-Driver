/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_imap.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_quota.h"

int
vfs_mount(
	struct bhv_desc		*bdp,
	struct xfs_mount_args	*args,
	struct cred		*cr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_mount)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_mount)(next, args, cr));
}

int
vfs_parseargs(
	struct bhv_desc		*bdp,
	char			*s,
	struct xfs_mount_args	*args,
	int			f)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_parseargs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_parseargs)(next, s, args, f));
}

int
vfs_showargs(
	struct bhv_desc		*bdp,
	struct seq_file		*m)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_showargs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_showargs)(next, m));
}

int
vfs_unmount(
	struct bhv_desc		*bdp,
	int			fl,
	struct cred		*cr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_unmount)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_unmount)(next, fl, cr));
}

int
vfs_mntupdate(
	struct bhv_desc		*bdp,
	int			*fl,
	struct xfs_mount_args	*args)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_mntupdate)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_mntupdate)(next, fl, args));
}

int
vfs_root(
	struct bhv_desc		*bdp,
	struct vnode		**vpp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_root)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_root)(next, vpp));
}

int
vfs_statvfs(
	struct bhv_desc		*bdp,
	xfs_statfs_t		*sp,
	struct vnode		*vp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_statvfs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_statvfs)(next, sp, vp));
}

int
vfs_sync(
	struct bhv_desc		*bdp,
	int			fl,
	struct cred		*cr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_sync)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_sync)(next, fl, cr));
}

int
vfs_vget(
	struct bhv_desc		*bdp,
	struct vnode		**vpp,
	struct fid		*fidp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_vget)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_vget)(next, vpp, fidp));
}

int
vfs_dmapiops(
	struct bhv_desc		*bdp,
	caddr_t			addr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_dmapiops)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_dmapiops)(next, addr));
}

int
vfs_quotactl(
	struct bhv_desc		*bdp,
	int			cmd,
	int			id,
	caddr_t			addr)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_quotactl)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_quotactl)(next, cmd, id, addr));
}

struct inode *
vfs_get_inode(
	struct bhv_desc		*bdp,
	xfs_ino_t		ino,
	int			fl)
{
	struct bhv_desc		*next = bdp;

	while (! (bhvtovfsops(next))->vfs_get_inode)
		next = BHV_NEXTNULL(next);
	return ((*bhvtovfsops(next)->vfs_get_inode)(next, ino, fl));
}

void
vfs_init_vnode(
	struct bhv_desc		*bdp,
	struct vnode		*vp,
	struct bhv_desc		*bp,
	int			unlock)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_init_vnode)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_init_vnode)(next, vp, bp, unlock));
}

void
vfs_force_shutdown(
	struct bhv_desc		*bdp,
	int			fl,
	char			*file,
	int			line)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_force_shutdown)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_force_shutdown)(next, fl, file, line));
}

void
vfs_freeze(
	struct bhv_desc		*bdp)
{
	struct bhv_desc		*next = bdp;

	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_freeze)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_freeze)(next));
}

vfs_t *
vfs_allocate( void )
{
	struct vfs		*vfsp;

	vfsp = kmem_zalloc(sizeof(vfs_t), KM_SLEEP);
	bhv_head_init(VFS_BHVHEAD(vfsp), "vfs");
	INIT_LIST_HEAD(&vfsp->vfs_sync_list);
	spin_lock_init(&vfsp->vfs_sync_lock);
	init_waitqueue_head(&vfsp->vfs_wait_single_sync_task);
	return vfsp;
}

void
vfs_deallocate(
	struct vfs		*vfsp)
{
	bhv_head_destroy(VFS_BHVHEAD(vfsp));
	kmem_free(vfsp, sizeof(vfs_t));
}

void
vfs_insertops(
	struct vfs		*vfsp,
	struct bhv_vfsops	*vfsops)
{
	struct bhv_desc		*bdp;

	bdp = kmem_alloc(sizeof(struct bhv_desc), KM_SLEEP);
	bhv_desc_init(bdp, NULL, vfsp, vfsops);
	bhv_insert(&vfsp->vfs_bh, bdp);
}

void
vfs_insertbhv(
	struct vfs		*vfsp,
	struct bhv_desc		*bdp,
	struct vfsops		*vfsops,
	void			*mount)
{
	bhv_desc_init(bdp, mount, vfsp, vfsops);
	bhv_insert_initial(&vfsp->vfs_bh, bdp);
}

/*
 * Implementation for behaviours-as-modules
 */

typedef struct bhv_module_list {
	struct list_head	bm_list;
	struct module *		bm_module;
	const char *		bm_name;
	void *			bm_ops;
} bhv_module_list_t;
STATIC DEFINE_SPINLOCK(bhv_lock);
STATIC struct list_head bhv_list = LIST_HEAD_INIT(bhv_list);

void
bhv_module_init(
	const char		*name,
	struct module		*module,
	const void		*ops)
{
	bhv_module_list_t	*bm, *entry, *n;

	bm = kmem_alloc(sizeof(struct bhv_module_list), KM_SLEEP);
	INIT_LIST_HEAD(&bm->bm_list);
	bm->bm_module = module;
	bm->bm_name = name;
	bm->bm_ops = (void *)ops;

	spin_lock(&bhv_lock);
	list_for_each_entry_safe(entry, n, &bhv_list, bm_list)
		BUG_ON(strcmp(entry->bm_name, name) == 0);
	list_add(&bm->bm_list, &bhv_list);
	spin_unlock(&bhv_lock);
}

void
bhv_module_exit(
	const char		*name)
{
	bhv_module_list_t	*entry, *n;

	spin_lock(&bhv_lock);
	list_for_each_entry_safe(entry, n, &bhv_list, bm_list)
		if (strcmp(entry->bm_name, name) == 0)
			list_del(&entry->bm_list);
	spin_unlock(&bhv_lock);
}

STATIC void *
bhv_insert_module(
	const char		*name,
	const char		*modname)
{
	bhv_module_list_t	*entry, *n;
	void			*ops = NULL;

	spin_lock(&bhv_lock);
	list_for_each_entry_safe(entry, n, &bhv_list, bm_list)
		if (strcmp(entry->bm_name, name) == 0 &&
		    try_module_get(entry->bm_module))
			ops = entry->bm_ops;
	spin_unlock(&bhv_lock);
	return ops;
}

STATIC void
bhv_remove_module(
	const char		*name)
{
	bhv_module_list_t	*entry, *n;

	spin_lock(&bhv_lock);
	list_for_each_entry_safe(entry, n, &bhv_list, bm_list)
		if (strcmp(entry->bm_name, name) == 0)
		    module_put(entry->bm_module);
	spin_unlock(&bhv_lock);
}

STATIC void *
bhv_lookup_module(
	const char		*name,
	const char		*module)
{
	void			*ops;

	ops = bhv_insert_module(name, module);
	if (!ops && module) {
		request_module("%s", module);
		ops = bhv_insert_module(name, module);
	}
	return ops;
}

void
bhv_remove_vfsops(
	struct vfs		*vfsp,
	int			pos)
{
	struct bhv_desc		*bhv;

	bhv = bhv_lookup_range(&vfsp->vfs_bh, pos, pos);
	if (bhv) {
		struct bhv_module	*bm;

		bm = (bhv_module_t *) BHV_PDATA(bhv);
		bhv_remove(&vfsp->vfs_bh, bhv);
		bhv_remove_module(bm->bm_name);
		kmem_free(bhv, sizeof(*bhv));
	}
}

void
bhv_remove_all_vfsops(
	struct vfs		*vfsp,
	int			freebase)
{
	struct xfs_mount	*mp;

	bhv_remove_vfsops(vfsp, VFS_POSITION_QM);
	bhv_remove_vfsops(vfsp, VFS_POSITION_DM);
	bhv_remove_vfsops(vfsp, VFS_POSITION_IO);
	if (!freebase)
		return;
	mp = XFS_BHVTOM(bhv_lookup(VFS_BHVHEAD(vfsp), &xfs_vfsops));
	VFS_REMOVEBHV(vfsp, &mp->m_bhv);
	xfs_mount_free(mp, 0);
}

void
bhv_get_vfsops(
	struct vfs		*vfsp,
	const char		*name,
	const char		*module)
{
	struct bhv_vfsops	*ops;

	ops = (struct bhv_vfsops *) bhv_lookup_module(name, module);
	if (ops) {
		struct bhv_module	*bm;

		bm = kmem_alloc(sizeof(struct bhv_module), KM_SLEEP);
		bm->bm_name = name;
		bhv_desc_init(&bm->bm_desc, bm, vfsp, ops);
		bhv_insert(&vfsp->vfs_bh, &bm->bm_desc);
	}
}

void
bhv_insert_all_vfsops(
	struct vfs		*vfsp)
{
	struct xfs_mount	*mp;

	mp = xfs_mount_init();
	vfs_insertbhv(vfsp, &mp->m_bhv, &xfs_vfsops, mp);
	bhv_get_vfsops(vfsp, XFS_DMOPS,
		xfs_probe_dmapi ? XFS_DM_MODULE : NULL);
	bhv_get_vfsops(vfsp, XFS_QMOPS,
		xfs_probe_quota ? XFS_QM_MODULE : NULL);
	bhv_get_vfsops(vfsp, XFS_IOOPS,
		xfs_probe_ioops ? XFS_IO_MODULE : NULL);
}
