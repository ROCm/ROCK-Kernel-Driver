/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: readinode.c,v 1.95 2002/11/12 11:17:29 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/interrupt.h>
#include "nodelist.h"


D1(static void jffs2_print_fragtree(struct rb_root *list, int permitbug)
{
	struct jffs2_node_frag *this = frag_first(list);
	uint32_t lastofs = 0;
	int buggy = 0;

	while(this) {
		if (this->node)
			printk(KERN_DEBUG "frag %04x-%04x: 0x%08x(%d) on flash (*%p). left (%p), right (%p), parent (%p)\n",
			       this->ofs, this->ofs+this->size, ref_offset(this->node->raw), ref_flags(this->node->raw),
			       this, frag_left(this), frag_right(this), frag_parent(this));
		else 
			printk(KERN_DEBUG "frag %04x-%04x: hole (*%p). left (%p} right (%p), parent (%p)\n", this->ofs, 
			       this->ofs+this->size, this, frag_left(this), frag_right(this), frag_parent(this));
		if (this->ofs != lastofs)
			buggy = 1;
		lastofs = this->ofs+this->size;
		this = frag_next(this);
	}
	if (buggy && !permitbug) {
		printk(KERN_CRIT "Frag tree got a hole in it\n");
		BUG();
	}
})

D1(void jffs2_print_frag_list(struct jffs2_inode_info *f)
{
	jffs2_print_fragtree(&f->fragtree, 0);

	if (f->metadata) {
		printk(KERN_DEBUG "metadata at 0x%08x\n", ref_offset(f->metadata->raw));
	}
})


/* Given an inode, probably with existing list of fragments, add the new node
 * to the fragment list.
 */
int jffs2_add_full_dnode_to_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_full_dnode *fn)
{
	int ret;
	D1(printk(KERN_DEBUG "jffs2_add_full_dnode_to_inode(ino #%u, f %p, fn %p)\n", f->inocache->ino, f, fn));

	ret = jffs2_add_full_dnode_to_fraglist(c, &f->fragtree, fn);

	D2(jffs2_print_frag_list(f));
	return ret;
}

static void jffs2_obsolete_node_frag(struct jffs2_sb_info *c, struct jffs2_node_frag *this)
{
	if (this->node) {
		this->node->frags--;
		if (!this->node->frags) {
			/* The node has no valid frags left. It's totally obsoleted */
			D2(printk(KERN_DEBUG "Marking old node @0x%08x (0x%04x-0x%04x) obsolete\n",
				  ref_offset(this->node->raw), this->node->ofs, this->node->ofs+this->node->size));
			jffs2_mark_node_obsolete(c, this->node->raw);
			jffs2_free_full_dnode(this->node);
		} else {
			D2(printk(KERN_DEBUG "Marking old node @0x%08x (0x%04x-0x%04x) REF_NORMAL. frags is %d\n",
				  ref_offset(this->node->raw), this->node->ofs, this->node->ofs+this->node->size,
				  this->node->frags));
			mark_ref_normal(this->node->raw);
		}
		
	}
	jffs2_free_node_frag(this);
}

