/*
 * driverfs.c - The device driver file system
 *
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is a simple, ram-based filesystem, which allows kernel
 * callbacks for read/write of files.
 *
 * Please see Documentation/filesystems/driverfs.txt for more information.
 */

#include <linux/list.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <asm/uaccess.h>

#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

/* Random magic number */
#define DRIVERFS_MAGIC 0x42454552

static struct super_operations driverfs_ops;
static struct address_space_operations driverfs_aops;
static struct file_operations driverfs_dir_operations;
static struct file_operations driverfs_file_operations;
static struct inode_operations driverfs_dir_inode_operations;
static struct dentry_operations driverfs_dentry_dir_ops;
static struct dentry_operations driverfs_dentry_file_ops;


static struct vfsmount *driverfs_mount;
static spinlock_t mount_lock = SPIN_LOCK_UNLOCKED;
static int mount_count = 0;

static int driverfs_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = DRIVERFS_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = 255;
	return 0;
}

static struct dentry *driverfs_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

struct inode *driverfs_get_inode(struct super_block *sb, int mode, int dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_mapping->a_ops = &driverfs_aops;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_fop = &driverfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &driverfs_dir_inode_operations;
			inode->i_fop = &driverfs_dir_operations;
			break;
		}
	}
	return inode;
}

static int driverfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int dev)
{
	struct inode *inode = driverfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	return error;
}

static int driverfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	dentry->d_op = &driverfs_dentry_dir_ops;
	return driverfs_mknod(dir, dentry, mode | S_IFDIR, 0);
}

static int driverfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	dentry->d_op = &driverfs_dentry_file_ops;
 	return driverfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int
driverfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	if(S_ISDIR(inode->i_mode))
		return -EPERM;

	inode->i_nlink++;
	atomic_inc(&inode->i_count);
 	dget(dentry);
	d_instantiate(dentry, inode);
	return 0;
}

static inline int driverfs_positive(struct dentry *dentry)
{
	return (dentry->d_inode && !d_unhashed(dentry));
}

static int driverfs_empty(struct dentry *dentry)
{
	struct list_head *list;

	spin_lock(&dcache_lock);

	list_for_each(list, &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);
		if (driverfs_positive(de)) {
			spin_unlock(&dcache_lock);
			return 0;
		}
	}

	spin_unlock(&dcache_lock);
	return 1;
}

static int driverfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error = -ENOTEMPTY;

	if (driverfs_empty(dentry)) {
		struct inode *inode = dentry->d_inode;

		inode->i_nlink--;
		dput(dentry);
		error = 0;
	}
	return error;
}

static int
driverfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		 struct inode *new_dir, struct dentry *new_dentry)
{
	int error = -ENOTEMPTY;

	if (driverfs_empty(new_dentry)) {
		struct inode *inode = new_dentry->d_inode;
		if (inode) {
			inode->i_nlink--;
			dput(new_dentry);
		}
		error = 0;
	}
	return error;
}

static int driverfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int error;

	error = driverfs_mknod(dir, dentry, S_IFLNK | S_IRWXUGO, 0);
	if (!error) {
		int l = strlen(symname) + 1;
		struct inode *inode = dentry->d_inode;
		error = block_symlink(inode,symname,l);
	}
	return error;
}

#define driverfs_rmdir driverfs_unlink

/**
 * driverfs_read_file - "read" data from a file.
 * @file:	file pointer
 * @buf:	buffer to fill
 * @count:	number of bytes to read
 * @ppos:	starting offset in file
 *
 * Userspace wants data from a file. It is up to the creator of the file to
 * provide that data.
 * There is a struct driver_file_entry embedded in file->private_data. We
 * obtain that and check if the read callback is implemented. If so, we call
 * it, passing the data field of the file entry.
 * Said callback is responsible for filling the buffer and returning the number
 * of bytes it put in it. We update @ppos correctly.
 */
