/*
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Woodhouse <dwmw2@cambridge.redhat.com>
 *          David Howells <dhowells@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "volume.h"
#include "vnode.h"
#include "super.h"
#include "internal.h"

struct afs_iget_data {
	afs_fid_t		fid;
	afs_volume_t		*volume;	/* volume on which resides */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	afs_vnode_t		*new_vnode;	/* new vnode record */
#endif
};

/*****************************************************************************/
/*
 * map the AFS file status to the inode member variables
 */
static int afs_inode_map_status(afs_vnode_t *vnode)
{
	struct inode *inode = AFS_VNODE_TO_I(vnode);

	_debug("FS: ft=%d lk=%d sz=%u ver=%Lu mod=%hu",
	       vnode->status.type,
	       vnode->status.nlink,
	       vnode->status.size,
	       vnode->status.version,
	       vnode->status.mode);

	switch (vnode->status.type) {
	case AFS_FTYPE_FILE:
		inode->i_mode	= S_IFREG | vnode->status.mode;
		inode->i_op	= &afs_file_inode_operations;
		inode->i_fop	= &afs_file_file_operations;
		break;
	case AFS_FTYPE_DIR:
		inode->i_mode	= S_IFDIR | vnode->status.mode;
		inode->i_op	= &afs_dir_inode_operations;
		inode->i_fop	= &afs_dir_file_operations;
		break;
	case AFS_FTYPE_SYMLINK:
		inode->i_mode	= S_IFLNK | vnode->status.mode;
		inode->i_op	= &page_symlink_inode_operations;
		break;
	default:
		printk("kAFS: AFS vnode with undefined type\n");
		return -EBADMSG;
	}

	inode->i_nlink		= vnode->status.nlink;
	inode->i_uid		= vnode->status.owner;
	inode->i_gid		= 0;
	inode->i_rdev		= NODEV;
	inode->i_size		= vnode->status.size;
	inode->i_atime		= inode->i_mtime = inode->i_ctime = vnode->status.mtime_server;
	inode->i_blksize	= PAGE_CACHE_SIZE;
	inode->i_blocks		= 0;
	inode->i_version	= vnode->fid.unique;
	inode->i_mapping->a_ops	= &afs_fs_aops;

	/* check to see whether a symbolic link is really a mountpoint */
	if (vnode->status.type==AFS_FTYPE_SYMLINK) {
		afs_mntpt_check_symlink(vnode);

		if (vnode->flags & AFS_VNODE_MOUNTPOINT) {
			inode->i_mode	= S_IFDIR | vnode->status.mode;
			inode->i_op	= &afs_mntpt_inode_operations;
			inode->i_fop	= &afs_mntpt_file_operations;
		}
	}

	return 0;
} /* end afs_inode_map_status() */

/*****************************************************************************/
/*
 * attempt to fetch the status of an inode, coelescing multiple simultaneous fetches
 */
int afs_inode_fetch_status(struct inode *inode)
{
	afs_vnode_t *vnode;
	int ret;

	vnode = AFS_FS_I(inode);

	ret = afs_vnode_fetch_status(vnode);

	if (ret==0)
		ret = afs_inode_map_status(vnode);

	return ret;

} /* end afs_inode_fetch_status() */

/*****************************************************************************/
/*
 * iget5() comparator
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
static int afs_iget5_test(struct inode *inode, void *opaque)
{
	struct afs_iget_data *data = opaque;

	/* only match inodes with the same version number */
	return inode->i_ino==data->fid.vnode && inode->i_version==data->fid.unique;
} /* end afs_iget5_test() */
#endif

/*****************************************************************************/
/*
 * iget5() inode initialiser
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
static int afs_iget5_set(struct inode *inode, void *opaque)
{
	struct afs_iget_data *data = opaque;
	afs_vnode_t *vnode = AFS_FS_I(inode);

	inode->i_ino = data->fid.vnode;
	inode->i_version = data->fid.unique;
	vnode->fid = data->fid;
	vnode->volume = data->volume;

	return 0;
} /* end afs_iget5_set() */
#endif

/*****************************************************************************/
/*
 * iget4() comparator
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int afs_iget4_test(struct inode *inode, ino_t ino, void *opaque)
{
	struct afs_iget_data *data = opaque;

	/* only match inodes with the same version number */
	return inode->i_ino==data->fid.vnode && inode->i_version==data->fid.unique;
} /* end afs_iget4_test() */
#endif

/*****************************************************************************/
/*
 * read an inode (2.4 only)
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
void afs_read_inode2(struct inode *inode, void *opaque)
{
	struct afs_iget_data *data = opaque;
	afs_vnode_t *vnode;
	int ret;

	_enter(",{{%u,%u,%u},%p}",data->fid.vid,data->fid.vnode,data->fid.unique,data->volume);

	if (inode->u.generic_ip) BUG();

	/* attach a pre-allocated vnode record */
	inode->u.generic_ip = vnode = data->new_vnode;
	data->new_vnode = NULL;

	memset(vnode,0,sizeof(*vnode));
	vnode->inode = inode;
	init_waitqueue_head(&vnode->update_waitq);
	spin_lock_init(&vnode->lock);
	INIT_LIST_HEAD(&vnode->cb_link);
	INIT_LIST_HEAD(&vnode->cb_hash_link);
	afs_timer_init(&vnode->cb_timeout,&afs_vnode_cb_timed_out_ops);
	vnode->flags |= AFS_VNODE_CHANGED;
	vnode->volume = data->volume;
	vnode->fid = data->fid;

	/* ask the server for a status check */
	ret = afs_vnode_fetch_status(vnode);
	if (ret<0) {
		make_bad_inode(inode);
		_leave(" [bad inode]");
		return;
	}

	ret = afs_inode_map_status(vnode);
	if (ret<0) {
		make_bad_inode(inode);
		_leave(" [bad inode]");
		return;
	}

	_leave("");
	return;
} /* end afs_read_inode2() */
#endif