/* Doesn't set inode->i_size */
int jffs2_add_full_dnode_to_fraglist(struct jffs2_sb_info *c, struct rb_root *list, struct jffs2_full_dnode *fn)
{
	struct jffs2_node_frag *this;
	struct jffs2_node_frag *newfrag;
	uint32_t lastend;

	newfrag = jffs2_alloc_node_frag();
	if (!newfrag) {
		return -ENOMEM;
	}

	if (!fn->raw) {
		printk(KERN_WARNING "dwmw2 is stupid. j_a_f_d_t_f should never happen with ->raw == NULL\n");
		BUG();
	}

	D2(printk(KERN_DEBUG "adding node %04x-%04x @0x%08x on flash, newfrag *%p\n", fn->ofs, fn->ofs+fn->size, ref_offset(fn->raw), newfrag));
	
	if (!fn->size) {
		jffs2_free_node_frag(newfrag);
		return 0;
	}

	newfrag->ofs = fn->ofs;
	newfrag->size = fn->size;
	newfrag->node = fn;
	newfrag->node->frags = 1;

	/* Skip all the nodes which are completed before this one starts */
	this = jffs2_lookup_node_frag(list, fn->ofs);

	if (this) {
		D2(printk(KERN_DEBUG "j_a_f_d_t_f: Lookup gave frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n",
			  this->ofs, this->ofs+this->size, this->node?(ref_offset(this->node->raw)):0xffffffff, this));
		lastend = this->ofs + this->size;
	} else {
		D2(printk(KERN_DEBUG "j_a_f_d_t_f: Lookup gave no frag\n"));
		lastend = 0;
	}
			  
	/* See if we ran off the end of the list */
	if (lastend <= newfrag->ofs) {
		/* We did */

		/* Check if 'this' node was on the same page as the new node.
		   If so, both 'this' and the new node get marked REF_NORMAL so
		   the GC can take a look.
		*/
		if ((lastend-1) >> PAGE_CACHE_SHIFT == newfrag->ofs >> PAGE_CACHE_SHIFT) {
			if (this->node)
				mark_ref_normal(this->node->raw);
			mark_ref_normal(fn->raw);
		}

		if (lastend < fn->ofs) {
			/* ... and we need to put a hole in before the new node */
			struct jffs2_node_frag *holefrag = jffs2_alloc_node_frag();
			if (!holefrag)
				return -ENOMEM;
			holefrag->ofs = lastend;
			holefrag->size = fn->ofs - lastend;
			holefrag->node = NULL;
			if (this) {
				/* By definition, the 'this' node has no right-hand child, 
				   because there are no frags with offset greater than it.
				   So that's where we want to put the hole */
				D2(printk(KERN_DEBUG "Adding hole frag (%p) on right of node at (%p)\n", holefrag, this));
				rb_link_node(&holefrag->rb, &this->rb, &this->rb.rb_right);
			} else {
				D2(printk(KERN_DEBUG "Adding hole frag (%p) at root of tree\n", holefrag));
				rb_link_node(&holefrag->rb, NULL, &list->rb_node);
			}
			rb_insert_color(&holefrag->rb, list);
			this = holefrag;
		}
		if (this) {
			/* By definition, the 'this' node has no right-hand child, 
			   because there are no frags with offset greater than it.
			   So that's where we want to put the hole */
			D2(printk(KERN_DEBUG "Adding new frag (%p) on right of node at (%p)\n", newfrag, this));
			rb_link_node(&newfrag->rb, &this->rb, &this->rb.rb_right);			
		} else {
			D2(printk(KERN_DEBUG "Adding new frag (%p) at root of tree\n", newfrag));
			rb_link_node(&newfrag->rb, NULL, &list->rb_node);
		}
		rb_insert_color(&newfrag->rb, list);
		return 0;
	}

	D2(printk(KERN_DEBUG "j_a_f_d_t_f: dealing with frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n", 
		  this->ofs, this->ofs+this->size, this->node?(ref_offset(this->node->raw)):0xffffffff, this));

	/* OK. 'this' is pointing at the first frag that newfrag->ofs at least partially obsoletes,
	 * - i.e. newfrag->ofs < this->ofs+this->size && newfrag->ofs >= this->ofs  
	 */
	if (newfrag->ofs > this->ofs) {
		/* This node isn't completely obsoleted. The start of it remains valid */

		/* Mark the new node and the partially covered node REF_NORMAL -- let 
		   the GC take a look at them */
		mark_ref_normal(fn->raw);
		if (this->node)
			mark_ref_normal(this->node->raw);

		if (this->ofs + this->size > newfrag->ofs + newfrag->size) {
			/* The new node splits 'this' frag into two */
			struct jffs2_node_frag *newfrag2 = jffs2_alloc_node_frag();
			if (!newfrag2) {
				jffs2_free_node_frag(newfrag);
				return -ENOMEM;
			}
			D2(printk(KERN_DEBUG "split old frag 0x%04x-0x%04x -->", this->ofs, this->ofs+this->size);
			if (this->node)
				printk("phys 0x%08x\n", ref_offset(this->node->raw));
			else 
				printk("hole\n");
			   )
			
			/* New second frag pointing to this's node */
			newfrag2->ofs = newfrag->ofs + newfrag->size;
			newfrag2->size = (this->ofs+this->size) - newfrag2->ofs;
			newfrag2->node = this->node;
			if (this->node)
				this->node->frags++;

			/* Adjust size of original 'this' */
			this->size = newfrag->ofs - this->ofs;

			/* Now, we know there's no node with offset
			   greater than this->ofs but smaller than
			   newfrag2->ofs or newfrag->ofs, for obvious
			   reasons. So we can do a tree insert from
			   'this' to insert newfrag, and a tree insert
			   from newfrag to insert newfrag2. */
			jffs2_fragtree_insert(newfrag, this);
			rb_insert_color(&newfrag->rb, list);
			
			jffs2_fragtree_insert(newfrag2, newfrag);
			rb_insert_color(&newfrag2->rb, list);
			
			return 0;
		}
		/* New node just reduces 'this' frag in size, doesn't split it */
		this->size = newfrag->ofs - this->ofs;

		/* Again, we know it lives down here in the tree */
		jffs2_fragtree_insert(newfrag, this);
		rb_insert_color(&newfrag->rb, list);
	} else {
		/* New frag starts at the same point as 'this' used to. Replace 
		   it in the tree without doing a delete and insertion */
		D2(printk(KERN_DEBUG "Inserting newfrag (*%p),%d-%d in before 'this' (*%p),%d-%d\n",
			  newfrag, newfrag->ofs, newfrag->ofs+newfrag->size,
			  this, this->ofs, this->ofs+this->size));
	
		rb_replace_node(&this->rb, &newfrag->rb, list);
		
		if (newfrag->ofs + newfrag->size >= this->ofs+this->size) {
			D2(printk(KERN_DEBUG "Obsoleting node frag %p (%x-%x)\n", this, this->ofs, this->ofs+this->size));
			jffs2_obsolete_node_frag(c, this);
		} else {
			this->ofs += newfrag->size;
			this->size -= newfrag->size;

			jffs2_fragtree_insert(this, newfrag);
			rb_insert_color(&this->rb, list);
			return 0;
		}
	}
	/* OK, now we have newfrag added in the correct place in the tree, but
	   frag_next(newfrag) may be a fragment which is overlapped by it 
	*/
	while ((this = frag_next(newfrag)) && newfrag->ofs + newfrag->size >= this->ofs + this->size) {
		/* 'this' frag is obsoleted completely. */
		D2(printk(KERN_DEBUG "Obsoleting node frag %p (%x-%x) and removing from tree\n", this, this->ofs, this->ofs+this->size));
		rb_erase(&this->rb, list);
		jffs2_obsolete_node_frag(c, this);
	}
	/* Now we're pointing at the first frag which isn't totally obsoleted by 
	   the new frag */

	if (!this || newfrag->ofs + newfrag->size == this->ofs) {
		return 0;
	}
	/* Still some overlap but we don't need to move it in the tree */
	this->size = (this->ofs + this->size) - (newfrag->ofs + newfrag->size);
	this->ofs = newfrag->ofs + newfrag->size;

	/* And mark them REF_NORMAL so the GC takes a look at them */
	if (this->node)
		mark_ref_normal(this->node->raw);
	mark_ref_normal(fn->raw);

	return 0;
}

