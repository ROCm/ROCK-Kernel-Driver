/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: build.c,v 1.46 2003/04/29 17:12:26 gleixner Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "nodelist.h"

int jffs2_build_inode_pass1(struct jffs2_sb_info *, struct jffs2_inode_cache *);
int jffs2_build_remove_unlinked_inode(struct jffs2_sb_info *, struct jffs2_inode_cache *);

static inline struct jffs2_inode_cache *
first_inode_chain(int *i, struct jffs2_sb_info *c)
{
	for (; *i < INOCACHE_HASHSIZE; (*i)++) {
		if (c->inocache_list[*i])
			return c->inocache_list[*i];
	}
	return NULL;
}

static inline struct jffs2_inode_cache *
next_inode(int *i, struct jffs2_inode_cache *ic, struct jffs2_sb_info *c)
{
	/* More in this chain? */
	if (ic->next)
		return ic->next;
	(*i)++;
	return first_inode_chain(i, c);
}

#define for_each_inode(i, c, ic)			\
	for (i = 0, ic = first_inode_chain(&i, (c));	\
	     ic;					\
	     ic = next_inode(&i, ic, (c)))

/* Scan plan:
 - Scan physical nodes. Build map of inodes/dirents. Allocate inocaches as we go
 - Scan directory tree from top down, setting nlink in inocaches
 - Scan inocaches for inodes with nlink==0
*/
static int jffs2_build_filesystem(struct jffs2_sb_info *c)
{
	int ret;
	int i;
	struct jffs2_inode_cache *ic;

	/* First, scan the medium and build all the inode caches with
	   lists of physical nodes */

	c->flags |= JFFS2_SB_FLAG_MOUNTING;
	ret = jffs2_scan_medium(c);
	c->flags &= ~JFFS2_SB_FLAG_MOUNTING;

	if (ret)
		return ret;

	D1(printk(KERN_DEBUG "Scanned flash completely\n"));
	D1(jffs2_dump_block_lists(c));

	/* Now scan the directory tree, increasing nlink according to every dirent found. */
	for_each_inode(i, c, ic) {
		D1(printk(KERN_DEBUG "Pass 1: ino #%u\n", ic->ino));
		ret = jffs2_build_inode_pass1(c, ic);
		if (ret) {
			D1(printk(KERN_WARNING "Eep. jffs2_build_inode_pass1 for ino %d returned %d\n", ic->ino, ret));
			return ret;
		}
		cond_resched();
	}
	D1(printk(KERN_DEBUG "Pass 1 complete\n"));
	D1(jffs2_dump_block_lists(c));

	/* Next, scan for inodes with nlink == 0 and remove them. If
	   they were directories, then decrement the nlink of their
	   children too, and repeat the scan. As that's going to be
	   a fairly uncommon occurrence, it's not so evil to do it this
	   way. Recursion bad. */
	do { 
		D1(printk(KERN_DEBUG "Pass 2 (re)starting\n"));
		ret = 0;
		for_each_inode(i, c, ic) {
			D1(printk(KERN_DEBUG "Pass 2: ino #%u, nlink %d, ic %p, nodes %p\n", ic->ino, ic->nlink, ic, ic->nodes));
			if (ic->nlink)
				continue;
			
			/* XXX: Can get high latency here. Move the cond_resched() from the end of the loop? */

			ret = jffs2_build_remove_unlinked_inode(c, ic);
			if (ret)
				break;
		/* -EAGAIN means the inode's nlink was zero, so we deleted it,
		   and furthermore that it had children and their nlink has now
		   gone to zero too. So we have to restart the scan. */
		} 
		D1(jffs2_dump_block_lists(c));

		cond_resched();
	
	} while(ret == -EAGAIN);

	D1(printk(KERN_DEBUG "Pass 2 complete\n"));
	
	/* Finally, we can scan again and free the dirent nodes and scan_info structs */
	for_each_inode(i, c, ic) {
		struct jffs2_full_dirent *fd;
		D1(printk(KERN_DEBUG "Pass 3: ino #%u, ic %p, nodes %p\n", ic->ino, ic, ic->nodes));

		while(ic->scan_dents) {
			fd = ic->scan_dents;
			ic->scan_dents = fd->next;
			jffs2_free_full_dirent(fd);
		}
		ic->scan_dents = NULL;
		cond_resched();
	}
	D1(printk(KERN_DEBUG "Pass 3 complete\n"));
	D1(jffs2_dump_block_lists(c));

	/* Rotate the lists by some number to ensure wear levelling */
	jffs2_rotate_lists(c);

	return ret;
}

