/* ckrm_socketaq.c 
 *
 * Copyright (C) Vivek Kashyap,      IBM Corp. 2004
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
 * Initial version
 */

/*******************************************************************************
 *  Socket class type
 *   
 * Defines the root structure for socket based classes. Currently only inbound
 * connection control is supported based on prioritized accept queues. 
 ******************************************************************************/


#include <linux/rcfs.h>
#include <net/tcp.h>

extern int rcfs_create(struct inode *,struct dentry *, int, struct nameidata *);
extern int rcfs_unlink(struct inode *, struct dentry *);
extern int  rcfs_symlink(struct inode *, struct dentry *, const char *);
extern int rcfs_mknod(struct inode *, struct dentry *, int mode, dev_t);
extern int rcfs_mkdir(struct inode *, struct dentry *, int);
extern int rcfs_rmdir(struct inode *, struct dentry *);
extern int rcfs_rename(struct inode *, struct dentry *, struct inode *, 
		struct dentry *);

extern int rcfs_create_coredir(struct inode *, struct dentry *);
int sock_mkdir(struct inode *, struct dentry *, int mode);
int sock_rmdir(struct inode *, struct dentry *);


int sock_create_noperm(struct inode *, struct dentry *,int, struct nameidata *);
int sock_unlink_noperm(struct inode *,struct dentry *);
int sock_mkdir_noperm(struct inode *,struct dentry *,int);
int sock_rmdir_noperm(struct inode *,struct dentry *);
int sock_mknod_noperm(struct inode *,struct dentry *,int, dev_t);

void sock_set_directory(void);

extern struct file_operations config_fileops,
			members_fileops,
			shares_fileops,
			stats_fileops,
			target_fileops;


struct inode_operations my_iops = {
	        .create         = rcfs_create,
		.lookup         = simple_lookup,
		.link           = simple_link,
		.unlink         = rcfs_unlink,
		.symlink        = rcfs_symlink,
		.mkdir          = sock_mkdir,
		.rmdir          = sock_rmdir,
		.mknod          = rcfs_mknod,
		.rename         = rcfs_rename,
};

struct inode_operations class_iops = {
	        .create         = sock_create_noperm,
		.lookup         = simple_lookup,
		.link           = simple_link,
		.unlink         = sock_unlink_noperm,
		.symlink        = rcfs_symlink,
		.mkdir          = sock_mkdir_noperm,
		.rmdir          = sock_rmdir_noperm,
		.mknod          = sock_mknod_noperm,
		.rename         = rcfs_rename,
};

struct inode_operations sub_iops = {
	        .create         = sock_create_noperm,
		.lookup         = simple_lookup,
		.link           = simple_link,
		.unlink         = sock_unlink_noperm,
		.symlink        = rcfs_symlink,
		.mkdir          = sock_mkdir_noperm,
		.rmdir          = sock_rmdir_noperm,
		.mknod          = sock_mknod_noperm,
		.rename         = rcfs_rename,
};

struct rcfs_magf def_magf = {
	.mode = RCFS_DEFAULT_DIR_MODE,
	.i_op = &sub_iops,
	.i_fop = NULL,
};

struct rcfs_magf sock_rootdesc[] = {
	{
	//	.name = should not be set, copy from classtype name,
		.mode = RCFS_DEFAULT_DIR_MODE,
		.i_op = &my_iops,
		//.i_fop   = &simple_dir_operations,
		.i_fop = NULL,
	},
	{
		.name = "members",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &members_fileops,
	},
	{
		.name = "target",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &target_fileops,
	},
};

struct rcfs_magf sock_magf[] = {
	{
		.name = "config",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &config_fileops,
	},
	{
		.name = "members",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop =&members_fileops,
	},
	{
		.name = "shares",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &shares_fileops,
	},
	{
		.name = "stats",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &stats_fileops,
	},
	{
		.name = "target",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &target_fileops,
	},
};

struct rcfs_magf sub_magf[] = {
	{
		.name = "config",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &config_fileops,
	},
	{
		.name = "shares",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &shares_fileops,
	},
	{
		.name = "stats",
		.mode = RCFS_DEFAULT_FILE_MODE,
		.i_op = &my_iops,
		.i_fop = &stats_fileops,
	},
};

struct rcfs_mfdesc sock_mfdesc = {
	.rootmf		= sock_rootdesc,
	.rootmflen 	= (sizeof(sock_rootdesc)/sizeof(struct rcfs_magf)),
};


#define SOCK_MAX_MAGF (sizeof(sock_magf)/sizeof(struct rcfs_magf))
#define LAQ_MAX_SUBMAGF (sizeof(sub_magf)/sizeof(struct rcfs_magf))

