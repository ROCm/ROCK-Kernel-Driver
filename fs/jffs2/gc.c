/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: gc.c,v 1.68 2002/03/08 15:11:24 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/crc32.h>
#include "nodelist.h"

static int jffs2_garbage_collect_metadata(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
					struct jffs2_inode_info *f, struct jffs2_full_dnode *fd);
static int jffs2_garbage_collect_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd);
static int jffs2_garbage_collect_deletion_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd);
static int jffs2_garbage_collect_hole(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				      struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				      uint32_t start, uint32_t end);
static int jffs2_garbage_collect_dnode(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				       struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				       uint32_t start, uint32_t end);

/* Called with erase_completion_lock held */
static struct jffs2_eraseblock *jffs2_find_gc_block(struct jffs2_sb_info *c)
{
	struct jffs2_eraseblock *ret;
	struct list_head *nextlist = NULL;
	int n = jiffies % 128;

	/* Pick an eraseblock to garbage collect next. This is where we'll
	   put the clever wear-levelling algorithms. Eventually.  */
	if (!list_empty(&c->bad_used_list) && c->nr_free_blocks > JFFS2_RESERVED_BLOCKS_GCBAD) {
		D1(printk(KERN_DEBUG "Picking block from bad_used_list to GC next\n"));
		nextlist = &c->bad_used_list;
	} else if (n < 100 && !list_empty(&c->erasable_list)) {
		D1(printk(KERN_DEBUG "Picking block from erasable_list to GC next\n"));
		nextlist = &c->erasable_list;
	} else if (n < 126 && !list_empty(&c->dirty_list)) {
		/* Most of the time, pick one off the dirty list */
		D1(printk(KERN_DEBUG "Picking block from dirty_list to GC next\n"));
		nextlist = &c->dirty_list;
	} else if (!list_empty(&c->clean_list)) {
		D1(printk(KERN_DEBUG "Picking block from clean_list to GC next\n"));
		nextlist = &c->clean_list;
	} else if (!list_empty(&c->dirty_list)) {
		D1(printk(KERN_DEBUG "Picking block from dirty_list to GC next (clean_list was empty)\n"));

		nextlist = &c->dirty_list;
	} else if (!list_empty(&c->erasable_list)) {
		D1(printk(KERN_DEBUG "Picking block from erasable_list to GC next (clean_list and dirty_list were empty)\n"));

		nextlist = &c->erasable_list;
	} else {
		/* Eep. Both were empty */
		printk(KERN_NOTICE "jffs2: No clean, dirty _or_ erasable blocks to GC from! Where are they all?\n");
		return NULL;
	}

	ret = list_entry(nextlist->next, struct jffs2_eraseblock, list);
	list_del(&ret->list);
	c->gcblock = ret;
	ret->gc_node = ret->first_node;
	if (!ret->gc_node) {
		printk(KERN_WARNING "Eep. ret->gc_node for block at 0x%08x is NULL\n", ret->offset);
		BUG();
	}
	return ret;
}

/* jffs2_garbage_collect_pass
 * Make a single attempt to progress GC. Move one node, and possibly
 * start erasing one eraseblock.
 */