/*****************************************************************************/
/*
 * inode retrieval
 */
inline int afs_iget(struct super_block *sb, afs_fid_t *fid, struct inode **_inode)
{
	struct afs_iget_data data = { fid: *fid };
	struct afs_super_info *as;
	struct inode *inode;
	afs_vnode_t *vnode;
	int ret;

	_enter(",{%u,%u,%u},,",fid->vid,fid->vnode,fid->unique);

	as = sb->s_fs_info;
	data.volume = as->volume;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
	inode = iget5_locked(sb,fid->vnode,afs_iget5_test,afs_iget5_set,&data);
	if (!inode) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	vnode = AFS_FS_I(inode);

	/* deal with an existing inode */
	if (!(inode->i_state & I_NEW)) {
		ret = afs_vnode_fetch_status(vnode);
		if (ret==0)
			*_inode = inode;
		else
			iput(inode);
		_leave(" = %d",ret);
		return ret;
	}

	/* okay... it's a new inode */
	vnode->flags |= AFS_VNODE_CHANGED;
	ret = afs_inode_fetch_status(inode);
	if (ret<0)
		goto bad_inode;

#if 0
	/* find a cache entry for it */
	ret = afs_cache_lookup_vnode(as->volume,vnode);
	if (ret<0)
		goto bad_inode;
#endif

	/* success */
	unlock_new_inode(inode);

	*_inode = inode;
	_leave(" = 0 [CB { v=%u x=%lu t=%u nix=%u }]",
	       vnode->cb_version,
	       vnode->cb_timeout.timo_jif,
	       vnode->cb_type,
	       vnode->nix
	       );
	return 0;

	/* failure */
 bad_inode:
	make_bad_inode(inode);
	unlock_new_inode(inode);
	iput(inode);

	_leave(" = %d [bad]",ret);
	return ret;

#else

	/* pre-allocate a vnode record so that afs_read_inode2() doesn't have to return an inode
	 * without one attached
	 */
	data.new_vnode = kmalloc(sizeof(afs_vnode_t),GFP_KERNEL);
	if (!data.new_vnode) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	inode = iget4(sb,fid->vnode,afs_iget4_test,&data);
	if (data.new_vnode) kfree(data.new_vnode); 
	if (!inode) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	vnode = AFS_FS_I(inode);
	*_inode = inode;
	_leave(" = 0 [CB { v=%u x=%lu t=%u nix=%u }]",
	       vnode->cb_version,
	       vnode->cb_timeout.timo_jif,
	       vnode->cb_type,
	       vnode->nix
	       );
	return 0;
#endif
} /* end afs_iget() */

/*****************************************************************************/
/*
 * read the attributes of an inode
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
int afs_inode_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode;
	afs_vnode_t *vnode;
	int ret;

	inode = dentry->d_inode;

	_enter("{ ino=%lu v=%lu }",inode->i_ino,inode->i_version);

	vnode = AFS_FS_I(inode);

	ret = afs_inode_fetch_status(inode);
	if (ret==-ENOENT) {
		_leave(" = %d [%d %p]",ret,atomic_read(&dentry->d_count),dentry->d_inode);
		return ret;
	}
	else if (ret<0) {
		make_bad_inode(inode);
		_leave(" = %d",ret);
		return ret;
	}

	/* transfer attributes from the inode structure to the stat structure */
	generic_fillattr(inode,stat);

	_leave(" = 0 CB { v=%u x=%u t=%u }",
	       vnode->cb_version,
	       vnode->cb_expiry,
	       vnode->cb_type);

	return 0;
} /* end afs_inode_getattr() */
#endif

/*****************************************************************************/
/*
 * revalidate the inode
 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)
int afs_inode_revalidate(struct dentry *dentry)
{
	struct inode *inode;
	afs_vnode_t *vnode;
	int ret;

	inode = dentry->d_inode;

	_enter("{ ino=%lu v=%lu }",inode->i_ino,inode->i_version);

	vnode = AFS_FS_I(inode);

	ret = afs_inode_fetch_status(inode);
	if (ret==-ENOENT) {
		_leave(" = %d [%d %p]",ret,atomic_read(&dentry->d_count),dentry->d_inode);
		return ret;
	}
	else if (ret<0) {
		make_bad_inode(inode);
		_leave(" = %d",ret);
		return ret;
	}

	_leave(" = 0 CB { v=%u x=%u t=%u }",
	       vnode->cb_version,
	       vnode->cb_expiry,
	       vnode->cb_type);

	return 0;
} /* end afs_inode_revalidate() */
#endif

/*****************************************************************************/
/*
 * clear an AFS inode
 */
void afs_clear_inode(struct inode *inode)
{
	afs_vnode_t *vnode;

	vnode = AFS_FS_I(inode);

	_enter("ino=%lu { vn=%08x v=%u x=%u t=%u }",
	       inode->i_ino,
	       vnode->fid.vnode,
	       vnode->cb_version,
	       vnode->cb_expiry,
	       vnode->cb_type
	       );

	if (inode->i_ino!=vnode->fid.vnode) BUG();

	afs_vnode_give_up_callback(vnode);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)
	if (inode->u.generic_ip) kfree(inode->u.generic_ip);
#endif

	_leave("");
} /* end afs_clear_inode() */