void jffs2_truncate_fraglist (struct jffs2_sb_info *c, struct rb_root *list, uint32_t size)
{
	struct jffs2_node_frag *frag = jffs2_lookup_node_frag(list, size);

	D1(printk(KERN_DEBUG "Truncating fraglist to 0x%08x bytes\n", size));

	/* We know frag->ofs <= size. That's what lookup does for us */
	if (frag && frag->ofs != size) {
		if (frag->ofs+frag->size >= size) {
			D1(printk(KERN_DEBUG "Truncating frag 0x%08x-0x%08x\n", frag->ofs, frag->ofs+frag->size));
			frag->size = size - frag->ofs;
		}
		frag = frag_next(frag);
	}
	while (frag && frag->ofs >= size) {
		struct jffs2_node_frag *next = frag_next(frag);

		D1(printk(KERN_DEBUG "Removing frag 0x%08x-0x%08x\n", frag->ofs, frag->ofs+frag->size));
		frag_erase(frag, list);
		jffs2_obsolete_node_frag(c, frag);
		frag = next;
	}
}

/* Scan the list of all nodes present for this ino, build map of versions, etc. */

int jffs2_do_read_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, 
			uint32_t ino, struct jffs2_raw_inode *latest_node)
{
	struct jffs2_tmp_dnode_info *tn_list, *tn;
	struct jffs2_full_dirent *fd_list;
	struct jffs2_full_dnode *fn = NULL;
	uint32_t crc;
	uint32_t latest_mctime, mctime_ver;
	uint32_t mdata_ver = 0;
	size_t retlen;
	int ret;

	D2(printk(KERN_DEBUG "jffs2_do_read_inode(): getting inocache\n"));

	f->inocache = jffs2_get_ino_cache(c, ino);

	D2(printk(KERN_DEBUG "jffs2_do_read_inode(): Got inocache at %p\n", f->inocache));

	if (!f->inocache && ino == 1) {
		/* Special case - no root inode on medium */
		f->inocache = jffs2_alloc_inode_cache();
		if (!f->inocache) {
			printk(KERN_CRIT "jffs2_do_read_inode(): Cannot allocate inocache for root inode\n");
			return -ENOMEM;
		}
		D1(printk(KERN_DEBUG "jffs2_do_read_inode(): Creating inocache for root inode\n"));
		memset(f->inocache, 0, sizeof(struct jffs2_inode_cache));
		f->inocache->ino = f->inocache->nlink = 1;
		f->inocache->nodes = (struct jffs2_raw_node_ref *)f->inocache;
		jffs2_add_ino_cache(c, f->inocache);
	}
	if (!f->inocache) {
		printk(KERN_WARNING "jffs2_do_read_inode() on nonexistent ino %u\n", ino);
		return -ENOENT;
	}
	D1(printk(KERN_DEBUG "jffs2_do_read_inode(): ino #%u nlink is %d\n", ino, f->inocache->nlink));

	/* Grab all nodes relevant to this ino */
	ret = jffs2_get_inode_nodes(c, ino, f, &tn_list, &fd_list, &f->highest_version, &latest_mctime, &mctime_ver);

	if (ret) {
		printk(KERN_CRIT "jffs2_get_inode_nodes() for ino %u returned %d\n", ino, ret);
		return ret;
	}
	f->dents = fd_list;

	while (tn_list) {
		tn = tn_list;

		fn = tn->fn;

		if (f->metadata && tn->version > mdata_ver) {
			D1(printk(KERN_DEBUG "Obsoleting old metadata at 0x%08x\n", ref_offset(f->metadata->raw)));
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
			
			mdata_ver = 0;
		}

		if (fn->size) {
			jffs2_add_full_dnode_to_inode(c, f, fn);
		} else {
			/* Zero-sized node at end of version list. Just a metadata update */
			D1(printk(KERN_DEBUG "metadata @%08x: ver %d\n", ref_offset(fn->raw), tn->version));
			f->metadata = fn;
			mdata_ver = tn->version;
		}
		tn_list = tn->next;
		jffs2_free_tmp_dnode_info(tn);
	}
	if (!fn) {
		/* No data nodes for this inode. */
		if (ino != 1) {
			printk(KERN_WARNING "jffs2_do_read_inode(): No data nodes found for ino #%u\n", ino);
			if (!fd_list) {
				return -EIO;
			}
			printk(KERN_WARNING "jffs2_do_read_inode(): But it has children so we fake some modes for it\n");
		}
		latest_node->mode = cpu_to_je32(S_IFDIR|S_IRUGO|S_IWUSR|S_IXUGO);
		latest_node->version = cpu_to_je32(0);
		latest_node->atime = latest_node->ctime = latest_node->mtime = cpu_to_je32(0);
		latest_node->isize = cpu_to_je32(0);
		latest_node->gid = cpu_to_je16(0);
		latest_node->uid = cpu_to_je16(0);
		return 0;
	}

	ret = jffs2_flash_read(c, ref_offset(fn->raw), sizeof(*latest_node), &retlen, (void *)latest_node);
	if (ret || retlen != sizeof(*latest_node)) {
		printk(KERN_NOTICE "MTD read in jffs2_do_read_inode() failed: Returned %d, %ld of %d bytes read\n",
		       ret, (long)retlen, sizeof(*latest_node));
		/* FIXME: If this fails, there seems to be a memory leak. Find it. */
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
		return ret?ret:-EIO;
	}

	crc = crc32(0, latest_node, sizeof(*latest_node)-8);
	if (crc != je32_to_cpu(latest_node->node_crc)) {
		printk(KERN_NOTICE "CRC failed for read_inode of inode %u at physical location 0x%x\n", ino, ref_offset(fn->raw));
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
		return -EIO;
	}

	switch(je32_to_cpu(latest_node->mode) & S_IFMT) {
	case S_IFDIR:
		if (mctime_ver > je32_to_cpu(latest_node->version)) {
			/* The times in the latest_node are actually older than
			   mctime in the latest dirent. Cheat. */
			latest_node->ctime = latest_node->mtime = cpu_to_je32(latest_mctime);
		}
		break;

			
	case S_IFREG:
		/* If it was a regular file, truncate it to the latest node's isize */
		jffs2_truncate_fraglist(c, &f->fragtree, je32_to_cpu(latest_node->isize));
		break;

	case S_IFLNK:
		/* Hack to work around broken isize in old symlink code.
		   Remove this when dwmw2 comes to his senses and stops
		   symlinks from being an entirely gratuitous special
		   case. */
		if (!je32_to_cpu(latest_node->isize))
			latest_node->isize = latest_node->dsize;
		/* fall through... */

	case S_IFBLK:
	case S_IFCHR:
		/* Xertain inode types should have only one data node, and it's
		   kept as the metadata node */
		if (f->metadata) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o had metadata node\n", ino, je32_to_cpu(latest_node->mode));
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		if (!frag_first(&f->fragtree)) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o has no fragments\n", ino, je32_to_cpu(latest_node->mode));
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* ASSERT: f->fraglist != NULL */
		if (frag_next(frag_first(&f->fragtree))) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o had more than one node\n", ino, je32_to_cpu(latest_node->mode));
			/* FIXME: Deal with it - check crc32, check for duplicate node, check times and discard the older one */
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* OK. We're happy */
		f->metadata = frag_first(&f->fragtree)->node;
		jffs2_free_node_frag(frag_first(&f->fragtree));
		f->fragtree = RB_ROOT;
		break;
	}
	f->inocache->state = INO_STATE_PRESENT;

	return 0;
}