int jffs2_garbage_collect_pass(struct jffs2_sb_info *c)
{
	struct jffs2_eraseblock *jeb;
	struct jffs2_inode_info *f;
	struct jffs2_raw_node_ref *raw;
	struct jffs2_node_frag *frag;
	struct jffs2_full_dnode *fn = NULL;
	struct jffs2_full_dirent *fd;
	uint32_t start = 0, end = 0, nrfrags = 0;
	uint32_t inum;
	struct inode *inode;
	int ret = 0;

	if (down_interruptible(&c->alloc_sem))
		return -EINTR;

	spin_lock_bh(&c->erase_completion_lock);

	/* First, work out which block we're garbage-collecting */
	jeb = c->gcblock;

	if (!jeb)
		jeb = jffs2_find_gc_block(c);

	if (!jeb) {
		printk(KERN_NOTICE "jffs2: Couldn't find erase block to garbage collect!\n");
		spin_unlock_bh(&c->erase_completion_lock);
		up(&c->alloc_sem);
		return -EIO;
	}

	D1(printk(KERN_DEBUG "garbage collect from block at phys 0x%08x\n", jeb->offset));

	if (!jeb->used_size)
		goto eraseit;

	raw = jeb->gc_node;
			
	while(raw->flash_offset & 1) {
		D1(printk(KERN_DEBUG "Node at 0x%08x is obsolete... skipping\n", raw->flash_offset &~3));
		jeb->gc_node = raw = raw->next_phys;
		if (!raw) {
			printk(KERN_WARNING "eep. End of raw list while still supposedly nodes to GC\n");
			printk(KERN_WARNING "erase block at 0x%08x. free_size 0x%08x, dirty_size 0x%08x, used_size 0x%08x\n", 
			       jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size);
			spin_unlock_bh(&c->erase_completion_lock);
			up(&c->alloc_sem);
			BUG();
		}
	}
	D1(printk(KERN_DEBUG "Going to garbage collect node at 0x%08x\n", raw->flash_offset &~3));
	if (!raw->next_in_ino) {
		/* Inode-less node. Clean marker, snapshot or something like that */
		/* FIXME: If it's something that needs to be copied, including something
		   we don't grok that has JFFS2_NODETYPE_RWCOMPAT_COPY, we should do so */
		spin_unlock_bh(&c->erase_completion_lock);
		jffs2_mark_node_obsolete(c, raw);
		goto eraseit_lock;
	}
						     
	inum = jffs2_raw_ref_to_inum(raw);
	D1(printk(KERN_DEBUG "Inode number is #%u\n", inum));

	spin_unlock_bh(&c->erase_completion_lock);

	D1(printk(KERN_DEBUG "jffs2_garbage_collect_pass collecting from block @0x%08x. Node @0x%08x, ino #%u\n", jeb->offset, raw->flash_offset&~3, inum));

	inode = iget(OFNI_BS_2SFFJ(c), inum);
	if (is_bad_inode(inode)) {
		printk(KERN_NOTICE "Eep. read_inode() failed for ino #%u\n", inum);
		/* NB. This will happen again. We need to do something appropriate here. */
		iput(inode);
		up(&c->alloc_sem);
		return -EIO;
	}

	f = JFFS2_INODE_INFO(inode);
	down(&f->sem);

	/* Now we have the lock for this inode. Check that it's still the one at the head
	   of the list. */

	if (raw->flash_offset & 1) {
		D1(printk(KERN_DEBUG "node to be GC'd was obsoleted in the meantime.\n"));
		/* They'll call again */
		goto upnout;
	}
	/* OK. Looks safe. And nobody can get us now because we have the semaphore. Move the block */
	if (f->metadata && f->metadata->raw == raw) {
		fn = f->metadata;
		ret = jffs2_garbage_collect_metadata(c, jeb, f, fn);
		goto upnout;
	}
	
	for (frag = f->fraglist; frag; frag = frag->next) {
		if (frag->node && frag->node->raw == raw) {
			fn = frag->node;
			end = frag->ofs + frag->size;
			if (!nrfrags++)
				start = frag->ofs;
			if (nrfrags == frag->node->frags)
				break; /* We've found them all */
		}
	}
	if (fn) {
		/* We found a datanode. Do the GC */
		if((start >> PAGE_CACHE_SHIFT) < ((end-1) >> PAGE_CACHE_SHIFT)) {
			/* It crosses a page boundary. Therefore, it must be a hole. */
			ret = jffs2_garbage_collect_hole(c, jeb, f, fn, start, end);
		} else {
			/* It could still be a hole. But we GC the page this way anyway */
			ret = jffs2_garbage_collect_dnode(c, jeb, f, fn, start, end);
		}
		goto upnout;
	}
	
	/* Wasn't a dnode. Try dirent */
	for (fd = f->dents; fd; fd=fd->next) {
		if (fd->raw == raw)
			break;
	}

	if (fd && fd->ino) {
		ret = jffs2_garbage_collect_dirent(c, jeb, f, fd);
	} else if (fd) {
		ret = jffs2_garbage_collect_deletion_dirent(c, jeb, f, fd);
	} else {
		printk(KERN_WARNING "Raw node at 0x%08x wasn't in node lists for ino #%u\n",
		       raw->flash_offset&~3, f->inocache->ino);
		if (raw->flash_offset & 1) {
			printk(KERN_WARNING "But it's obsolete so we don't mind too much\n");
		} else {
			ret = -EIO;
		}
	}
 upnout:
	up(&f->sem);
	iput(inode);

 eraseit_lock:
	/* If we've finished this block, start it erasing */
	spin_lock_bh(&c->erase_completion_lock);

 eraseit:
	if (c->gcblock && !c->gcblock->used_size) {
		D1(printk(KERN_DEBUG "Block at 0x%08x completely obsoleted by GC. Moving to erase_pending_list\n", c->gcblock->offset));
		/* We're GC'ing an empty block? */
		list_add_tail(&c->gcblock->list, &c->erase_pending_list);
		c->gcblock = NULL;
		c->nr_erasing_blocks++;
		jffs2_erase_pending_trigger(c);
	}
	spin_unlock_bh(&c->erase_completion_lock);
	up(&c->alloc_sem);

	return ret;
}

