/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: readinode.c,v 1.71 2002/03/06 12:25:59 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/mtd/mtd.h>
#include <linux/interrupt.h>
#include "nodelist.h"


D1(void jffs2_print_frag_list(struct jffs2_inode_info *f)
{
	struct jffs2_node_frag *this = f->fraglist;

	while(this) {
		if (this->node)
			printk(KERN_DEBUG "frag %04x-%04x: 0x%08x on flash (*%p->%p)\n", this->ofs, this->ofs+this->size, this->node->raw->flash_offset &~3, this, this->next);
		else 
			printk(KERN_DEBUG "frag %04x-%04x: hole (*%p->%p)\n", this->ofs, this->ofs+this->size, this, this->next);
		this = this->next;
	}
	if (f->metadata) {
		printk(KERN_DEBUG "metadata at 0x%08x\n", f->metadata->raw->flash_offset &~3);
	}
})


/* Given an inode, probably with existing list of fragments, add the new node
 * to the fragment list.
 */
int jffs2_add_full_dnode_to_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_full_dnode *fn)
{
	int ret;
	D1(printk(KERN_DEBUG "jffs2_add_full_dnode_to_inode(ino #%u, f %p, fn %p)\n", f->inocache->ino, f, fn));

	ret = jffs2_add_full_dnode_to_fraglist(c, &f->fraglist, fn);

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
				  this->node->raw->flash_offset &~3, this->node->ofs, this->node->ofs+this->node->size));
			jffs2_mark_node_obsolete(c, this->node->raw);
			jffs2_free_full_dnode(this->node);
		} else {
			D2(printk(KERN_DEBUG "Not marking old node @0x%08x (0x%04x-0x%04x) obsolete. frags is %d\n",
				  this->node->raw->flash_offset &~3, this->node->ofs, this->node->ofs+this->node->size,
				  this->node->frags));
		}
		
	}
	jffs2_free_node_frag(this);
}