static ssize_t
driverfs_read_file(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct driver_file_entry * entry;
	unsigned char *page;
	ssize_t retval = 0;
	struct device * dev;

	entry = (struct driver_file_entry *)file->private_data;
	if (!entry) {
		DBG("%s: file entry is NULL\n",__FUNCTION__);
		return -ENOENT;
	}

	dev = list_entry(entry->parent,struct device, dir);
	get_device(dev);

	if (!entry->show)
		goto done;

	page = (unsigned char*)__get_free_page(GFP_KERNEL);
	if (!page) {
		retval = -ENOMEM;
		goto done;
	}

	while (count > 0) {
		ssize_t len;

		len = entry->show(dev,page,count,*ppos);

		if (len <= 0) {
			if (len < 0)
				retval = len;
			break;
		}

		if (copy_to_user(buf,page,len)) {
			retval = -EFAULT;
			break;
		}

		*ppos += len;
		count -= len;
		buf += len;
		retval += len;
	}
	free_page((unsigned long)page);

 done:
	put_device(dev);
	return retval;
}

/**
 * driverfs_write_file - "write" to a file
 * @file:	file pointer
 * @buf:	data to write
 * @count:	number of bytes
 * @ppos:	starting offset
 *
 * Similarly to driverfs_read_file, we act essentially as a bit pipe.
 * We check for a "write" callback in file->private_data, and pass
 * @buffer, @count, @ppos, and the file entry's data to the callback.
 * The number of bytes written is returned, and we handle updating
 * @ppos properly.
 */
static ssize_t
driverfs_write_file(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct driver_file_entry * entry;
	struct device * dev;
	ssize_t retval = 0;

	entry = (struct driver_file_entry *)file->private_data;
	if (!entry) {
		DBG("%s: file entry is NULL\n",__FUNCTION__);
		return -ENOENT;
	}

	dev = list_entry(entry->parent,struct device, dir);
	get_device(dev);

	if (!entry->store)
		goto done;

	while (count > 0) {
		ssize_t len;

		len = entry->store(dev,buf,count,*ppos);

		if (len <= 0) {
			if (len < 0)
				retval = len;
			break;
		}
		retval += len;
		count -= len;
		*ppos += len;
		buf += len;
	}
 done:
	put_device(dev);
	return retval;
}

static loff_t
driverfs_file_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t retval = -EINVAL;

	switch(orig) {
	case 0:
		if (offset > 0) {
			file->f_pos = offset;
			retval = file->f_pos;
		}
		break;
	case 1:
		if ((offset + file->f_pos) > 0) {
			file->f_pos += offset;
			retval = file->f_pos;
		}
		break;
	default:
		break;
	}
	return retval;
}

static int driverfs_open_file(struct inode * inode, struct file * filp)
{
	if (filp && inode)
		filp->private_data = inode->u.generic_ip;

	return 0;
}

static int driverfs_sync_file (struct file *file, struct dentry *dentry, int datasync)
{
	return 0;
}

/* When the dentry goes away (the refcount hits 0), free the
 * driver_file_entry for it.
 */
static int driverfs_d_delete_file (struct dentry * dentry)
{
	struct driver_file_entry * entry;

	entry = (struct driver_file_entry *)dentry->d_fsdata;
	if (entry)
		kfree(entry);
	return 0;
}

static struct address_space_operations driverfs_aops = {

};

static struct file_operations driverfs_file_operations = {
	read:		driverfs_read_file,
	write:		driverfs_write_file,
	llseek:		driverfs_file_lseek,
	mmap:		generic_file_mmap,
	open:		driverfs_open_file,
	fsync:		driverfs_sync_file,
};

static struct file_operations driverfs_dir_operations = {
	read:		generic_read_dir,
	readdir:	dcache_readdir,
	fsync:		driverfs_sync_file,
};

static struct inode_operations driverfs_dir_inode_operations = {
	create:		driverfs_create,
	lookup:		driverfs_lookup,
	link:		driverfs_link,
	unlink:		driverfs_unlink,
	symlink:	driverfs_symlink,
	mkdir:		driverfs_mkdir,
	rmdir:		driverfs_rmdir,
	mknod:		driverfs_mknod,
	rename:		driverfs_rename,
};

static struct dentry_operations driverfs_dentry_file_ops = {
	d_delete:	driverfs_d_delete_file,
};

static struct super_operations driverfs_ops = {
	statfs:		driverfs_statfs,
	put_inode:	force_delete,
};

