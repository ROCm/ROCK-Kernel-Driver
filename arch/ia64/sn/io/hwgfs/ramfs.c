/*
 * Copyright (c) 2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  Mostly shameless copied from Linus Torvalds' ramfs and thus
 *  Copyright (C) 2000 Linus Torvalds.
 *                2000 Transmeta Corp.
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
 * or the like.  Any license provided herein, whether implied or
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

#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/uaccess.h>

/* some random number */
#define HWGFS_MAGIC	0x12061983

static struct super_operations hwgfs_ops;
static struct address_space_operations hwgfs_aops;
static struct file_operations hwgfs_file_operations;
static struct inode_operations hwgfs_file_inode_operations;
static struct inode_operations hwgfs_dir_inode_operations;

static struct backing_dev_info hwgfs_backing_dev_info = {
	.ra_pages       = 0,	/* No readahead */
	.memory_backed  = 1,	/* Does not contribute to dirty memory */
};

static struct inode *hwgfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &hwgfs_aops;
		inode->i_mapping->backing_dev_info = &hwgfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &hwgfs_file_inode_operations;
			inode->i_fop = &hwgfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &hwgfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			inode->i_nlink++;
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

static int hwgfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode * inode = hwgfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);		/* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int hwgfs_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	return hwgfs_mknod(dir, dentry, mode | S_IFDIR, 0);
}

static int hwgfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return hwgfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int hwgfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = hwgfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	return error;
}

static struct address_space_operations hwgfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct file_operations hwgfs_file_operations = {
	.read		= generic_file_read,
	.write		= generic_file_write,
	.mmap		= generic_file_mmap,
	.fsync		= simple_sync_file,
	.sendfile	= generic_file_sendfile,
};

static struct inode_operations hwgfs_file_inode_operations = {
	.getattr	= simple_getattr,
};

static struct inode_operations hwgfs_dir_inode_operations = {
	.create		= hwgfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= hwgfs_symlink,
	.mkdir		= hwgfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= hwgfs_mknod,
	.rename		= simple_rename,
};

static struct super_operations hwgfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

static int hwgfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = HWGFS_MAGIC;
	sb->s_op = &hwgfs_ops;
	inode = hwgfs_get_inode(sb, S_IFDIR | 0755, 0);
	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

static struct super_block *hwgfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, hwgfs_fill_super);
}

static struct file_system_type hwgfs_fs_type = {
	.owner		= THIS_MODULE,
	.name           = "hwgfs",
	.get_sb         = hwgfs_get_sb,
	.kill_sb        = kill_litter_super,
};

struct vfsmount *hwgfs_vfsmount;

int __init init_hwgfs_fs(void)
{
	int error;

	error = register_filesystem(&hwgfs_fs_type);
	if (error)
		return error;

	hwgfs_vfsmount = kern_mount(&hwgfs_fs_type);
	if (IS_ERR(hwgfs_vfsmount))
		goto fail;
	return 0;

fail:
	unregister_filesystem(&hwgfs_fs_type);
	return PTR_ERR(hwgfs_vfsmount);
}

static void __exit exit_hwgfs_fs(void)
{
	unregister_filesystem(&hwgfs_fs_type);
}

MODULE_LICENSE("GPL");

module_init(init_hwgfs_fs)
module_exit(exit_hwgfs_fs)