/* Doesn't set inode->i_size */
int jffs2_add_full_dnode_to_fraglist(struct jffs2_sb_info *c, struct jffs2_node_frag **list, struct jffs2_full_dnode *fn)
{
	
	struct jffs2_node_frag *this, **prev, *old;
	struct jffs2_node_frag *newfrag, *newfrag2;
	uint32_t lastend = 0;


	newfrag = jffs2_alloc_node_frag();
	if (!newfrag) {
		return -ENOMEM;
	}

	D2(if (fn->raw)
		printk(KERN_DEBUG "adding node %04x-%04x @0x%08x on flash, newfrag *%p\n", fn->ofs, fn->ofs+fn->size, fn->raw->flash_offset &~3, newfrag);
	else
		printk(KERN_DEBUG "adding hole node %04x-%04x on flash, newfrag *%p\n", fn->ofs, fn->ofs+fn->size, newfrag));
	
	prev = list;
	this = *list;

	if (!fn->size) {
		jffs2_free_node_frag(newfrag);
		return 0;
	}

	newfrag->ofs = fn->ofs;
	newfrag->size = fn->size;
	newfrag->node = fn;
	newfrag->node->frags = 1;
	newfrag->next = (void *)0xdeadbeef;

	/* Skip all the nodes which are completed before this one starts */
	while(this && fn->ofs >= this->ofs+this->size) {
		lastend = this->ofs + this->size;

		D2(printk(KERN_DEBUG "j_a_f_d_t_f: skipping frag 0x%04x-0x%04x; phys 0x%08x (*%p->%p)\n", 
			  this->ofs, this->ofs+this->size, this->node?(this->node->raw->flash_offset &~3):0xffffffff, this, this->next));
		prev = &this->next;
		this = this->next;
	}

	/* See if we ran off the end of the list */
	if (!this) {
		/* We did */
		if (lastend < fn->ofs) {
			/* ... and we need to put a hole in before the new node */
			struct jffs2_node_frag *holefrag = jffs2_alloc_node_frag();
			if (!holefrag)
				return -ENOMEM;
			holefrag->ofs = lastend;
			holefrag->size = fn->ofs - lastend;
			holefrag->next = NULL;
			holefrag->node = NULL;
			*prev = holefrag;
			prev = &holefrag->next;
		}
		newfrag->next = NULL;
		*prev = newfrag;
		return 0;
	}

	D2(printk(KERN_DEBUG "j_a_f_d_t_f: dealing with frag 0x%04x-0x%04x; phys 0x%08x (*%p->%p)\n", 
		  this->ofs, this->ofs+this->size, this->node?(this->node->raw->flash_offset &~3):0xffffffff, this, this->next));

	/* OK. 'this' is pointing at the first frag that fn->ofs at least partially obsoletes,
	 * - i.e. fn->ofs < this->ofs+this->size && fn->ofs >= this->ofs  
	 */
	if (fn->ofs > this->ofs) {
		/* This node isn't completely obsoleted. The start of it remains valid */
		if (this->ofs + this->size > fn->ofs + fn->size) {
			/* The new node splits 'this' frag into two */
			newfrag2 = jffs2_alloc_node_frag();
			if (!newfrag2) {
				jffs2_free_node_frag(newfrag);
				return -ENOMEM;
			}
			D1(printk(KERN_DEBUG "split old frag 0x%04x-0x%04x -->", this->ofs, this->ofs+this->size);
			if (this->node)
				printk("phys 0x%08x\n", this->node->raw->flash_offset &~3);
			else 
				printk("hole\n");
			   )
			newfrag2->ofs = fn->ofs + fn->size;
			newfrag2->size = (this->ofs+this->size) - newfrag2->ofs;
			newfrag2->next = this->next;
			newfrag2->node = this->node;
			if (this->node)
				this->node->frags++;
			newfrag->next = newfrag2;
			this->next = newfrag;
			this->size = newfrag->ofs - this->ofs;
			return 0;
		}
		/* New node just reduces 'this' frag in size, doesn't split it */
		this->size = fn->ofs - this->ofs;
		newfrag->next = this->next;
		this->next = newfrag;
		this = newfrag->next;
	} else {
		D2(printk(KERN_DEBUG "Inserting newfrag (*%p) in before 'this' (*%p)\n", newfrag, this));
		*prev = newfrag;
	        newfrag->next = this;
	}
	/* OK, now we have newfrag added in the correct place in the list, but
	   newfrag->next points to a fragment which may be overlapping it
	*/
	while (this && newfrag->ofs + newfrag->size >= this->ofs + this->size) {
		/* 'this' frag is obsoleted. */
		old = this;
		this = old->next;
		jffs2_obsolete_node_frag(c, old);
	}
	/* Now we're pointing at the first frag which isn't totally obsoleted by 
	   the new frag */
	newfrag->next = this;

	if (!this || newfrag->ofs + newfrag->size == this->ofs) {
		return 0;
	}
	/* Still some overlap */
	this->size = (this->ofs + this->size) - (newfrag->ofs + newfrag->size);
	this->ofs = newfrag->ofs + newfrag->size;
	return 0;
}