static struct super_block*
driverfs_read_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = DRIVERFS_MAGIC;
	sb->s_op = &driverfs_ops;
	inode = driverfs_get_inode(sb, S_IFDIR | 0755, 0);

	if (!inode) {
		DBG("%s: could not get inode!\n",__FUNCTION__);
		return NULL;
	}

	root = d_alloc_root(inode);
	if (!root) {
		DBG("%s: could not get root dentry!\n",__FUNCTION__);
		iput(inode);
		return NULL;
	}
	sb->s_root = root;
	return sb;
}

static DECLARE_FSTYPE(driverfs_fs_type, "driverfs", driverfs_read_super, FS_SINGLE | FS_LITTER);

static int get_mount(void)
{
	struct vfsmount * mnt;

	spin_lock(&mount_lock);
	if (driverfs_mount) {
		mntget(driverfs_mount);
		++mount_count;
		spin_unlock(&mount_lock);
		goto go_ahead;
	}

	spin_unlock(&mount_lock);
	mnt = kern_mount(&driverfs_fs_type);

	if (IS_ERR(mnt)) {
		printk(KERN_ERR "driverfs: could not mount!\n");
		return -ENODEV;
	}

	spin_lock(&mount_lock);
	if (!driverfs_mount) {
		driverfs_mount = mnt;
		++mount_count;
		spin_unlock(&mount_lock);
		goto go_ahead;
	}

	mntget(driverfs_mount);
	++mount_count;
	spin_unlock(&mount_lock);

 go_ahead:
	DBG("driverfs: mount_count = %d\n",mount_count);
	return 0;
}

static void put_mount(void)
{
	struct vfsmount * mnt;

	spin_lock(&mount_lock);
	mnt = driverfs_mount;
	--mount_count;
	if (!mount_count)
		driverfs_mount = NULL;
	spin_unlock(&mount_lock);
	mntput(mnt);
	DBG("driverfs: mount_count = %d\n",mount_count);
}

int __init init_driverfs_fs(void)
{
	int error;

	DBG("%s: registering filesystem.\n",__FUNCTION__);
	error = register_filesystem(&driverfs_fs_type);

	if (error)
		DBG(KERN_ERR "%s: register_filesystem failed with %d\n",
		       __FUNCTION__,error);

	return error;
}

static void __exit exit_driverfs_fs(void)
{
	unregister_filesystem(&driverfs_fs_type);
}
#if 0
module_init(init_driverfs_fs);
module_exit(exit_driverfs_fs);
#endif

EXPORT_SYMBOL(driverfs_create_file);
EXPORT_SYMBOL(driverfs_create_dir);
EXPORT_SYMBOL(driverfs_remove_file);
EXPORT_SYMBOL(driverfs_remove_dir);

MODULE_DESCRIPTION("The device driver filesystem");
MODULE_LICENSE("GPL");

/**
 * driverfs_create_dir - create a directory in the filesystem
 * @entry:	directory entry
 * @parent:	parent directory entry
 */
int
driverfs_create_dir(struct driver_dir_entry * entry,
		    struct driver_dir_entry * parent)
{
	struct dentry * dentry = NULL;
	struct dentry * parent_dentry;
	struct qstr qstr;
	int error = 0;

	if (!entry)
		return -EINVAL;

	get_mount();

	parent_dentry = parent ? parent->dentry : NULL;

	if (!parent_dentry)
		if (driverfs_mount && driverfs_mount->mnt_sb)
			parent_dentry = driverfs_mount->mnt_sb->s_root;

	if (!parent_dentry) {
		put_mount();
		return -EFAULT;
	}

	dget(parent_dentry);
	down(&parent_dentry->d_inode->i_sem);

	qstr.name = entry->name;
	qstr.len = strlen(entry->name);
	qstr.hash = full_name_hash(entry->name,qstr.len);

	dentry = lookup_hash(&qstr,parent_dentry);
	if (IS_ERR(dentry)) {
		error = PTR_ERR(dentry);
		up(&parent_dentry->d_inode->i_sem);
		dput(parent_dentry);
	} else {
		dentry->d_fsdata = (void *) entry;
		entry->dentry = dentry;
		error = vfs_mkdir(parent_dentry->d_inode,dentry,entry->mode);
		up(&parent_dentry->d_inode->i_sem);
	}

