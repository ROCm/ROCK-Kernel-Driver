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



#define rcfs_positive(dentry)  ((dentry)->d_inode && !d_unhashed((dentry)))

int rcfs_empty(struct dentry *dentry)
{
        struct dentry *child;
        int ret = 0;
                                                                                               
        spin_lock(&dcache_lock);
        list_for_each_entry(child, &dentry->d_subdirs, d_child)
                if (!rcfs_is_magic(child) && rcfs_positive(child))
                        goto out;
        ret = 1;
out:
        spin_unlock(&dcache_lock);
        return ret;
}

                                                                                               


/* Directory inode operations */


int 
rcfs_create(struct inode *dir, struct dentry *dentry, int mode, 
	    struct nameidata *nd)
{
	return rcfs_mknod(dir, dentry, mode | S_IFREG, 0);
}
EXPORT_SYMBOL(rcfs_create);


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
EXPORT_SYMBOL(rcfs_symlink);

int
rcfs_create_coredir(struct inode *dir, struct dentry *dentry)
{

	struct rcfs_inode_info *ripar, *ridir;
	int sz;

	ripar = RCFS_I(dir);
	ridir = RCFS_I(dentry->d_inode);

	// Inform RC's - do Core operations 
	if (ckrm_is_core_valid(ripar->core)) {
		sz = strlen(ripar->name) + strlen(dentry->d_name.name) + 2 ;
		ridir->name = kmalloc(sz, GFP_KERNEL);
		if (!ridir->name) {
			return -ENOMEM;
		}
		snprintf(ridir->name, sz,"%s/%s", ripar->name, 
			 dentry->d_name.name);
		ridir->core = (*(ripar->core->classtype->alloc))
			(ripar->core,ridir->name);
	}
	else {
		printk(KERN_ERR "rcfs_mkdir: Invalid parent core %p\n",
		       ripar->core);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(rcfs_create_coredir);


int
rcfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{

	int retval = 0;
	ckrm_classtype_t *clstype;

#if 0
	struct dentry *pd = list_entry(dir->i_dentry.next, struct dentry, 
							d_alias);
	if ((!strcmp(pd->d_name.name, "/") &&
	     !strcmp(dentry->d_name.name, "ce"))) {
		// Call CE's mkdir if it has registered, else fail.
		if (rcfs_eng_callbacks.mkdir) {
			return (*rcfs_eng_callbacks.mkdir)(dir, dentry, mode);
		} else {
			return -EINVAL;
		}
	}
#endif

	if (_rcfs_mknod(dir, dentry, mode | S_IFDIR, 0)) {
		printk(KERN_ERR "rcfs_mkdir: error in _rcfs_mknod\n");
		return retval;
	}

	dir->i_nlink++;

	// Inherit parent's ops since _rcfs_mknod assigns noperm ops
	dentry->d_inode->i_op = dir->i_op;
	dentry->d_inode->i_fop = dir->i_fop;


 	retval = rcfs_create_coredir(dir, dentry);
 	if (retval) {
		simple_rmdir(dir,dentry);
		return retval;
                // goto mkdir_err;
	}
 
 	// create the default set of magic files 
	clstype = (RCFS_I(dentry->d_inode))->core->classtype;
	rcfs_create_magic(dentry, &(((struct rcfs_magf*)clstype->mfdesc)[1]), 
			  clstype->mfcount-1);

	return retval;

//mkdir_err:
	dir->i_nlink--;
	return retval;
}
EXPORT_SYMBOL(rcfs_mkdir);


int 
rcfs_rmdir(struct inode * dir, struct dentry * dentry)
{
	struct rcfs_inode_info *ri = RCFS_I(dentry->d_inode);

#if 0
	struct dentry *pd = list_entry(dir->i_dentry.next, 
				       struct dentry, d_alias);
	if ((!strcmp(pd->d_name.name, "/") &&
	     !strcmp(dentry->d_name.name, "ce"))) {
		// Call CE's mkdir if it has registered, else fail.
		if (rcfs_eng_callbacks.rmdir) {
			return (*rcfs_eng_callbacks.rmdir)(dir, dentry);
		} else {
			return simple_rmdir(dir, dentry);
		}
	}
	else if ((!strcmp(pd->d_name.name, "/") &&
		  !strcmp(dentry->d_name.name, "network"))) {
		return -EPERM;
	}
#endif
	
	if (!rcfs_empty(dentry)) {
		printk(KERN_ERR "rcfs_rmdir: directory not empty\n");
		goto out;
	}

	// Core class removal 

	if (ri->core == NULL) {
		printk(KERN_ERR "rcfs_rmdir: core==NULL\n");
		// likely a race condition
		return 0;
	}

	if ((*(ri->core->classtype->free))(ri->core)) {
		printk(KERN_ERR "rcfs_rmdir: ckrm_free_core_class failed\n");
		goto out;
	}
	ri->core = NULL ; // just to be safe 

	// Clear magic files only after core successfully removed 
 	rcfs_clear_magic(dentry);

	return simple_rmdir(dir, dentry);

out:
	return -EBUSY;
}
EXPORT_SYMBOL(rcfs_rmdir);


int
rcfs_unlink(struct inode *dir, struct dentry *dentry)
{
	// -ENOENT and not -ENOPERM to allow rm -rf to work despite 
	// magic files being present
	return -ENOENT;
}
EXPORT_SYMBOL(rcfs_unlink);
	
// rename is allowed on directories only
int
rcfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	if (S_ISDIR(old_dentry->d_inode->i_mode)) 
		return simple_rename(old_dir, old_dentry, new_dir, new_dentry);
	else
		return -EINVAL;
}
EXPORT_SYMBOL(rcfs_rename);


struct inode_operations rcfs_dir_inode_operations = {
	.create		= rcfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= rcfs_unlink,
	.symlink	= rcfs_symlink,
	.mkdir		= rcfs_mkdir,
	.rmdir          = rcfs_rmdir,
	.mknod		= rcfs_mknod,
	.rename		= rcfs_rename,
};





int 
rcfs_root_create(struct inode *dir, struct dentry *dentry, int mode, 
		 struct nameidata *nd)
{
	return -EPERM;
}


int  
rcfs_root_symlink(struct inode * dir, struct dentry *dentry, 
		  const char * symname)
{
	return -EPERM;
}

int 
rcfs_root_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	return -EPERM;
}

int 
rcfs_root_rmdir(struct inode * dir, struct dentry * dentry)
{
	return -EPERM;
}

int
rcfs_root_unlink(struct inode *dir, struct dentry *dentry)
{
	return -EPERM;
}

int
rcfs_root_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	return -EPERM;
}
	
int
rcfs_root_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	return -EPERM;
}

struct inode_operations rcfs_rootdir_inode_operations = {
	.create		= rcfs_root_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= rcfs_root_unlink,
	.symlink	= rcfs_root_symlink,
	.mkdir		= rcfs_root_mkdir,
	.rmdir          = rcfs_root_rmdir,
	.mknod		= rcfs_root_mknod,
	.rename		= rcfs_root_rename,
};
