/* 
 * fs/rcfs/super.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 *		 Vivek Kashyap,   IBM Corp. 2004
 *           
 * Super block operations for rcfs
 * 
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
#include <linux/ckrm.h>


static kmem_cache_t *rcfs_inode_cachep;


inline struct rcfs_inode_info *RCFS_I(struct inode *inode)
{
	return container_of(inode, struct rcfs_inode_info, vfs_inode);
}
EXPORT_SYMBOL(RCFS_I);



static struct inode *
rcfs_alloc_inode(struct super_block *sb)
{
	struct rcfs_inode_info *ri;
	ri = (struct rcfs_inode_info *) kmem_cache_alloc(rcfs_inode_cachep, 
							 SLAB_KERNEL);
	if (!ri)
		return NULL;
	ri->name = NULL;
	return &ri->vfs_inode;
}

static void 
rcfs_destroy_inode(struct inode *inode)
{
	struct rcfs_inode_info *ri = RCFS_I(inode);

	kfree(ri->name);
	kmem_cache_free(rcfs_inode_cachep, RCFS_I(inode));
}

static void 
rcfs_init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct rcfs_inode_info *ri = (struct rcfs_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&ri->vfs_inode);
}

int 
rcfs_init_inodecache(void)
{
	rcfs_inode_cachep = kmem_cache_create("rcfs_inode_cache",
				sizeof(struct rcfs_inode_info),
				0, SLAB_HWCACHE_ALIGN | SLAB_RECLAIM_ACCOUNT,
				rcfs_init_once, NULL);
	if (rcfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void rcfs_destroy_inodecache(void)
{
	printk(KERN_WARNING "destroy inodecache was called\n");
	if (kmem_cache_destroy(rcfs_inode_cachep))
		printk(KERN_INFO "rcfs_inode_cache: not all structures were freed\n");
}

struct super_operations rcfs_super_ops =
{
	.alloc_inode	= rcfs_alloc_inode,
	.destroy_inode	= rcfs_destroy_inode,
	.statfs		= simple_statfs,
	.drop_inode     = generic_delete_inode,
};


struct dentry *rcfs_rootde; /* redundant since one can also get it from sb */
static struct inode *rcfs_root;
static struct rcfs_inode_info *rcfs_rootri;

static int rcfs_mounted;

static int rcfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	struct rcfs_inode_info *rootri;
	struct ckrm_classtype *clstype;
	int i,rc;

	sb->s_fs_info = NULL;
	if (rcfs_mounted) {
		return -EPERM;
	}
	rcfs_mounted++;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;	
	sb->s_magic = RCFS_MAGIC;
	sb->s_op = &rcfs_super_ops;
	inode = rcfs_get_inode(sb, S_IFDIR | 0755, 0);
	if (!inode)
		return -ENOMEM;
	inode->i_op = &rcfs_rootdir_inode_operations;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;

	
	// Link inode and core class 
	rootri = RCFS_I(inode);
	rootri->name = kmalloc(strlen(RCFS_ROOT) + 1, GFP_KERNEL);
	if (!rootri->name) {
		d_delete(root);
		iput(inode);
		return -ENOMEM;
	}
	strcpy(rootri->name, RCFS_ROOT);
	rootri->core = NULL;

	rcfs_root = inode;
	sb->s_fs_info = rcfs_root = inode;
	rcfs_rootde = root ;
	rcfs_rootri = rootri ;

	// register metatypes
	for ( i=0; i<CKRM_MAX_CLASSTYPES; i++) {
		clstype = ckrm_classtypes[i];
		if (clstype == NULL) 
			continue;
		printk("A non null classtype\n");

		if ((rc = rcfs_register_classtype(clstype)))
			continue ;  // could return with an error too 
	}

	// register CE's with rcfs 
	// check if CE loaded
	// call rcfs_register_engine for each classtype
	// AND rcfs_mkroot (preferably subsume latter in former) 

	return 0;
}


static struct super_block *rcfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_nodev(fs_type, flags, data, rcfs_fill_super);
}


void 
rcfs_kill_sb(struct super_block *sb)
{
	int i,rc;
	struct ckrm_classtype *clstype;

	if (sb->s_fs_info != rcfs_root) {
		generic_shutdown_super(sb);
		return;
	}
	rcfs_mounted--;

	for ( i=0; i < CKRM_MAX_CLASSTYPES; i++) {

		clstype = ckrm_classtypes[i];
		if (clstype == NULL || clstype->rootde == NULL) 
			continue;

		if ((rc = rcfs_deregister_classtype(clstype))) {
			printk(KERN_ERR "Error removing classtype %s\n",
			       clstype->name);
			// return ;   // can also choose to stop here
		}
	}
	
	// do not remove comment block until ce directory issue resolved
	// deregister CE with rcfs
	// Check if loaded
	// if ce is in  one directory /rcfs/ce, 
	//       rcfs_deregister_engine for all classtypes within above 
	//             codebase 
	//       followed by
	//       rcfs_rmroot here
	// if ce in multiple (per-classtype) directories
	//       call rbce_deregister_engine within ckrm_deregister_classtype

	// following will automatically clear rcfs root entry including its 
	//  rcfs_inode_info

	generic_shutdown_super(sb);

	// printk(KERN_ERR "Removed all entries\n");
}	


static struct file_system_type rcfs_fs_type = {
	.name		= "rcfs",
	.get_sb		= rcfs_get_sb,
	.kill_sb	= rcfs_kill_sb,
};

struct rcfs_functions my_rcfs_fn = {
	.mkroot               = rcfs_mkroot,
	.rmroot               = rcfs_rmroot,
	.register_classtype   = rcfs_register_classtype,
	.deregister_classtype = rcfs_deregister_classtype,
};

extern struct rcfs_functions rcfs_fn ;

static int __init init_rcfs_fs(void)
{
	int ret;

	ret = register_filesystem(&rcfs_fs_type);
	if (ret)
		goto init_register_err;

	ret = rcfs_init_inodecache();
	if (ret)
		goto init_cache_err;

	rcfs_fn = my_rcfs_fn ;
	
	return ret;

init_cache_err:
	unregister_filesystem(&rcfs_fs_type);
init_register_err:
	return ret;
}

static void __exit exit_rcfs_fs(void)
{
	rcfs_destroy_inodecache();
	unregister_filesystem(&rcfs_fs_type);
}

module_init(init_rcfs_fs)
module_exit(exit_rcfs_fs)

MODULE_LICENSE("GPL");