static int jffs2_garbage_collect_metadata(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
					struct jffs2_inode_info *f, struct jffs2_full_dnode *fn)
{
	struct jffs2_full_dnode *new_fn;
	struct jffs2_raw_inode ri;
	unsigned short dev;
	char *mdata = NULL, mdatalen = 0;
	uint32_t alloclen, phys_ofs;
	int ret;

	if (S_ISBLK(JFFS2_F_I_MODE(f)) ||
	    S_ISCHR(JFFS2_F_I_MODE(f)) ) {
		/* For these, we don't actually need to read the old node */
		/* FIXME: for minor or major > 255. */
		dev =  ((JFFS2_F_I_RDEV_MAJ(f) << 8) | 
			JFFS2_F_I_RDEV_MIN(f));
		mdata = (char *)&dev;
		mdatalen = sizeof(dev);
		D1(printk(KERN_DEBUG "jffs2_garbage_collect_metadata(): Writing %d bytes of kdev_t\n", mdatalen));
	} else if (S_ISLNK(JFFS2_F_I_MODE(f))) {
		mdatalen = fn->size;
		mdata = kmalloc(fn->size, GFP_KERNEL);
		if (!mdata) {
			printk(KERN_WARNING "kmalloc of mdata failed in jffs2_garbage_collect_metadata()\n");
			return -ENOMEM;
		}
		ret = jffs2_read_dnode(c, fn, mdata, 0, mdatalen);
		if (ret) {
			printk(KERN_WARNING "read of old metadata failed in jffs2_garbage_collect_metadata(): %d\n", ret);
			kfree(mdata);
			return ret;
		}
		D1(printk(KERN_DEBUG "jffs2_garbage_collect_metadata(): Writing %d bites of symlink target\n", mdatalen));

	}
	
	ret = jffs2_reserve_space_gc(c, sizeof(ri) + mdatalen, &phys_ofs, &alloclen);
	if (ret) {
		printk(KERN_WARNING "jffs2_reserve_space_gc of %d bytes for garbage_collect_metadata failed: %d\n",
		       sizeof(ri)+ mdatalen, ret);
		goto out;
	}
	
	memset(&ri, 0, sizeof(ri));
	ri.magic = JFFS2_MAGIC_BITMASK;
	ri.nodetype = JFFS2_NODETYPE_INODE;
	ri.totlen = sizeof(ri) + mdatalen;
	ri.hdr_crc = crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4);

	ri.ino = f->inocache->ino;
	ri.version = ++f->highest_version;
	ri.mode = JFFS2_F_I_MODE(f);
	ri.uid = JFFS2_F_I_UID(f);
	ri.gid = JFFS2_F_I_GID(f);
	ri.isize = JFFS2_F_I_SIZE(f);
	ri.atime = JFFS2_F_I_ATIME(f);
	ri.ctime = JFFS2_F_I_CTIME(f);
	ri.mtime = JFFS2_F_I_MTIME(f);
	ri.offset = 0;
	ri.csize = mdatalen;
	ri.dsize = mdatalen;
	ri.compr = JFFS2_COMPR_NONE;
	ri.node_crc = crc32(0, &ri, sizeof(ri)-8);
	ri.data_crc = crc32(0, mdata, mdatalen);

	new_fn = jffs2_write_dnode(c, f, &ri, mdata, mdatalen, phys_ofs, NULL);

	if (IS_ERR(new_fn)) {
		printk(KERN_WARNING "Error writing new dnode: %ld\n", PTR_ERR(new_fn));
		ret = PTR_ERR(new_fn);
		goto out;
	}
	jffs2_mark_node_obsolete(c, fn->raw);
	jffs2_free_full_dnode(fn);
	f->metadata = new_fn;
 out:
	if (S_ISLNK(JFFS2_F_I_MODE(f)))
		kfree(mdata);
	return ret;
}

