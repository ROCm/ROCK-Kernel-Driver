/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: file.c,v 1.85 2003/05/26 09:50:38 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/crc32.h>
#include <linux/jffs2.h>
#include "nodelist.h"

extern int generic_file_open(struct inode *, struct file *) __attribute__((weak));
extern loff_t generic_file_llseek(struct file *file, loff_t offset, int origin) __attribute__((weak));


int jffs2_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	if (!c->wbuf || !c->wbuf_len)
		return 0;

	/* flush write buffer and update c->nextblock */
	
	/* FIXME NAND */
	/* At the moment we flush the buffer, to make sure
	 * that every thing is on the flash.
	 * maybe we have to think about it to find a smarter
	 * solution.
	 */
	down(&c->alloc_sem);
	down(&f->sem);
	jffs2_flush_wbuf(c,2);
	up(&f->sem);
	up(&c->alloc_sem);
			
	return 0;	
}

struct file_operations jffs2_file_operations =
{
	.llseek =	generic_file_llseek,
	.open =		generic_file_open,
	.read =		generic_file_read,
	.write =	generic_file_write,
	.ioctl =	jffs2_ioctl,
	.mmap =		generic_file_readonly_mmap,
	.fsync =	jffs2_fsync,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,29)
	.sendfile =	generic_file_sendfile
#endif
};

/* jffs2_file_inode_operations */

struct inode_operations jffs2_file_inode_operations =
{
	.setattr =	jffs2_setattr
};

struct address_space_operations jffs2_file_address_operations =
{
	.readpage =	jffs2_readpage,
	.prepare_write =jffs2_prepare_write,
	.commit_write =	jffs2_commit_write
};

