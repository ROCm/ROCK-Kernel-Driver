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

#include "rcfs.h"
#include "magic.h"


/* Address of variable used as flag to indicate a magic file, value unimportant */
int RCFS_IS_MAGIC;


//static struct magf_t mymagf = { "mymagf", NULL, 100, &rcfs_file_operations };


static struct backing_dev_info rcfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};


struct inode *rcfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &rcfs_aops;
		inode->i_mapping->backing_dev_info = &rcfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			/* Treat as default assignment */
			inode->i_op = &rcfs_file_inode_operations;
			inode->i_fop = &rcfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &rcfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
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
	int error = -ENOSPC, i=0;

	inode = rcfs_get_inode(dir->i_sb, mode, dev);

	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;

		printk (KERN_INFO "%s being created\n", dentry->d_name.name);


		/* Reassign i_op, i_fop for magic files */
		/* Safe to do here as /rcfs/ce, /rcfs/network shouldn't invoke
		   rcfs_mknod */

		i=0;
		while ((i < NR_MAGF) && magf[i].name && 
		       (strnicmp(dentry->d_name.name,magf[i].name,MAGF_NAMELEN))) 
			i++;
		
		if (i < NR_MAGF) {
			if (magf[i].i_fop)
				inode->i_fop = magf[i].i_fop;
			if (magf[i].i_op)
				inode->i_op = magf[i].i_op;
		}
	}
	return error;
}

int
rcfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode;
	int err = -ENOSPC, i = 0;

        // printk(KERN_ERR "rcfs_mknod called with dir=%p dentry=%p mode=%d\n",dir,dentry,mode);
	// Do not allow creation of files by the user. Only directories i.e.
	// the classes can be created.

	/*
	if ((mode & S_IFMT) == S_IFREG)
		return -EINVAL;
	else 
	*/
	return _rcfs_mknod(dir, dentry, mode, dev);
}


#if 0
int
rcfs_create_magic(struct dentry *parent, struct magf_t *magf)
{
	struct qstr qstr;
	struct dentry *mfdentry ;


	if (!(magf && magf->name && magf->mode && (magf->i_op || magf->i_fop)))
		return -EINVAL;

	dget(parent);

	/* Get new dentry for name  */
	qstr.name = magf->name;
	qstr.len = strlen(magf->name);
	qstr.hash = full_name_hash(magf->name,qstr.len);
	mfdentry = lookup_hash(&qstr,parent);

	if (!IS_ERR(mfdentry)) {
		int err; 

		down(&parent->d_inode->i_sem);
		err = rcfs_mknod(parent->d_inode, mfdentry, magf->mode, 0);
		up(&parent->d_inode->i_sem);

		if (err) {
			dput(mfdentry);
			dput(parent);
			return -EINVAL;
		}

		if (magf->i_op)
			mfdentry->d_inode->i_op = magf->i_op;
		if (magf->i_fop)
			mfdentry->d_inode->i_fop = magf->i_fop;
		mfdentry->d_fsdata = &RCFS_IS_MAGIC; // flag for magic file/dir
	}
	dput(parent);

	return 0 ;
}
#endif

struct dentry * 
rcfs_create_internal(struct dentry *parent, const char *name, int mfmode, 
			int magic)
{
	struct qstr qstr;
	struct dentry *mfdentry ;

	
	/* Get new dentry for name  */
	qstr.name = name;
	qstr.len = strlen(name);
	qstr.hash = full_name_hash(name,qstr.len);
	mfdentry = lookup_hash(&qstr,parent);

	printk(KERN_INFO "parent %p name %s mfdentry is %x\n",parent, name, mfdentry);
	if (!IS_ERR(mfdentry)) {
		int err; 

		down(&parent->d_inode->i_sem);
		if (magic)
			err = rcfs_mkdir(parent->d_inode, mfdentry, mfmode, 0);
		else
			err = _rcfs_mknod(parent->d_inode, mfdentry, mfmode, 0);
		parent->d_inode->i_nlink++;
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
rcfs_delete_all_magic(struct dentry *parent)
{
	struct dentry *mftmp, *mfdentry ;
	umode_t mfmode;

//	if (!(magf && magf->name))
//		return NULL;
	
	down(&parent->d_inode->i_sem);

	//dget(parent);

	list_for_each_entry_safe(mfdentry, mftmp, &parent->d_subdirs, d_child) {
		int x=0;
		if (!rcfs_is_magic(mfdentry))
			continue ;

		mfmode =  (mfdentry->d_inode->i_mode);
			
		if (mfmode & S_IFREG)
			simple_unlink(parent->d_inode, mfdentry);
		if (mfmode & S_IFDIR)
			simple_rmdir(parent->d_inode, mfdentry);

		d_delete(mfdentry);
		dput(mfdentry);
	}
	up(&parent->d_inode->i_sem);		
		
#if 0
	if (!parent || (parent->d_inode != dir))
		return -EINVAL;
//	down(&parent->d_inode->i_sem);
	d_delete(magf->dentry);
	simple_unlink(parent->d_inode, magf->dentry);
	dput(magf->dentry);
//	up(&parent->d_inode->i_sem);
#endif

	return 0;
}


struct inode_operations rcfs_file_inode_operations = {
//	.getattr	= simple_getattr,
};
		


	