int jffs2_build_inode_pass1(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic)
{
	struct jffs2_full_dirent *fd;

	D1(printk(KERN_DEBUG "jffs2_build_inode building inode #%u\n", ic->ino));

	if (ic->ino > c->highest_ino)
		c->highest_ino = ic->ino;

	/* For each child, increase nlink */
	for(fd=ic->scan_dents; fd; fd = fd->next) {
		struct jffs2_inode_cache *child_ic;
		if (!fd->ino)
			continue;
		
		/* XXX: Can get high latency here with huge directories */

		child_ic = jffs2_get_ino_cache(c, fd->ino);
		if (!child_ic) {
			printk(KERN_NOTICE "Eep. Child \"%s\" (ino #%u) of dir ino #%u doesn't exist!\n",
				  fd->name, fd->ino, ic->ino);
			continue;
		}

		if (child_ic->nlink++ && fd->type == DT_DIR) {
			printk(KERN_NOTICE "Child dir \"%s\" (ino #%u) of dir ino #%u appears to be a hard link\n", fd->name, fd->ino, ic->ino);
			if (fd->ino == 1 && ic->ino == 1) {
				printk(KERN_NOTICE "This is mostly harmless, and probably caused by creating a JFFS2 image\n");
				printk(KERN_NOTICE "using a buggy version of mkfs.jffs2. Use at least v1.17.\n");
			}
			/* What do we do about it? */
		}
		D1(printk(KERN_DEBUG "Increased nlink for child \"%s\" (ino #%u)\n", fd->name, fd->ino));
		/* Can't free them. We might need them in pass 2 */
	}
	return 0;
}

int jffs2_build_remove_unlinked_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic)
{
	struct jffs2_raw_node_ref *raw;
	struct jffs2_full_dirent *fd;
	int ret = 0;

	D1(printk(KERN_DEBUG "JFFS2: Removing ino #%u with nlink == zero.\n", ic->ino));
	
	for (raw = ic->nodes; raw != (void *)ic; raw = raw->next_in_ino) {
		D1(printk(KERN_DEBUG "obsoleting node at 0x%08x\n", ref_offset(raw)));
		jffs2_mark_node_obsolete(c, raw);
	}

	if (ic->scan_dents) {
		int whinged = 0;
		D1(printk(KERN_DEBUG "Inode #%u was a directory which may have children...\n", ic->ino));

		while(ic->scan_dents) {
			struct jffs2_inode_cache *child_ic;

			fd = ic->scan_dents;
			ic->scan_dents = fd->next;

			if (!fd->ino) {
				/* It's a deletion dirent. Ignore it */
				D1(printk(KERN_DEBUG "Child \"%s\" is a deletion dirent, skipping...\n", fd->name));
				jffs2_free_full_dirent(fd);
				continue;
			}
			if (!whinged) {
				whinged = 1;
				printk(KERN_NOTICE "Inode #%u was a directory with children - removing those too...\n", ic->ino);
			}

			D1(printk(KERN_DEBUG "Removing child \"%s\", ino #%u\n",
				  fd->name, fd->ino));
			
			child_ic = jffs2_get_ino_cache(c, fd->ino);
			if (!child_ic) {
				printk(KERN_NOTICE "Cannot remove child \"%s\", ino #%u, because it doesn't exist\n", fd->name, fd->ino);
				jffs2_free_full_dirent(fd);
				continue;
			}
			jffs2_free_full_dirent(fd);
			child_ic->nlink--;
		}
		ret = -EAGAIN;
	}

	/*
	   We don't delete the inocache from the hash list and free it yet. 
	   The erase code will do that, when all the nodes are completely gone.
	*/

	return ret;
}

int jffs2_do_mount_fs(struct jffs2_sb_info *c)
{
	int i;

	c->free_size = c->flash_size;
	c->nr_blocks = c->flash_size / c->sector_size;
	c->blocks = kmalloc(sizeof(struct jffs2_eraseblock) * c->nr_blocks, GFP_KERNEL);
	if (!c->blocks)
		return -ENOMEM;
	for (i=0; i<c->nr_blocks; i++) {
		INIT_LIST_HEAD(&c->blocks[i].list);
		c->blocks[i].offset = i * c->sector_size;
		c->blocks[i].free_size = c->sector_size;
		c->blocks[i].dirty_size = 0;
		c->blocks[i].wasted_size = 0;
		c->blocks[i].unchecked_size = 0;
		c->blocks[i].used_size = 0;
		c->blocks[i].first_node = NULL;
		c->blocks[i].last_node = NULL;
	}

	init_MUTEX(&c->alloc_sem);
	init_MUTEX(&c->erase_free_sem);
	init_waitqueue_head(&c->erase_wait);
	init_waitqueue_head(&c->inocache_wq);
	spin_lock_init(&c->erase_completion_lock);
	spin_lock_init(&c->inocache_lock);

	INIT_LIST_HEAD(&c->clean_list);
	INIT_LIST_HEAD(&c->very_dirty_list);
	INIT_LIST_HEAD(&c->dirty_list);
	INIT_LIST_HEAD(&c->erasable_list);
	INIT_LIST_HEAD(&c->erasing_list);
	INIT_LIST_HEAD(&c->erase_pending_list);
	INIT_LIST_HEAD(&c->erasable_pending_wbuf_list);
	INIT_LIST_HEAD(&c->erase_complete_list);
	INIT_LIST_HEAD(&c->free_list);
	INIT_LIST_HEAD(&c->bad_list);
	INIT_LIST_HEAD(&c->bad_used_list);
	c->highest_ino = 1;

	if (jffs2_build_filesystem(c)) {
		D1(printk(KERN_DEBUG "build_fs failed\n"));
		jffs2_free_ino_caches(c);
		jffs2_free_raw_node_refs(c);
		kfree(c->blocks);
		return -EIO;
	}
	return 0;
}
