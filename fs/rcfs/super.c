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



static kmem_cache_t *rcfs_inode_cachep;


inline struct rcfs_inode_info *RCFS_I(struct inode *inode)
{
	return container_of(inode, struct rcfs_inode_info, vfs_inode);
}


static struct inode *
rcfs_alloc_inode(struct super_block *sb)
{
	struct rcfs_inode_info *ri;
	ri = (struct rcfs_inode_info *) kmem_cache_alloc(rcfs_inode_cachep, SLAB_KERNEL);
	if (!ri)
		return NULL;
	return &ri->vfs_inode;
}

static void 
rcfs_destroy_inode(struct inode *inode)
{
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


/* exported operations */
struct super_operations rcfs_super_ops =
{
	.alloc_inode	= rcfs_alloc_inode,
	.destroy_inode	= rcfs_destroy_inode,
	.statfs		= simple_statfs,
	.drop_inode     = generic_delete_inode,
};



struct dentry *rcfs_rootde, *rcfs_nwde, *rcfs_nw_aqde;
struct inode *rcfs_root, *rcfs_nw, *rcfs_nw_aq;
struct rcfs_inode_info *rcfs_rootri, *rcfs_nwri;

static int rcfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	struct rcfs_inode_info *rootri;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = RCFS_MAGIC;
	sb->s_op = &rcfs_super_ops;
	inode = rcfs_get_inode(sb, S_IFDIR | 0755, 0);
	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;

	/* Link inode and core class */

	rootri = RCFS_I(inode);
	rootri->core = &ckrm_dflt_class;


	rcfs_root = inode;
	rcfs_rootde = root ;
	rcfs_rootri = rootri ;

	ckrm_dflt_class.dentry = rcfs_rootde;


	printk("get_alloc_super: root class created (%s, de-%p ri-%p in-%p ri->core-%p ri->core->in-%p",root->d_name.name, root, rootri, inode, rootri->core, ((struct ckrm_core_class *)(rootri->core))->dentry);
	
#ifdef CONFIG_CKRM_RES_SOCKETAQ

	// Currently both /rcfs/network and /rcfs/network/socket_aq are configured by 
	// the same option. 


	// Create the network root
	// XXX -- add error reporting
	rcfs_nwde = rcfs_create_internal(rcfs_rootde, "network", 
							rcfs_root->i_mode, 0);
	// Link inode and core class
	rootri = RCFS_I(rcfs_nwde->d_inode);
	rootri->core = &ckrm_net_root;
	ckrm_net_root.dentry = rcfs_nwde;

	// Pre-create other top level network directories
	// At present only the socket_aq direcotry.
	rcfs_nw_aqde = rcfs_create_internal(rcfs_nwde, "socket_aq", 
							rcfs_root->i_mode, 0);
	// Link inode and core class
	RCFS_I(rcfs_nw_aqde->d_inode)->core = 
		ckrm_alloc_core_class((ckrm_core_class_t *)&ckrm_net_root,
						rcfs_nw_aqde);

#endif

	return 0;
	
}

static struct super_block *rcfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_nodev(fs_type, flags, data, rcfs_fill_super);
}

static struct file_system_type rcfs_fs_type = {
	.name		= "rcfs",
	.get_sb		= rcfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init init_rcfs_fs(void)
{
	int ret;

	ret = register_filesystem(&rcfs_fs_type);
	if (ret)
		goto init_register_err;

	ret = rcfs_init_inodecache();
	if (ret)
		goto init_cache_err;
	
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


EXPORT_SYMBOL(RCFS_I);
MODULE_LICENSE("GPL");