void jffs2_do_clear_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f)
{
	struct jffs2_full_dirent *fd, *fds;
	/* I don't think we care about the potential race due to reading this
	   without f->sem. It can never get undeleted. */
	int deleted = f->inocache && !f->inocache->nlink;

	/* If it's a deleted inode, grab the alloc_sem. This prevents
	   jffs2_garbage_collect_pass() from deciding that it wants to
	   garbage collect one of the nodes we're just about to mark 
	   obsolete -- by the time we drop alloc_sem and return, all
	   the nodes are marked obsolete, and jffs2_g_c_pass() won't
	   call iget() for the inode in question.

	   We also do this to keep the (maybe temporary) BUG() in 
	   jffs2_mark_node_obsolete() from triggering. 
	*/
	if(deleted)
		down(&c->alloc_sem);

	down(&f->sem);

	if (f->metadata) {
		if (deleted)
			jffs2_mark_node_obsolete(c, f->metadata->raw);
		jffs2_free_full_dnode(f->metadata);
	}

	jffs2_kill_fragtree(&f->fragtree, deleted?c:NULL);

	fds = f->dents;

	while(fds) {
		fd = fds;
		fds = fd->next;
		jffs2_free_full_dirent(fd);
	}

	if (f->inocache)
		f->inocache->state = INO_STATE_CHECKEDABSENT;

	up(&f->sem);

	if(deleted)
		up(&c->alloc_sem);
}
