/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * The original JFFS, from which the design for JFFS2 was derived,
 * was designed and implemented by Axis Communications AB.
 *
 * The contents of this file are subject to the Red Hat eCos Public
 * License Version 1.1 (the "Licence"); you may not use this file
 * except in compliance with the Licence.  You may obtain a copy of
 * the Licence at http://www.redhat.com/
 *
 * Software distributed under the Licence is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 * See the Licence for the specific language governing rights and
 * limitations under the Licence.
 *
 * The Original Code is JFFS2 - Journalling Flash File System, version 2
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the RHEPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the RHEPL or the GPL.
 *
 * $Id: super.c,v 1.62 2002/03/12 16:23:41 dwmw2 Exp $
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/jffs2.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/interrupt.h>
#include "nodelist.h"

void jffs2_put_super (struct super_block *);


static kmem_cache_t *jffs2_inode_cachep;

static struct inode *jffs2_alloc_inode(struct super_block *sb)
{
	struct jffs2_inode_info *ei;
	ei = (struct jffs2_inode_info *)kmem_cache_alloc(jffs2_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void jffs2_destroy_inode(struct inode *inode)
{
	kmem_cache_free(jffs2_inode_cachep, JFFS2_INODE_INFO(inode));
}

static void jffs2_i_init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct jffs2_inode_info *ei = (struct jffs2_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		init_MUTEX(&ei->sem);
		inode_init_once(&ei->vfs_inode);
	}
}

static struct super_operations jffs2_super_operations =
{
	alloc_inode:	jffs2_alloc_inode,
	destroy_inode:	jffs2_destroy_inode,
	read_inode:	jffs2_read_inode,
	put_super:	jffs2_put_super,
	write_super:	jffs2_write_super,
	statfs:		jffs2_statfs,
	remount_fs:	jffs2_remount_fs,
	clear_inode:	jffs2_clear_inode
};


static int jffs2_blk_fill_super(struct super_block *sb, void *data, int silent)
{
	struct jffs2_sb_info *c;
	int ret;

	D1(printk(KERN_DEBUG "jffs2: blk_read_super for device %s\n", sb->s_id));

	if (major(sb->s_dev) != MTD_BLOCK_MAJOR) {
		if (!silent)
			printk(KERN_NOTICE "jffs2: attempt to mount non-MTD device %s\n",
			       sb->s_id);
		return -EINVAL;
	}

	c = JFFS2_SB_INFO(sb);
	memset(c, 0, sizeof(*c));
	
	sb->s_op = &jffs2_super_operations;

	c->mtd = get_mtd_device(NULL, minor(sb->s_dev));
	if (!c->mtd) {
		D1(printk(KERN_DEBUG "jffs2: MTD device #%u doesn't appear to exist\n", minor(sb->s_dev)));
		return -EINVAL;
	}

	ret = jffs2_do_fill_super(sb, data, silent);
	if (ret)
		put_mtd_device(c->mtd);

	return ret;
}

void jffs2_put_super (struct super_block *sb)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

	D2(printk(KERN_DEBUG "jffs2: jffs2_put_super()\n"));


	if (!(sb->s_flags & MS_RDONLY))
		jffs2_stop_garbage_collect_thread(c);
	jffs2_flush_wbuf(c, 1);
	jffs2_free_ino_caches(c);
	jffs2_free_raw_node_refs(c);
	kfree(c->blocks);
	if (c->mtd->sync)
		c->mtd->sync(c->mtd);
	put_mtd_device(c->mtd);
	
	D1(printk(KERN_DEBUG "jffs2_put_super returning\n"));
}

static struct super_block *jffs2_get_sb(struct file_system_type *fs_type,
					int flags, char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, jffs2_blk_fill_super);
}
 
static struct file_system_type jffs2_fs_type = {
	owner:		THIS_MODULE,
	name:		"jffs2",
	get_sb:		jffs2_get_sb,
	kill_sb:	kill_block_super,
	fs_flags:	FS_REQUIRES_DEV,
};



static int __init init_jffs2_fs(void)
{
	int ret;

	printk(KERN_INFO "JFFS2 version 2.1. (C) 2001, 2002 Red Hat, Inc.\n");

	jffs2_inode_cachep = kmem_cache_create("jffs2_i",
					     sizeof(struct jffs2_inode_info),
					     0, SLAB_HWCACHE_ALIGN,
					     jffs2_i_init_once, NULL);
	if (!jffs2_inode_cachep) {
		printk(KERN_ERR "JFFS2 error: Failed to initialise inode cache\n");
		return -ENOMEM;
	}
	ret = jffs2_zlib_init();
	if (ret) {
		printk(KERN_ERR "JFFS2 error: Failed to initialise zlib workspaces\n");
		return ret;
	}
	ret = jffs2_create_slab_caches();
	if (ret) {
		printk(KERN_ERR "JFFS2 error: Failed to initialise slab caches\n");
		return ret;
	}
	ret = register_filesystem(&jffs2_fs_type);
	if (ret) {
		printk(KERN_ERR "JFFS2 error: Failed to register filesystem\n");
		jffs2_destroy_slab_caches();
	}
	return ret;
}

static void __exit exit_jffs2_fs(void)
{
	unregister_filesystem(&jffs2_fs_type);
	jffs2_destroy_slab_caches();
	jffs2_zlib_exit();
	kmem_cache_destroy(jffs2_inode_cachep);
}

module_init(init_jffs2_fs);
module_exit(exit_jffs2_fs);

MODULE_DESCRIPTION("The Journalling Flash File System, v2");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL"); // Actually dual-licensed, but it doesn't matter for 
		       // the sake of this tag. It's Free Software.