void jffs2_truncate_fraglist (struct jffs2_sb_info *c, struct jffs2_node_frag **list, uint32_t size)
{
	D1(printk(KERN_DEBUG "Truncating fraglist to 0x%08x bytes\n", size));

	while (*list) {
		if ((*list)->ofs >= size) {
			struct jffs2_node_frag *this = *list;
			*list = this->next;
			D1(printk(KERN_DEBUG "Removing frag 0x%08x-0x%08x\n", this->ofs, this->ofs+this->size));
			jffs2_obsolete_node_frag(c, this);
			continue;
		} else if ((*list)->ofs + (*list)->size > size) {
			D1(printk(KERN_DEBUG "Truncating frag 0x%08x-0x%08x\n", (*list)->ofs, (*list)->ofs + (*list)->size));
			(*list)->size = size - (*list)->ofs;
		}
		list = &(*list)->next;
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
			D1(printk(KERN_DEBUG "Obsoleting old metadata at 0x%08x\n", f->metadata->raw->flash_offset &~3));
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
			
			mdata_ver = 0;
		}

		if (fn->size) {
			jffs2_add_full_dnode_to_inode(c, f, fn);
		} else {
			/* Zero-sized node at end of version list. Just a metadata update */
			D1(printk(KERN_DEBUG "metadata @%08x: ver %d\n", fn->raw->flash_offset &~3, tn->version));
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
		latest_node->mode = S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO;
		latest_node->version = 0;
		latest_node->atime = latest_node->ctime = latest_node->mtime = 0;
		latest_node->isize = 0;
		latest_node->gid = 0;
		latest_node->uid = 0;
		return 0;
	}

	ret = jffs2_flash_read(c, fn->raw->flash_offset & ~3, sizeof(*latest_node), &retlen, (void *)latest_node);
	if (ret || retlen != sizeof(*latest_node)) {
		printk(KERN_NOTICE "MTD read in jffs2_do_read_inode() failed: Returned %d, %ld of %d bytes read\n",
		       ret, (long)retlen, sizeof(*latest_node));
		/* FIXME: If this fails, there seems to be a memory leak. Find it. */
		jffs2_do_clear_inode(c, f);
		return ret?ret:-EIO;
	}

	crc = crc32(0, latest_node, sizeof(*latest_node)-8);
	if (crc != latest_node->node_crc) {
		printk(KERN_NOTICE "CRC failed for read_inode of inode %u at physical location 0x%x\n", ino, fn->raw->flash_offset & ~3);
		jffs2_do_clear_inode(c, f);
		return -EIO;
	}

	switch(latest_node->mode & S_IFMT) {
	case S_IFDIR:
		if (mctime_ver > latest_node->version) {
			/* The times in the latest_node are actually older than
			   mctime in the latest dirent. Cheat. */
			latest_node->ctime = latest_node->mtime = latest_mctime;
		}
		break;

			
	case S_IFREG:
		/* If it was a regular file, truncate it to the latest node's isize */
		jffs2_truncate_fraglist(c, &f->fraglist, latest_node->isize);
		break;

	case S_IFLNK:
		/* Hack to work around broken isize in old symlink code.
		   Remove this when dwmw2 comes to his senses and stops
		   symlinks from being an entirely gratuitous special
		   case. */
		if (!latest_node->isize)
			latest_node->isize = latest_node->dsize;
		/* fall through... */

	case S_IFBLK:
	case S_IFCHR:
		/* Xertain inode types should have only one data node, and it's
		   kept as the metadata node */
		if (f->metadata) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o had metadata node\n", ino, latest_node->mode);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		if (!f->fraglist) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o has no fragments\n", ino, latest_node->mode);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* ASSERT: f->fraglist != NULL */
		if (f->fraglist->next) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o had more than one node\n", ino, latest_node->mode);
			/* FIXME: Deal with it - check crc32, check for duplicate node, check times and discard the older one */
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* OK. We're happy */
		f->metadata = f->fraglist->node;
		jffs2_free_node_frag(f->fraglist);
		f->fraglist = NULL;
		break;
	}

	return 0;
}


void jffs2_do_clear_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f)
{
	struct jffs2_node_frag *frag, *frags;
	struct jffs2_full_dirent *fd, *fds;

	/* If it's a deleted inode, grab the alloc_sem to keep the
	   (maybe temporary) BUG() in jffs2_mark_node_obsolete() 
	   from triggering */
	if(!f->inocache->nlink)
		down(&c->alloc_sem);

	down(&f->sem);

	frags = f->fraglist;
	fds = f->dents;
	if (f->metadata) {
		if (!f->inocache->nlink)
			jffs2_mark_node_obsolete(c, f->metadata->raw);
		jffs2_free_full_dnode(f->metadata);
	}

	while (frags) {
		frag = frags;
		frags = frag->next;
		D2(printk(KERN_DEBUG "jffs2_do_clear_inode: frag at 0x%x-0x%x: node %p, frags %d--\n", frag->ofs, frag->ofs+frag->size, frag->node, frag->node?frag->node->frags:0));

		if (frag->node && !(--frag->node->frags)) {
			/* Not a hole, and it's the final remaining frag of this node. Free the node */
			if (!f->inocache->nlink)
				jffs2_mark_node_obsolete(c, frag->node->raw);

			jffs2_free_full_dnode(frag->node);
		}
		jffs2_free_node_frag(frag);
	}
	while(fds) {
		fd = fds;
		fds = fd->next;
		jffs2_free_full_dirent(fd);
	}

	/* Urgh. Is there a nicer way to do this? */
	if(!f->inocache->nlink) {
		up(&f->sem);
		up(&c->alloc_sem);
	} else {
		up(&f->sem);
	}
}