int jffs2_setattr (struct dentry *dentry, struct iattr *iattr)
{
	struct jffs2_full_dnode *old_metadata, *new_metadata;
	struct inode *inode = dentry->d_inode;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_raw_inode *ri;
	unsigned short dev;
	unsigned char *mdata = NULL;
	int mdatalen = 0;
	unsigned int ivalid;
	uint32_t phys_ofs, alloclen;
	int ret;
	D1(printk(KERN_DEBUG "jffs2_setattr(): ino #%lu\n", inode->i_ino));
	ret = inode_change_ok(inode, iattr);
	if (ret) 
		return ret;

	/* Special cases - we don't want more than one data node
	   for these types on the medium at any time. So setattr
	   must read the original data associated with the node
	   (i.e. the device numbers or the target name) and write
	   it out again with the appropriate data attached */
	if (S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode)) {
		/* For these, we don't actually need to read the old node */
		dev =  (major(dentry->d_inode->i_rdev) << 8) | 
			minor(dentry->d_inode->i_rdev);
		mdata = (char *)&dev;
		mdatalen = sizeof(dev);
		D1(printk(KERN_DEBUG "jffs2_setattr(): Writing %d bytes of kdev_t\n", mdatalen));
	} else if (S_ISLNK(inode->i_mode)) {
		mdatalen = f->metadata->size;
		mdata = kmalloc(f->metadata->size, GFP_USER);
		if (!mdata)
			return -ENOMEM;
		ret = jffs2_read_dnode(c, f->metadata, mdata, 0, mdatalen);
		if (ret) {
			kfree(mdata);
			return ret;
		}
		D1(printk(KERN_DEBUG "jffs2_setattr(): Writing %d bytes of symlink target\n", mdatalen));
	}

	ri = jffs2_alloc_raw_inode();
	if (!ri) {
		if (S_ISLNK(inode->i_mode))
			kfree(mdata);
		return -ENOMEM;
	}
		
	ret = jffs2_reserve_space(c, sizeof(*ri) + mdatalen, &phys_ofs, &alloclen, ALLOC_NORMAL);
	if (ret) {
		jffs2_free_raw_inode(ri);
		if (S_ISLNK(inode->i_mode & S_IFMT))
			 kfree(mdata);
		return ret;
	}
	down(&f->sem);
	ivalid = iattr->ia_valid;
	
	ri->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri->nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
	ri->totlen = cpu_to_je32(sizeof(*ri) + mdatalen);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unknown_node)-4));

	ri->ino = cpu_to_je32(inode->i_ino);
	ri->version = cpu_to_je32(++f->highest_version);

	ri->uid = cpu_to_je16((ivalid & ATTR_UID)?iattr->ia_uid:inode->i_uid);
	ri->gid = cpu_to_je16((ivalid & ATTR_GID)?iattr->ia_gid:inode->i_gid);

	if (ivalid & ATTR_MODE)
		if (iattr->ia_mode & S_ISGID &&
		    !in_group_p(je16_to_cpu(ri->gid)) && !capable(CAP_FSETID))
			ri->mode = cpu_to_jemode(iattr->ia_mode & ~S_ISGID);
		else 
			ri->mode = cpu_to_jemode(iattr->ia_mode);
	else
		ri->mode = cpu_to_jemode(inode->i_mode);


	ri->isize = cpu_to_je32((ivalid & ATTR_SIZE)?iattr->ia_size:inode->i_size);
	ri->atime = cpu_to_je32(I_SEC((ivalid & ATTR_ATIME)?iattr->ia_atime:inode->i_atime));
	ri->mtime = cpu_to_je32(I_SEC((ivalid & ATTR_MTIME)?iattr->ia_mtime:inode->i_mtime));
	ri->ctime = cpu_to_je32(I_SEC((ivalid & ATTR_CTIME)?iattr->ia_ctime:inode->i_ctime));

	ri->offset = cpu_to_je32(0);
	ri->csize = ri->dsize = cpu_to_je32(mdatalen);
	ri->compr = JFFS2_COMPR_NONE;
	if (ivalid & ATTR_SIZE && inode->i_size < iattr->ia_size) {
		/* It's an extension. Make it a hole node */
		ri->compr = JFFS2_COMPR_ZERO;
		ri->dsize = cpu_to_je32(iattr->ia_size - inode->i_size);
		ri->offset = cpu_to_je32(inode->i_size);
	}
	ri->node_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));
	if (mdatalen)
		ri->data_crc = cpu_to_je32(crc32(0, mdata, mdatalen));
	else
		ri->data_crc = cpu_to_je32(0);

	new_metadata = jffs2_write_dnode(c, f, ri, mdata, mdatalen, phys_ofs, NULL);
	if (S_ISLNK(inode->i_mode))
		kfree(mdata);
	
	if (IS_ERR(new_metadata)) {
		jffs2_complete_reservation(c);
		jffs2_free_raw_inode(ri);
		up(&f->sem);
		return PTR_ERR(new_metadata);
	}
	/* It worked. Update the inode */
	inode->i_atime = ITIME(je32_to_cpu(ri->atime));
	inode->i_ctime = ITIME(je32_to_cpu(ri->ctime));
	inode->i_mtime = ITIME(je32_to_cpu(ri->mtime));
	inode->i_mode = jemode_to_cpu(ri->mode);
	inode->i_uid = je16_to_cpu(ri->uid);
	inode->i_gid = je16_to_cpu(ri->gid);


	old_metadata = f->metadata;

	if (ivalid & ATTR_SIZE && inode->i_size > iattr->ia_size) {
		vmtruncate(inode, iattr->ia_size);
		jffs2_truncate_fraglist (c, &f->fragtree, iattr->ia_size);
	}

	if (ivalid & ATTR_SIZE && inode->i_size < iattr->ia_size) {
		jffs2_add_full_dnode_to_inode(c, f, new_metadata);
		inode->i_size = iattr->ia_size;
		f->metadata = NULL;
	} else {
		f->metadata = new_metadata;
	}
	if (old_metadata) {
		jffs2_mark_node_obsolete(c, old_metadata->raw);
		jffs2_free_full_dnode(old_metadata);
	}
	jffs2_free_raw_inode(ri);

	up(&f->sem);
	jffs2_complete_reservation(c);

	return 0;
}