int 
sock_rmdir(struct inode *p, struct dentry *me)
{
	struct dentry *mftmp, *mfdentry ;

	// delete all magic sub directories
	list_for_each_entry_safe(mfdentry, mftmp, &me->d_subdirs, d_child) {
		if (S_ISDIR(mfdentry->d_inode->i_mode))
			rcfs_rmdir(me->d_inode, mfdentry);
	}
	// delete ourselves
	rcfs_rmdir(p,me);

	return 0;
}

#ifdef NUM_ACCEPT_QUEUES
#define LAQ_NUM_ACCEPT_QUEUES NUM_ACCEPT_QUEUES
#else
#define LAQ_NUM_ACCEPT_QUEUES 0
#endif

int
sock_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int retval = 0;
	int i,j;
	struct dentry *pentry, *mfdentry;

	if (_rcfs_mknod(dir, dentry, mode | S_IFDIR, 0)) {
		printk(KERN_ERR "rcfs_mkdir: error reaching parent\n");
		return retval;
	}
	
	// Needed if only _rcfs_mknod is used instead of i_op->mkdir
	dir->i_nlink++;

	retval = rcfs_create_coredir(dir, dentry);
	if (retval) 
		goto mkdir_err;

	/* create the default set of magic files */
	for (i =0; i < SOCK_MAX_MAGF; i++) {
		mfdentry = rcfs_create_internal(dentry, &sock_magf[i],0);
		mfdentry->d_fsdata = &RCFS_IS_MAGIC;
		RCFS_I(mfdentry->d_inode)->core = 
				RCFS_I(dentry->d_inode)->core;
		if (sock_magf[i].i_fop)
			mfdentry->d_inode->i_fop = sock_magf[i].i_fop;
		if (sock_magf[i].i_op)
			mfdentry->d_inode->i_op = sock_magf[i].i_op;
	}
	
	for (i=1; i < LAQ_NUM_ACCEPT_QUEUES; i++) {
		j = sprintf(def_magf.name, "%d",i);
		def_magf.name[j] = '\0';

		pentry = rcfs_create_internal(dentry, &def_magf,0);
		retval = rcfs_create_coredir(dentry->d_inode, pentry);
		if (retval)
			goto mkdir_err;
		for (j=0; j < LAQ_MAX_SUBMAGF; j++) {
			mfdentry = rcfs_create_internal(pentry, &sub_magf[j],0);
			mfdentry->d_fsdata = &RCFS_IS_MAGIC;
			RCFS_I(mfdentry->d_inode)->core = 
					RCFS_I(pentry->d_inode)->core;
			if (sub_magf[j].i_fop)
				mfdentry->d_inode->i_fop = sub_magf[j].i_fop;
			if (sub_magf[j].i_op)
				mfdentry->d_inode->i_op = sub_magf[j].i_op;
		}
		pentry->d_inode->i_op = &sub_iops;
	}
	dentry->d_inode->i_op = &class_iops;
	return 0;

mkdir_err:
	// Needed
	dir->i_nlink--;
	return retval;
}
#ifndef NUM_ACCEPT_QUEUES
#define NUM_ACCEPT_QUEUES 0
#endif

char *
sock_get_name(struct ckrm_core_class *c)
{
	char *p = (char *)c->name;
	
	while(*p)
		p++;
	while( *p != '/' && p != c->name)
		p--;

	return ++p;
}

int 
sock_create_noperm(struct inode *dir,struct dentry *dentry,int mode, struct nameidata *nd)
{
	return -EPERM;
}

int 
sock_unlink_noperm(struct inode *dir,struct dentry *dentry)
{
	return -EPERM;
}

int 
sock_mkdir_noperm(struct inode *dir,struct dentry *dentry, int mode)
{
	return -EPERM;
}

int 
sock_rmdir_noperm(struct inode *dir,struct dentry *dentry)
{
	return -EPERM;
}

int 
sock_mknod_noperm(struct inode *dir,struct dentry *dentry,int mode, dev_t dev)
{
	return -EPERM;
}

#if 0
void
sock_set_directory()
{
	struct dentry *pentry, *dentry;

	pentry = rcfs_set_magf_byname("listen_aq", (void *)&my_dir_magf[0]);
	if (pentry) {
		dentry = rcfs_create_internal(pentry, &my_dir_magf[1],0);
		if (my_dir_magf[1].i_fop)
			dentry->d_inode->i_fop = my_dir_magf[1].i_fop;
		RCFS_I(dentry->d_inode)->core = 
				RCFS_I(pentry->d_inode)->core;
		dentry = rcfs_create_internal(pentry, &my_dir_magf[2],0);
		if (my_dir_magf[2].i_fop)
			dentry->d_inode->i_fop = my_dir_magf[2].i_fop;
		RCFS_I(dentry->d_inode)->core = 
				RCFS_I(pentry->d_inode)->core;
	}
	else  {
		printk(KERN_ERR "Could not create /rcfs/listen_aq\n"
				"Perhaps /rcfs needs to be mounted\n");
	}
}
#endif