	if (error)
		put_mount();
	return error;
}

/**
 * driverfs_create_file - create a file
 * @entry:	structure describing the file
 * @parent:	directory to create it in
 */
int
driverfs_create_file(struct driver_file_entry * entry,
		     struct driver_dir_entry * parent)
{
	struct dentry * dentry;
	struct dentry * parent_dentry;
	struct qstr qstr;
	int error = 0;

	if (!entry || !parent)
		return -EINVAL;

	/* make sure we're mounted */
	get_mount();

	if (!parent->dentry) {
		put_mount();
		return -EINVAL;
	}

	/* make sure daddy doesn't run out on us again... */
	parent_dentry = dget(parent->dentry);
	down(&parent_dentry->d_inode->i_sem);

	qstr.name = entry->name;
	qstr.len = strlen(entry->name);
	qstr.hash = full_name_hash(entry->name,qstr.len);

	dentry = lookup_hash(&qstr,parent_dentry);
	if (IS_ERR(dentry))
		error = PTR_ERR(dentry);
	else
		error = vfs_create(parent_dentry->d_inode,dentry,entry->mode);


	/* Still good? Ok, then fill in the blanks: */
	if (!error) {
		dentry->d_fsdata = (void *)entry;
		dentry->d_inode->u.generic_ip = (void *)entry;

		entry->dentry = dentry;
		entry->parent = parent;

		list_add_tail(&entry->node,&parent->files);
	}
	up(&parent_dentry->d_inode->i_sem);

	if (error) {
		dput(parent_dentry);
		put_mount();
	}
	return error;
}

/**
 * __remove_file - remove a regular file in the filesystem
 * @dentry:	dentry of file to remove
 *
 * Call unlink to remove the file, and dput on the dentry to drop
 * the refcount.
 */
static void __remove_file(struct dentry * dentry)
{
	dget(dentry);
	down(&dentry->d_inode->i_sem);

	vfs_unlink(dentry->d_parent->d_inode,dentry);

	unlock_dir(dentry);

	/* remove reference count from when file was created */
	dput(dentry);

	put_mount();
}

/**
 * driverfs_remove_file - exported file removal
 * @dir:	directory the file supposedly resides in
 * @name:	name of the file
 *
 * Try and find the file in the dir's list.
 * If it's there, call __remove_file() (above) for the dentry.
 */
void driverfs_remove_file(struct driver_dir_entry * dir, const char * name)
{
	struct dentry * dentry;
	struct list_head * node;

	if (!dir->dentry)
		return;

	dentry = dget(dir->dentry);
	down(&dentry->d_inode->i_sem);

	node = dir->files.next;
	while (node != &dir->files) {
		struct driver_file_entry * entry = NULL;

		entry = list_entry(node,struct driver_file_entry,node);
		if (!strcmp(entry->name,name)) {
			list_del_init(node);

			__remove_file(entry->dentry);
			break;
		}
		node = node->next;
	}
	unlock_dir(dentry);
}

/**
 * driverfs_remove_dir - exportable directory removal
 * @dir:	directory to remove
 *
 * To make sure we don't orphan anyone, first remove
 * all the children in the list, then do vfs_rmdir() to remove it
 * and decrement the refcount..
 */
void driverfs_remove_dir(struct driver_dir_entry * dir)
{
	struct list_head * node;
	struct dentry * dentry;

	if (!dir->dentry)
		goto done;

	/* lock the directory while we remove the files */
	dentry = dget(dir->dentry);
	down(&dentry->d_inode->i_sem);

	node = dir->files.next;
	while (node != &dir->files) {
		struct driver_file_entry * entry;
		entry = list_entry(node,struct driver_file_entry,node);

		list_del_init(node);

		__remove_file(entry->dentry);

		node = dir->files.next;
	}

	/* now lock the parent, so we can remove this directory */
	lock_parent(dentry);

	vfs_rmdir(dentry->d_parent->d_inode,dentry);
	double_unlock(dentry,dentry->d_parent);

	/* remove reference count from when directory was created */
	dput(dentry);
 done:
	put_mount();
}