int jffs2_do_readpage_nolock (struct inode *inode, struct page *pg)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	unsigned char *pg_buf;
	int ret;

	D1(printk(KERN_DEBUG "jffs2_do_readpage_nolock(): ino #%lu, page at offset 0x%lx\n", inode->i_ino, pg->index << PAGE_CACHE_SHIFT));

	if (!PageLocked(pg))
                PAGE_BUG(pg);

	pg_buf = kmap(pg);
	/* FIXME: Can kmap fail? */

	ret = jffs2_read_inode_range(c, f, pg_buf, pg->index << PAGE_CACHE_SHIFT, PAGE_CACHE_SIZE);

	if (ret) {
		ClearPageUptodate(pg);
		SetPageError(pg);
	} else {
		SetPageUptodate(pg);
		ClearPageError(pg);
	}

	flush_dcache_page(pg);
	kunmap(pg);

	D1(printk(KERN_DEBUG "readpage finished\n"));
	return 0;
}

int jffs2_do_readpage_unlock(struct inode *inode, struct page *pg)
{
	int ret = jffs2_do_readpage_nolock(inode, pg);
	unlock_page(pg);
	return ret;
}


int jffs2_readpage (struct file *filp, struct page *pg)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(pg->mapping->host);
	int ret;
	
	down(&f->sem);
	ret = jffs2_do_readpage_unlock(pg->mapping->host, pg);
	up(&f->sem);
	return ret;
}

int jffs2_prepare_write (struct file *filp, struct page *pg, unsigned start, unsigned end)
{
	struct inode *inode = pg->mapping->host;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	uint32_t pageofs = pg->index << PAGE_CACHE_SHIFT;
	int ret = 0;

	D1(printk(KERN_DEBUG "jffs2_prepare_write()\n"));

	if (pageofs > inode->i_size) {
		/* Make new hole frag from old EOF to new page */
		struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
		struct jffs2_raw_inode ri;
		struct jffs2_full_dnode *fn;
		uint32_t phys_ofs, alloc_len;
		
		D1(printk(KERN_DEBUG "Writing new hole frag 0x%x-0x%x between current EOF and new page\n",
			  (unsigned int)inode->i_size, pageofs));

		ret = jffs2_reserve_space(c, sizeof(ri), &phys_ofs, &alloc_len, ALLOC_NORMAL);
		if (ret)
			return ret;

		down(&f->sem);
		memset(&ri, 0, sizeof(ri));

		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
		ri.totlen = cpu_to_je32(sizeof(ri));
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4));

		ri.ino = cpu_to_je32(f->inocache->ino);
		ri.version = cpu_to_je32(++f->highest_version);
		ri.mode = cpu_to_jemode(inode->i_mode);
		ri.uid = cpu_to_je16(inode->i_uid);
		ri.gid = cpu_to_je16(inode->i_gid);
		ri.isize = cpu_to_je32(max((uint32_t)inode->i_size, pageofs));
		ri.atime = ri.ctime = ri.mtime = cpu_to_je32(get_seconds());
		ri.offset = cpu_to_je32(inode->i_size);
		ri.dsize = cpu_to_je32(pageofs - inode->i_size);
		ri.csize = cpu_to_je32(0);
		ri.compr = JFFS2_COMPR_ZERO;
		ri.node_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
		ri.data_crc = cpu_to_je32(0);
		
		fn = jffs2_write_dnode(c, f, &ri, NULL, 0, phys_ofs, NULL);

		if (IS_ERR(fn)) {
			ret = PTR_ERR(fn);
			jffs2_complete_reservation(c);
			up(&f->sem);
			return ret;
		}
		ret = jffs2_add_full_dnode_to_inode(c, f, fn);
		if (f->metadata) {
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
		}
		if (ret) {
			D1(printk(KERN_DEBUG "Eep. add_full_dnode_to_inode() failed in prepare_write, returned %d\n", ret));
			jffs2_mark_node_obsolete(c, fn->raw);
			jffs2_free_full_dnode(fn);
			jffs2_complete_reservation(c);
			up(&f->sem);
			return ret;
		}
		jffs2_complete_reservation(c);
		inode->i_size = pageofs;
		up(&f->sem);
	}
	
	/* Read in the page if it wasn't already present, unless it's a whole page */
	if (!PageUptodate(pg) && (start || end < PAGE_CACHE_SIZE)) {
		down(&f->sem);
		ret = jffs2_do_readpage_nolock(inode, pg);
		up(&f->sem);
	}
	D1(printk(KERN_DEBUG "end prepare_write(). pg->flags %lx\n", pg->flags));
	return ret;
}