static int jffs2_garbage_collect_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent *new_fd;
	struct jffs2_raw_dirent rd;
	uint32_t alloclen, phys_ofs;
	int ret;

	rd.magic = JFFS2_MAGIC_BITMASK;
	rd.nodetype = JFFS2_NODETYPE_DIRENT;
	rd.nsize = strlen(fd->name);
	rd.totlen = sizeof(rd) + rd.nsize;
	rd.hdr_crc = crc32(0, &rd, sizeof(struct jffs2_unknown_node)-4);

	rd.pino = f->inocache->ino;
	rd.version = ++f->highest_version;
	rd.ino = fd->ino;
	rd.mctime = max(JFFS2_F_I_MTIME(f), JFFS2_F_I_CTIME(f));
	rd.type = fd->type;
	rd.node_crc = crc32(0, &rd, sizeof(rd)-8);
	rd.name_crc = crc32(0, fd->name, rd.nsize);
	
	ret = jffs2_reserve_space_gc(c, sizeof(rd)+rd.nsize, &phys_ofs, &alloclen);
	if (ret) {
		printk(KERN_WARNING "jffs2_reserve_space_gc of %d bytes for garbage_collect_dirent failed: %d\n",
		       sizeof(rd)+rd.nsize, ret);
		return ret;
	}
	new_fd = jffs2_write_dirent(c, f, &rd, fd->name, rd.nsize, phys_ofs, NULL);

	if (IS_ERR(new_fd)) {
		printk(KERN_WARNING "jffs2_write_dirent in garbage_collect_dirent failed: %ld\n", PTR_ERR(new_fd));
		return PTR_ERR(new_fd);
	}
	jffs2_add_fd_to_list(c, new_fd, &f->dents);
	return 0;
}

static int jffs2_garbage_collect_deletion_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent **fdp = &f->dents;
	int found = 0;

	/* On a medium where we can't actually mark nodes obsolete
	   pernamently, such as NAND flash, we need to work out
	   whether this deletion dirent is still needed to actively
	   delete a 'real' dirent with the same name that's still
	   somewhere else on the flash. */
	if (!jffs2_can_mark_obsolete(c)) {
		struct jffs2_raw_dirent rd;
		struct jffs2_raw_node_ref *raw;
		int ret;
		size_t retlen;
		int name_len = strlen(fd->name);
		uint32_t name_crc = crc32(0, fd->name, name_len);
		char *namebuf = NULL;

		/* Prevent the erase code from nicking the obsolete node refs while
		   we're looking at them. I really don't like this extra lock but
		   can't see any alternative. Suggestions on a postcard to... */
		down(&c->erase_free_sem);

		for (raw = f->inocache->nodes; raw != (void *)f->inocache; raw = raw->next_in_ino) {
			/* We only care about obsolete ones */
			if (!(raw->flash_offset & 1))
				continue;

			/* Doesn't matter if there's one in the same erase block. We're going to 
			   delete it too at the same time. */
			if ((raw->flash_offset & ~(c->sector_size-1)) ==
			    (fd->raw->flash_offset & ~(c->sector_size-1)))
				continue;

			/* This is an obsolete node belonging to the same directory */
			ret = jffs2_flash_read(c, raw->flash_offset & ~3, sizeof(struct jffs2_unknown_node), &retlen, (char *)&rd);
			if (ret) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Read error (%d) reading header from obsolete node at %08x\n", ret, raw->flash_offset & ~3);
				/* If we can't read it, we don't need to continune to obsolete it. Continue */
				continue;
			}
			if (retlen != sizeof(struct jffs2_unknown_node)) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Short read (%d not %d) reading header from obsolete node at %08x\n",
				       retlen, sizeof(struct jffs2_unknown_node), raw->flash_offset & ~3);
				continue;
			}
			if (rd.nodetype != JFFS2_NODETYPE_DIRENT ||
			    PAD(rd.totlen) != PAD(sizeof(rd) + name_len))
				continue;

			/* OK, it's a dirent node, it's the right length. We have to take a 
			   closer look at it... */
			ret = jffs2_flash_read(c, raw->flash_offset & ~3, sizeof(rd), &retlen, (char *)&rd);
			if (ret) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Read error (%d) reading from obsolete node at %08x\n", ret, raw->flash_offset & ~3);
				/* If we can't read it, we don't need to continune to obsolete it. Continue */
				continue;
			}
			if (retlen != sizeof(struct jffs2_unknown_node)) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Short read (%d not %d) reading from obsolete node at %08x\n",
				       retlen, sizeof(struct jffs2_unknown_node), raw->flash_offset & ~3);
				continue;
			}

			/* If the name CRC doesn't match, skip */
			if (rd.name_crc != name_crc)
				continue;
			/* If the name length doesn't match, or it's another deletion dirent, skip */
			if (rd.nsize != name_len || !rd.ino)
				continue;

			/* OK, check the actual name now */
			if (!namebuf) {
				namebuf = kmalloc(name_len + 1, GFP_KERNEL);
				if (!namebuf) {
					up(&c->erase_free_sem);
					return -ENOMEM;
				}
			}
			/* We read the extra byte before it so it's a word-aligned read */
			ret = jffs2_flash_read(c, (raw->flash_offset & ~3)+sizeof(rd)-1, name_len+1, &retlen, namebuf);
			if (ret) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Read error (%d) reading name from obsolete node at %08x\n", ret, raw->flash_offset & ~3);
				/* If we can't read it, we don't need to continune to obsolete it. Continue */
				continue;
			}
			if (retlen != sizeof(rd)) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Short read (%d not %d) reading name from obsolete node at %08x\n",
				       retlen, name_len, raw->flash_offset & ~3);
				continue;
			}
			if (memcmp(namebuf+1, fd->name, name_len))
				continue;

			/* OK. The name really does match. There really is still an older node on
			   the flash which our deletion dirent obsoletes. So we have to write out
			   a new deletion dirent to replace it */
			
			if (namebuf)
				kfree(namebuf);

			up(&c->erase_free_sem);
			return jffs2_garbage_collect_dirent(c, jeb, f, fd);
		}

		up(&c->erase_free_sem);

		if (namebuf) 
			kfree(namebuf);
	}

	/* No need for it any more. Just mark it obsolete and remove it from the list */
	while (*fdp) {
		if ((*fdp) == fd) {
			found = 1;
			*fdp = fd->next;
			break;
		}
		fdp = &(*fdp)->next;
	}
	if (!found) {
		printk(KERN_WARNING "Deletion dirent \"%s\" not found in list for ino #%u\n", fd->name, f->inocache->ino);
	}
	jffs2_mark_node_obsolete(c, fd->raw);
	jffs2_free_full_dirent(fd);
	return 0;
}

