/* 
 * fs/rcfs/inode.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *               Vivek Kashyap,   IBM Corp. 2004
 *           
 * 
 * Resource class filesystem (rcfs) forming the 
 * user interface to Class-based Kernel Resource Management (CKRM).
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 *
 * 05 Mar 2004
 *        Created.
 * 06 Mar 2004
 *        Parsing for shares added
 */


#include <linux/module.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/namespace.h>
#include <linux/dcache.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/parser.h>
#include <asm/uaccess.h>

#include <linux/rcfs.h>



// Address of variable used as flag to indicate a magic file, 
// ; value unimportant 
int RCFS_IS_MAGIC;


struct inode *rcfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			// Treat as default assignment */
			inode->i_op = &rcfs_file_inode_operations;
			// inode->i_fop = &rcfs_file_operations;
			break;
		case S_IFDIR:
			// inode->i_op = &rcfs_dir_inode_operations;
			inode->i_op = &rcfs_rootdir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			// directory inodes start off with i_nlink == 2 
			//  (for "." entry)
 
			inode->i_nlink++;
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}



int
_rcfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode;
	int error = -EPERM;

	if (dentry->d_inode)
		return -EEXIST;

	inode = rcfs_get_inode(dir->i_sb, mode, dev);
	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
		d_instantiate(dentry, inode);
		dget(dentry);	
		error = 0;
	}

	return error;
}
EXPORT_SYMBOL(_rcfs_mknod);


int
rcfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	// User can only create directories, not files
	if ((mode & S_IFMT) != S_IFDIR)
		return -EINVAL;

	return  dir->i_op->mkdir(dir, dentry, mode);
}
EXPORT_SYMBOL(rcfs_mknod);


struct dentry * 
rcfs_create_internal(struct dentry *parent, struct rcfs_magf *magf, int magic)
{
	struct qstr qstr;
	struct dentry *mfdentry ;

	// Get new dentry for name  
 	qstr.name = magf->name;
 	qstr.len = strlen(magf->name);
 	qstr.hash = full_name_hash(magf->name,qstr.len);
	mfdentry = lookup_hash(&qstr,parent);

	if (!IS_ERR(mfdentry)) {
		int err; 

		down(&parent->d_inode->i_sem);
 		if (magic && (magf->mode & S_IFDIR))
 			err = parent->d_inode->i_op->mkdir(parent->d_inode,
						   mfdentry, magf->mode);
		else {
 			err =_rcfs_mknod(parent->d_inode,mfdentry,
					 magf->mode,0);
			// _rcfs_mknod doesn't increment parent's link count, 
			// i_op->mkdir does.
			parent->d_inode->i_nlink++;
		}
		up(&parent->d_inode->i_sem);

		if (err) {
			dput(mfdentry);
			return mfdentry;
		}
	}
	return mfdentry ;
}
EXPORT_SYMBOL(rcfs_create_internal);

int 
rcfs_delete_internal(struct dentry *mfdentry)
{
	struct dentry *parent ;

	if (!mfdentry || !mfdentry->d_parent)
		return -EINVAL;
	
	parent = mfdentry->d_parent;

	if (!mfdentry->d_inode) {
		return 0;
	}
	down(&mfdentry->d_inode->i_sem);
	if (S_ISDIR(mfdentry->d_inode->i_mode))
		simple_rmdir(parent->d_inode, mfdentry);
	else
		simple_unlink(parent->d_inode, mfdentry);
	up(&mfdentry->d_inode->i_sem);

	d_delete(mfdentry);

	return 0;
}
EXPORT_SYMBOL(rcfs_delete_internal);

struct inode_operations rcfs_file_inode_operations = {
	.getattr	= simple_getattr,
};
		