int jffs2_commit_write (struct file *filp, struct page *pg, unsigned start, unsigned end)
{
	/* Actually commit the write from the page cache page we're looking at.
	 * For now, we write the full page out each time. It sucks, but it's simple
	 */
	struct inode *inode = pg->mapping->host;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_raw_inode *ri;
	int ret = 0;
	uint32_t writtenlen = 0;

	D1(printk(KERN_DEBUG "jffs2_commit_write(): ino #%lu, page at 0x%lx, range %d-%d, flags %lx\n",
		  inode->i_ino, pg->index << PAGE_CACHE_SHIFT, start, end, pg->flags));

	if (!start && end == PAGE_CACHE_SIZE) {
		/* We need to avoid deadlock with page_cache_read() in
		   jffs2_garbage_collect_pass(). So we have to mark the
		   page up to date, to prevent page_cache_read() from 
		   trying to re-lock it. */
		SetPageUptodate(pg);
	}

	ri = jffs2_alloc_raw_inode();

	if (!ri) {
		D1(printk(KERN_DEBUG "jffs2_commit_write(): Allocation of raw inode failed\n"));
		return -ENOMEM;
	}

	/* Set the fields that the generic jffs2_write_inode_range() code can't find */
	ri->ino = cpu_to_je32(inode->i_ino);
	ri->mode = cpu_to_jemode(inode->i_mode);
	ri->uid = cpu_to_je16(inode->i_uid);
	ri->gid = cpu_to_je16(inode->i_gid);
	ri->isize = cpu_to_je32((uint32_t)inode->i_size);
	ri->atime = ri->ctime = ri->mtime = cpu_to_je32(get_seconds());

	/* In 2.4, it was already kmapped by generic_file_write(). Doesn't
	   hurt to do it again. The alternative is ifdefs, which are ugly. */
	kmap(pg);

	ret = jffs2_write_inode_range(c, f, ri, page_address(pg) + start,
				      (pg->index << PAGE_CACHE_SHIFT) + start,
				      end - start, &writtenlen);

	kunmap(pg);

	if (ret) {
		/* There was an error writing. */
		SetPageError(pg);
	}

	if (writtenlen) {
		if (inode->i_size < (pg->index << PAGE_CACHE_SHIFT) + start + writtenlen) {
			inode->i_size = (pg->index << PAGE_CACHE_SHIFT) + start + writtenlen;
			inode->i_blocks = (inode->i_size + 511) >> 9;
			
			inode->i_ctime = inode->i_mtime = ITIME(je32_to_cpu(ri->ctime));
		}
	}

	jffs2_free_raw_inode(ri);

	if (start+writtenlen < end) {
		/* generic_file_write has written more to the page cache than we've
		   actually written to the medium. Mark the page !Uptodate so that 
		   it gets reread */
		D1(printk(KERN_DEBUG "jffs2_commit_write(): Not all bytes written. Marking page !uptodate\n"));
		SetPageError(pg);
		ClearPageUptodate(pg);
	}

	D1(printk(KERN_DEBUG "jffs2_commit_write() returning %d\n",writtenlen?writtenlen:ret));
	return writtenlen?writtenlen:ret;
}