static int jffs2_garbage_collect_hole(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				      struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				      uint32_t start, uint32_t end)
{
	struct jffs2_raw_inode ri;
	struct jffs2_node_frag *frag;
	struct jffs2_full_dnode *new_fn;
	uint32_t alloclen, phys_ofs;
	int ret;

	D1(printk(KERN_DEBUG "Writing replacement hole node for ino #%u from offset 0x%x to 0x%x\n",
		  f->inocache->ino, start, end));
	
	memset(&ri, 0, sizeof(ri));

	if(fn->frags > 1) {
		size_t readlen;
		uint32_t crc;
		/* It's partially obsoleted by a later write. So we have to 
		   write it out again with the _same_ version as before */
		ret = jffs2_flash_read(c, fn->raw->flash_offset & ~3, sizeof(ri), &readlen, (char *)&ri);
		if (readlen != sizeof(ri) || ret) {
			printk(KERN_WARNING "Node read failed in jffs2_garbage_collect_hole. Ret %d, retlen %d. Data will be lost by writing new hold node\n", ret, readlen);
			goto fill;
		}
		if (ri.nodetype != JFFS2_NODETYPE_INODE) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node at 0x%08x had node type 0x%04x instead of JFFS2_NODETYPE_INODE(0x%04x)\n",
			       fn->raw->flash_offset & ~3, ri.nodetype, JFFS2_NODETYPE_INODE);
			return -EIO;
		}
		if (ri.totlen != sizeof(ri)) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node at 0x%08x had totlen 0x%x instead of expected 0x%x\n",
			       fn->raw->flash_offset & ~3, ri.totlen, sizeof(ri));
			return -EIO;
		}
		crc = crc32(0, &ri, sizeof(ri)-8);
		if (crc != ri.node_crc) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node at 0x%08x had CRC 0x%08x which doesn't match calculated CRC 0x%08x\n",
			       fn->raw->flash_offset & ~3, ri.node_crc, crc);
			/* FIXME: We could possibly deal with this by writing new holes for each frag */
			printk(KERN_WARNING "Data in the range 0x%08x to 0x%08x of inode #%u will be lost\n", 
			       start, end, f->inocache->ino);
			goto fill;
		}
		if (ri.compr != JFFS2_COMPR_ZERO) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node 0x%08x wasn't a hole node!\n", fn->raw->flash_offset & ~3);
			printk(KERN_WARNING "Data in the range 0x%08x to 0x%08x of inode #%u will be lost\n", 
			       start, end, f->inocache->ino);
			goto fill;
		}
	} else {
	fill:
		ri.magic = JFFS2_MAGIC_BITMASK;
		ri.nodetype = JFFS2_NODETYPE_INODE;
		ri.totlen = sizeof(ri);
		ri.hdr_crc = crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4);

		ri.ino = f->inocache->ino;
		ri.version = ++f->highest_version;
		ri.offset = start;
		ri.dsize = end - start;
		ri.csize = 0;
		ri.compr = JFFS2_COMPR_ZERO;
	}
	ri.mode = JFFS2_F_I_MODE(f);
	ri.uid = JFFS2_F_I_UID(f);
	ri.gid = JFFS2_F_I_GID(f);
	ri.isize = JFFS2_F_I_SIZE(f);
	ri.atime = JFFS2_F_I_ATIME(f);
	ri.ctime = JFFS2_F_I_CTIME(f);
	ri.mtime = JFFS2_F_I_MTIME(f);
	ri.data_crc = 0;
	ri.node_crc = crc32(0, &ri, sizeof(ri)-8);

	ret = jffs2_reserve_space_gc(c, sizeof(ri), &phys_ofs, &alloclen);
	if (ret) {
		printk(KERN_WARNING "jffs2_reserve_space_gc of %d bytes for garbage_collect_hole failed: %d\n",
		       sizeof(ri), ret);
		return ret;
	}
	new_fn = jffs2_write_dnode(c, f, &ri, NULL, 0, phys_ofs, NULL);

	if (IS_ERR(new_fn)) {
		printk(KERN_WARNING "Error writing new hole node: %ld\n", PTR_ERR(new_fn));
		return PTR_ERR(new_fn);
	}
	if (ri.version == f->highest_version) {
		jffs2_add_full_dnode_to_inode(c, f, new_fn);
		if (f->metadata) {
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
			return 0;
		}
	}
	for (frag = f->fraglist; frag; frag = frag->next) {
		if (frag->ofs > fn->size + fn->ofs)
			break;
		if (frag->node == fn) {
			frag->node = new_fn;
			new_fn->frags++;
			fn->frags--;
		}
	}
	if (fn->frags) {
		printk(KERN_WARNING "jffs2_garbage_collect_hole: Old node still has frags!\n");
		BUG();
	}
	if (!new_fn->frags) {
		printk(KERN_WARNING "jffs2_garbage_collect_hole: New node has no frags!\n");
		BUG();
	}
		
	jffs2_mark_node_obsolete(c, fn->raw);
	jffs2_free_full_dnode(fn);
	
	return 0;
}

