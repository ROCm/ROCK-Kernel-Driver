/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: read.c,v 1.23 2002/05/20 14:56:38 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include "nodelist.h"

int jffs2_read_dnode(struct jffs2_sb_info *c, struct jffs2_full_dnode *fd, unsigned char *buf, int ofs, int len)
{
	struct jffs2_raw_inode *ri;
	size_t readlen;
	uint32_t crc;
	unsigned char *decomprbuf = NULL;
	unsigned char *readbuf = NULL;
	int ret = 0;

	ri = jffs2_alloc_raw_inode();
	if (!ri)
		return -ENOMEM;

	ret = jffs2_flash_read(c, fd->raw->flash_offset & ~3, sizeof(*ri), &readlen, (char *)ri);
	if (ret) {
		jffs2_free_raw_inode(ri);
		printk(KERN_WARNING "Error reading node from 0x%08x: %d\n", fd->raw->flash_offset & ~3, ret);
		return ret;
	}
	if (readlen != sizeof(*ri)) {
		jffs2_free_raw_inode(ri);
		printk(KERN_WARNING "Short read from 0x%08x: wanted 0x%x bytes, got 0x%x\n", 
		       fd->raw->flash_offset & ~3, sizeof(*ri), readlen);
		return -EIO;
	}
	crc = crc32(0, ri, sizeof(*ri)-8);

	D1(printk(KERN_DEBUG "Node read from %08x: node_crc %08x, calculated CRC %08x. dsize %x, csize %x, offset %x, buf %p\n", fd->raw->flash_offset & ~3, ri->node_crc, crc, ri->dsize, ri->csize, ri->offset, buf));
	if (crc != ri->node_crc) {
		printk(KERN_WARNING "Node CRC %08x != calculated CRC %08x for node at %08x\n", ri->node_crc, crc, fd->raw->flash_offset & ~3);
		ret = -EIO;
		goto out_ri;
	}
	/* There was a bug where we wrote hole nodes out with csize/dsize
	   swapped. Deal with it */
	if (ri->compr == JFFS2_COMPR_ZERO && !ri->dsize && ri->csize) {
		ri->dsize = ri->csize;
		ri->csize = 0;
	}

	D1(if(ofs + len > ri->dsize) {
		printk(KERN_WARNING "jffs2_read_dnode() asked for %d bytes at %d from %d-byte node\n", len, ofs, ri->dsize);
		ret = -EINVAL;
		goto out_ri;
	});

	
	if (ri->compr == JFFS2_COMPR_ZERO) {
		memset(buf, 0, len);
		goto out_ri;
	}

	/* Cases:
	   Reading whole node and it's uncompressed - read directly to buffer provided, check CRC.
	   Reading whole node and it's compressed - read into comprbuf, check CRC and decompress to buffer provided 
	   Reading partial node and it's uncompressed - read into readbuf, check CRC, and copy 
	   Reading partial node and it's compressed - read into readbuf, check checksum, decompress to decomprbuf and copy
	*/
	if (ri->compr == JFFS2_COMPR_NONE && len == ri->dsize) {
		readbuf = buf;
	} else {
		readbuf = kmalloc(ri->csize, GFP_KERNEL);
		if (!readbuf) {
			ret = -ENOMEM;
			goto out_ri;
		}
	}
	if (ri->compr != JFFS2_COMPR_NONE) {
		if (len < ri->dsize) {
			decomprbuf = kmalloc(ri->dsize, GFP_KERNEL);
			if (!decomprbuf) {
				ret = -ENOMEM;
				goto out_readbuf;
			}
		} else {
			decomprbuf = buf;
		}
	} else {
		decomprbuf = readbuf;
	}

	D2(printk(KERN_DEBUG "Read %d bytes to %p\n", ri->csize, readbuf));
	ret = jffs2_flash_read(c, (fd->raw->flash_offset &~3) + sizeof(*ri), ri->csize, &readlen, readbuf);

	if (!ret && readlen != ri->csize)
		ret = -EIO;
	if (ret)
		goto out_decomprbuf;

	crc = crc32(0, readbuf, ri->csize);
	if (crc != ri->data_crc) {
		printk(KERN_WARNING "Data CRC %08x != calculated CRC %08x for node at %08x\n", ri->data_crc, crc, fd->raw->flash_offset & ~3);
		ret = -EIO;
		goto out_decomprbuf;
	}
	D2(printk(KERN_DEBUG "Data CRC matches calculated CRC %08x\n", crc));
	if (ri->compr != JFFS2_COMPR_NONE) {
		D2(printk(KERN_DEBUG "Decompress %d bytes from %p to %d bytes at %p\n", ri->csize, readbuf, ri->dsize, decomprbuf)); 
		ret = jffs2_decompress(ri->compr, readbuf, decomprbuf, ri->csize, ri->dsize);
		if (ret) {
			printk(KERN_WARNING "Error: jffs2_decompress returned %d\n", ret);
			goto out_decomprbuf;
		}
	}

	if (len < ri->dsize) {
		memcpy(buf, decomprbuf+ofs, len);
	}
 out_decomprbuf:
	if(decomprbuf != buf && decomprbuf != readbuf)
		kfree(decomprbuf);
 out_readbuf:
	if(readbuf != buf)
		kfree(readbuf);
 out_ri:
	jffs2_free_raw_inode(ri);

	return ret;
}

