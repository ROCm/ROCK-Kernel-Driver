/* 
 * fs/rcfs/dir.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *               Vivek Kashyap,   IBM Corp. 2004
 *           
 * 
 * Directory operations for rcfs
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
 * 08 Mar 2004
 *        Created.
 */


#include <linux/module.h>
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
#include "magic.h"


rbce_eng_callback_t rcfs_eng_callbacks = {
	NULL, NULL
};
/* Helper functions */

#define rcfs_positive(dentry)  ((dentry)->d_inode && !d_unhashed((dentry)))

#if 0
static
int rcfs_empty(struct dentry *dentry)
{
	struct dentry *child;
	int ret = 0;

	/* Don't use now - don't know if its safe to take the dcache_lock */
	return 0 ;

	spin_lock(&dcache_lock);
	list_for_each_entry(child, &dentry->d_subdirs, d_child) 
		if (!rcfs_is_magic(child) && rcfs_positive(child))
			goto out;
	ret = 1;
out:
	spin_unlock(&dcache_lock);
	return ret;
}
#endif


int 
rcfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	// USEME later when files are only created automagically
	// return -EPERM;
	return rcfs_mknod(dir, dentry, mode | S_IFREG, 0);
}


/* Directory inode operations */


/* Symlinks permitted ?? */
int  
rcfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = rcfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			if (dir->i_mode & S_ISGID)
				inode->i_gid = dir->i_gid;
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	return error;
}


int 
rcfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
#if 0
	struct dentry *ldentry;
	int mfmode,i;
#endif
	int retval;
	struct rcfs_inode_info *ripar,*ridir;
	struct dentry *pd = list_entry(dir->i_dentry.next, struct dentry, d_alias);

//	printk(KERN_ERR "rcfs_mkdir called with dir=%p dentry=%p mode=%d\n",dir,dentry,mode);

	if ((!strcmp(pd->d_name.name, "/") &&
				!strcmp(dentry->d_name.name, "ce"))) {
		// Call CE's mkdir if it has registered, else fail.
		if (rcfs_eng_callbacks.mkdir) {
			return (*rcfs_eng_callbacks.mkdir)(dir, dentry, mode);
		} else {
			return -EINVAL;
		}
	}


	// Creation of /rcfs/network reserved for network controllers

	if ((!strcmp(pd->d_name.name, "/") &&
				!strcmp(dentry->d_name.name, "network"))) {
		return -EPERM;
	}

#if 0
	// Creation in /rcfs/network is not allowed
	// XXX - would be good to instead add a check with the parent's 
	// core class
	{
		extern struct dentry *rcfs_nwde;
		
		if (dir == rcfs_nwde->d_inode)
			return -EPERM;
	}
#endif


	retval  = rcfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (retval) {
		printk(KERN_ERR "rcfs_mkdir: error reaching parent\n");
		return retval;
	}

	dir->i_nlink++;

	ripar = RCFS_I(dir);
	ridir = RCFS_I(dentry->d_inode);

	/* Inform RC's - do Core operations */
	/* On error, goto mkdir_err */

	if (is_core_valid(ripar->core))
		ridir->core = ckrm_alloc_core_class(
				(ckrm_core_class_t *)ripar->core, dentry);
	else {
		printk(KERN_ERR "rcfs_mkdir: Invalid parent core \n");
		return -EINVAL;
	}
#if 0
	mfmode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;	
	for (i=0; i < NR_MAGF; i++) {
		ldentry = 
			rcfs_create_internal(dentry, magf[i].name, mfmode, 0);
		if (!ldentry) {
			printk(KERN_ERR "rcfs_mkdir: error creating magic files. Improve handling\n");
			goto mkdir_err;
		}
		ldentry->d_fsdata = &RCFS_IS_MAGIC;
 		if (magf[i].i_op)
 			ldentry->d_inode->i_op = magf[i].i_op;
 		if (magf[i].i_fop)
 			ldentry->d_inode->i_fop = magf[i].i_fop;
 
 		printk(KERN_ERR "rcfs_mkdir: magic entry created(%s, inode %p, dentry %p) created\n",ldentry->d_name.name, ldentry->d_inode, ldentry);

	}
#endif
	return retval;

//mkdir_err:
	dir->i_nlink--;
	return -EINVAL;
}


int 
rcfs_rmdir(struct inode * dir, struct dentry * dentry)
{
	struct rcfs_inode_info *ri = RCFS_I(dentry->d_inode);
	struct dentry *pd = list_entry(dir->i_dentry.next, struct dentry, d_alias);

	
//	printk(KERN_ERR "dir %p, dentry name %s inode %p, parent name %s inode %p\n", dir, dentry->d_name.name, dentry->d_inode, dentry->d_parent->d_name.name, dentry->d_parent->d_inode);
	

	/* Class about to be deleted.
	   Order of operations
	   - underlying rcfs entry removal (so no new ops initiated from user space)
	   - Core class removal (should handle CE initiated changes as well)
	   - rcfs dir removal 
	*/


	/* Ensure following first
	   a) members/ subdir empty.
	   b) no subdirs except members, no files except magic (latter not necessary
	      once we disallow all file creation.
	   c) remove members/ & magic files
	   If errors doing any of those, barf.
	*/
#if 0
 	rcfs_clear_magic(dentry);
#endif
 	// FIXME - this should be only be removed automagically on CE unregister only
	
	if ((!strcmp(pd->d_name.name, "/") &&
				!strcmp(dentry->d_name.name, "ce"))) {
		// Call CE's mkdir if it has registered, else fail.
		if (rcfs_eng_callbacks.rmdir) {
			return (*rcfs_eng_callbacks.rmdir)(dir, dentry);
		} else {
			return simple_rmdir(dir, dentry);
		}
	}


	/* Core class removal */
	// printk(KERN_ERR "About to remove %s ( %p)\n",dentry->d_name.name, ri);

	if (ckrm_free_core_class(ri->core)) {
		printk(KERN_ERR "rcfs_rmdir: ckrm_free_core_class failed\n");
		goto recreate_magic;
	}
	ri->core = NULL ; /* just to be safe */

	/*
	dentry->d_inode->i_nlink--;	
	*/
	return simple_rmdir(dir, dentry);

recreate_magic:
	printk("rcfs_rmdir: should recreate magic files here. Do manually now\n");
	return -EINVAL;
}

int
rcfs_register_engine(rbce_eng_callback_t *rcbs)
{
	if (!rcbs->mkdir || rcfs_eng_callbacks.mkdir) {
		return -EINVAL;
	}
	rcfs_eng_callbacks = *rcbs;
	return 0;
}

int
rcfs_unregister_engine(rbce_eng_callback_t *rcbs)
{
	if (!rcbs->mkdir || !rcfs_eng_callbacks.mkdir ||
			(rcbs->mkdir != rcfs_eng_callbacks.mkdir)) {
		return -EINVAL;
	}
	rcfs_eng_callbacks.mkdir = NULL;
	rcfs_eng_callbacks.rmdir = NULL;
	return 0;
}

struct inode_operations rcfs_dir_inode_operations = {
	.create		= rcfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= rcfs_symlink,
	.mkdir		= rcfs_mkdir,
	.rmdir          = rcfs_rmdir,
	.mknod		= rcfs_mknod,
	.rename		= simple_rename,
};

EXPORT_SYMBOL(rcfs_register_engine);
EXPORT_SYMBOL(rcfs_unregister_engine);