static int jffs2_garbage_collect_dnode(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				       struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				       uint32_t start, uint32_t end)
{
	struct jffs2_full_dnode *new_fn;
	struct jffs2_raw_inode ri;
	uint32_t alloclen, phys_ofs, offset, orig_end;	
	int ret = 0;
	unsigned char *comprbuf = NULL, *writebuf;
	struct page *pg;
	unsigned char *pg_ptr;
	/* FIXME: */ struct inode *inode = OFNI_EDONI_2SFFJ(f);

	memset(&ri, 0, sizeof(ri));

	D1(printk(KERN_DEBUG "Writing replacement dnode for ino #%u from offset 0x%x to 0x%x\n",
		  f->inocache->ino, start, end));

	orig_end = end;


	/* If we're looking at the last node in the block we're
	   garbage-collecting, we allow ourselves to merge as if the
	   block was already erasing. We're likely to be GC'ing a
	   partial page, and the next block we GC is likely to have
	   the other half of this page right at the beginning, which
	   means we'd expand it _then_, as nr_erasing_blocks would have
	   increased since we checked, and in doing so would obsolete 
	   the partial node which we'd have written here. Meaning that 
	   the GC would churn and churn, and just leave dirty blocks in
	   it's wake.
	*/
	if(c->nr_free_blocks + c->nr_erasing_blocks > JFFS2_RESERVED_BLOCKS_GCMERGE - (fn->raw->next_phys?0:1)) {
		/* Shitloads of space */
		/* FIXME: Integrate this properly with GC calculations */
		start &= ~(PAGE_CACHE_SIZE-1);
		end = min_t(uint32_t, start + PAGE_CACHE_SIZE, JFFS2_F_I_SIZE(f));
		D1(printk(KERN_DEBUG "Plenty of free space, so expanding to write from offset 0x%x to 0x%x\n",
			  start, end));
		if (end < orig_end) {
			printk(KERN_WARNING "Eep. jffs2_garbage_collect_dnode extended node to write, but it got smaller: start 0x%x, orig_end 0x%x, end 0x%x\n", start, orig_end, end);
			end = orig_end;
		}
	}
	
	/* First, use readpage() to read the appropriate page into the page cache */
	/* Q: What happens if we actually try to GC the _same_ page for which commit_write()
	 *    triggered garbage collection in the first place?
	 * A: I _think_ it's OK. read_cache_page shouldn't deadlock, we'll write out the
	 *    page OK. We'll actually write it out again in commit_write, which is a little
	 *    suboptimal, but at least we're correct.
	 */
	pg = read_cache_page(inode->i_mapping, start >> PAGE_CACHE_SHIFT, (void *)jffs2_do_readpage_unlock, inode);

	if (IS_ERR(pg)) {
		printk(KERN_WARNING "read_cache_page() returned error: %ld\n", PTR_ERR(pg));
		return PTR_ERR(pg);
	}
	pg_ptr = (char *)kmap(pg);
	comprbuf = kmalloc(end - start, GFP_KERNEL);

	offset = start;
	while(offset < orig_end) {
		uint32_t datalen;
		uint32_t cdatalen;
		char comprtype = JFFS2_COMPR_NONE;

		ret = jffs2_reserve_space_gc(c, sizeof(ri) + JFFS2_MIN_DATA_LEN, &phys_ofs, &alloclen);

		if (ret) {
			printk(KERN_WARNING "jffs2_reserve_space_gc of %d bytes for garbage_collect_dnode failed: %d\n",
			       sizeof(ri)+ JFFS2_MIN_DATA_LEN, ret);
			break;
		}
		cdatalen = min(alloclen - sizeof(ri), end - offset);
		datalen = end - offset;

		writebuf = pg_ptr + (offset & (PAGE_CACHE_SIZE -1));

		if (comprbuf) {
			comprtype = jffs2_compress(writebuf, comprbuf, &datalen, &cdatalen);
		}
		if (comprtype) {
			writebuf = comprbuf;
		} else {
			datalen = cdatalen;
		}
		ri.magic = JFFS2_MAGIC_BITMASK;
		ri.nodetype = JFFS2_NODETYPE_INODE;
		ri.totlen = sizeof(ri) + cdatalen;
		ri.hdr_crc = crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4);

		ri.ino = f->inocache->ino;
		ri.version = ++f->highest_version;
		ri.mode = JFFS2_F_I_MODE(f);
		ri.uid = JFFS2_F_I_UID(f);
		ri.gid = JFFS2_F_I_GID(f);
		ri.isize = JFFS2_F_I_SIZE(f);
		ri.atime = JFFS2_F_I_ATIME(f);
		ri.ctime = JFFS2_F_I_CTIME(f);
		ri.mtime = JFFS2_F_I_MTIME(f);
		ri.offset = offset;
		ri.csize = cdatalen;
		ri.dsize = datalen;
		ri.compr = comprtype;
		ri.node_crc = crc32(0, &ri, sizeof(ri)-8);
		ri.data_crc = crc32(0, writebuf, cdatalen);
	
		new_fn = jffs2_write_dnode(c, f, &ri, writebuf, cdatalen, phys_ofs, NULL);

		if (IS_ERR(new_fn)) {
			printk(KERN_WARNING "Error writing new dnode: %ld\n", PTR_ERR(new_fn));
			ret = PTR_ERR(new_fn);
			break;
		}
		ret = jffs2_add_full_dnode_to_inode(c, f, new_fn);
		offset += datalen;
		if (f->metadata) {
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
		}
	}
	if (comprbuf) kfree(comprbuf);

	kunmap(pg);
	/* XXX: Does the page get freed automatically? */
	/* AAA: Judging by the unmount getting stuck in __wait_on_page, nope. */
	page_cache_release(pg);
	return ret;
}