int jffs2_read_inode_range(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			   unsigned char *buf, uint32_t offset, uint32_t len)
{
	uint32_t end = offset + len;
	struct jffs2_node_frag *frag = f->fraglist;
	int ret;

	D1(printk(KERN_DEBUG "jffs2_read_inode_range: ino #%u, range 0x%08x-0x%08x\n",
		  f->inocache->ino, offset, offset+len));

	while(frag && frag->ofs + frag->size  <= offset) {
		D2(printk(KERN_DEBUG "skipping frag %d-%d; before the region we care about\n", frag->ofs, frag->ofs + frag->size));
		frag = frag->next;
	}
	/* XXX FIXME: Where a single physical node actually shows up in two
	   frags, we read it twice. Don't do that. */
	/* Now we're pointing at the first frag which overlaps our page */
	while(offset < end) {
		D2(printk(KERN_DEBUG "jffs2_read_inode_range: offset %d, end %d\n", offset, end));
		if (!frag || frag->ofs > offset) {
			uint32_t holesize = end - offset;
			if (frag) {
				D1(printk(KERN_NOTICE "Eep. Hole in ino #%u fraglist. frag->ofs = 0x%08x, offset = 0x%08x\n", f->inocache->ino, frag->ofs, offset));
				holesize = min(holesize, frag->ofs - offset);
				D1(jffs2_print_frag_list(f));
			}
			D1(printk(KERN_DEBUG "Filling non-frag hole from %d-%d\n", offset, offset+holesize));
			memset(buf, 0, holesize);
			buf += holesize;
			offset += holesize;
			continue;
		} else if (frag->ofs < offset && (offset & (PAGE_CACHE_SIZE-1)) != 0) {
			D1(printk(KERN_NOTICE "Eep. Overlap in ino #%u fraglist. frag->ofs = 0x%08x, offset = 0x%08x\n",
				  f->inocache->ino, frag->ofs, offset));
			D1(jffs2_print_frag_list(f));
			memset(buf, 0, end - offset);
			return -EIO;
		} else if (!frag->node) {
			uint32_t holeend = min(end, frag->ofs + frag->size);
			D1(printk(KERN_DEBUG "Filling frag hole from %d-%d (frag 0x%x 0x%x)\n", offset, holeend, frag->ofs, frag->ofs + frag->size));
			memset(buf, 0, holeend - offset);
			buf += holeend - offset;
			offset = holeend;
			frag = frag->next;
			continue;
		} else {
			uint32_t readlen;
			readlen = min(frag->size, end - offset);
			D1(printk(KERN_DEBUG "Reading %d-%d from node at 0x%x\n", frag->ofs, frag->ofs+readlen, frag->node->raw->flash_offset & ~3));
			ret = jffs2_read_dnode(c, frag->node, buf, frag->ofs - frag->node->ofs, readlen);
			D2(printk(KERN_DEBUG "node read done\n"));
			if (ret) {
				D1(printk(KERN_DEBUG"jffs2_read_inode_range error %d\n",ret));
				memset(buf, 0, frag->size);
				return ret;
			}
		}
		buf += frag->size;
		offset += frag->size;
		frag = frag->next;
		D2(printk(KERN_DEBUG "node read was OK. Looping\n"));
	}
	return 0;
}

/* Core function to read symlink target. */
char *jffs2_getlink(struct jffs2_sb_info *c, struct jffs2_inode_info *f)
{
	char *buf;
	int ret;

	down(&f->sem);

	if (!f->metadata) {
		printk(KERN_NOTICE "No metadata for symlink inode #%u\n", f->inocache->ino);
		up(&f->sem);
		return ERR_PTR(-EINVAL);
	}
	buf = kmalloc(f->metadata->size+1, GFP_USER);
	if (!buf) {
		up(&f->sem);
		return ERR_PTR(-ENOMEM);
	}
	buf[f->metadata->size]=0;

	ret = jffs2_read_dnode(c, f->metadata, buf, 0, f->metadata->size);

	up(&f->sem);

	if (ret) {
		kfree(buf);
		return ERR_PTR(ret);
	}
	return buf;
}
