/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: wbuf.c,v 1.7 2002/03/08 11:27:59 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/interrupt.h>
#include <linux/crc32.h>
#include "nodelist.h"

/* FIXME duplicated defines in wbuf.c and nand.c
 * Constants for out of band layout
 */
#define NAND_JFFS2_OOB_BADBPOS		5
#define NAND_JFFS2_OOB8_FSDAPOS		6
#define NAND_JFFS2_OOB16_FSDAPOS	8
#define NAND_JFFS2_OOB8_FSDALEN		2
#define NAND_JFFS2_OOB16_FSDALEN	8

#define MAX_ERASE_FAILURES 	5

static inline void jffs2_refile_wbuf_blocks(struct jffs2_sb_info *c)
{
	struct list_head *this, *next;

	if (list_empty(&c->erasable_pending_wbuf_list))
		return;

	list_for_each_safe(this, next, &c->erasable_pending_wbuf_list) {
		list_del(this);
		list_add_tail(this, &c->erasable_list);
	}
}

int jffs2_flush_wbuf(struct jffs2_sb_info *c, int pad)
{
	int ret;
	size_t retlen;

	if(!c->wbuf || !c->wbuf_len)
		return 0;

	/* claim remaining space on the page
	   this happens, if we have a change to a new block,
	   or if fsync forces us to flush the writebuffer.
	   if we have a switch to next page, we will not have
	   enough remaining space for this. 
	*/
	if (pad) {
		c->wbuf_len = PAD(c->wbuf_len);
		
		if ( c->wbuf_len + sizeof(struct jffs2_unknown_node) < c->wbuf_pagesize) {
			struct jffs2_unknown_node *padnode = (void *)(c->wbuf + c->wbuf_len);
			padnode->magic = JFFS2_MAGIC_BITMASK;
			padnode->nodetype = JFFS2_NODETYPE_PADDING;
			padnode->totlen = c->wbuf_pagesize - c->wbuf_len;
			padnode->hdr_crc = crc32(0, padnode, sizeof(*padnode)-4);
		}
	}
	/* else jffs2_flash_writev has actually filled in the rest of the
	   buffer for us, and will deal with the node refs etc. later. */
	
	ret = c->mtd->write(c->mtd, c->wbuf_ofs, c->wbuf_pagesize, &retlen, c->wbuf);
	
	if (ret || retlen != c->wbuf_pagesize) {
		if (ret)
			printk(KERN_CRIT "jffs2_flush_wbuf(): Write failed with %d\n",ret);
		else
			printk(KERN_CRIT "jffs2_flush_wbuf(): Write was short %d instead of %d\n",retlen,c->wbuf_pagesize);
			
		ret = -EIO;		
		/* CHECKME NAND 
		   So that the caller knows what happened. If
		   we were called from jffs2_flash_writev(), it'll
		   know to return failure and _its_ caller will
		   try again. writev gives back to jffs2_write_xxx 
		   in write.c. There are the real fixme's
		 */

		/*  FIXME NAND
		   If we were called from GC or fsync, there's no repair kit yet
		*/
		    
		return ret; 
	}

	/* Adjusting free size of next block only, if it's called from fsync ! */
	if (pad == 2) {
		D1(printk(KERN_DEBUG "jffs2_flush_wbuf() adjusting free_size of c->nextblock\n"));
		spin_lock_bh(&c->erase_completion_lock);
		if (!c->nextblock)
			BUG();
		if (c->nextblock->free_size < (c->wbuf_pagesize - c->wbuf_len))
			BUG();
		c->nextblock->free_size -= (c->wbuf_pagesize - c->wbuf_len);
		c->nextblock->dirty_size += (c->wbuf_pagesize - c->wbuf_len);
		spin_unlock_bh(&c->erase_completion_lock);
	}

	/* Stick any now-obsoleted blocks on the erase_pending_list */
	spin_lock_bh(&c->erase_completion_lock);
	jffs2_refile_wbuf_blocks(c);
	spin_unlock_bh(&c->erase_completion_lock);

	memset(c->wbuf,0xff,c->wbuf_pagesize);
	/* adjust write buffer offset, else we get a non contigous write bug */
	c->wbuf_ofs+= c->wbuf_pagesize;
	c->wbuf_len = 0;
	return 0;
}

#define PAGE_DIV(x) ( (x) & (~(c->wbuf_pagesize - 1)) )
#define PAGE_MOD(x) ( (x) & (c->wbuf_pagesize - 1) )
int jffs2_flash_writev(struct jffs2_sb_info *c, const struct iovec *invecs, unsigned long count, loff_t to, size_t *retlen)
{
	struct iovec outvecs[3];
	uint32_t totlen = 0;
	uint32_t split_ofs = 0;
	uint32_t old_totlen;
	int ret, splitvec = -1;
	int invec, outvec;
	size_t wbuf_retlen;
	unsigned char *wbuf_ptr;
	size_t donelen = 0;
	uint32_t outvec_to = to;

	/* If not NAND flash, don't bother */
	if (!c->wbuf)
		return jffs2_flash_direct_writev(c, invecs, count, to, retlen);
	
	/* If wbuf_ofs is not initialized, set it to target adress */
	if (c->wbuf_ofs == 0xFFFFFFFF) {
		c->wbuf_ofs = PAGE_DIV(to);
		c->wbuf_len = PAGE_MOD(to);			
		memset(c->wbuf,0xff,c->wbuf_pagesize);
	}

	/* Sanity checks on target address. 
	   It's permitted to write at PAD(c->wbuf_len+c->wbuf_ofs), 
	   and it's permitted to write at the beginning of a new 
	   erase block. Anything else, and you die.
	   New block starts at xxx000c (0-b = block header)
	*/
	if ( (to & ~(c->sector_size-1)) != (c->wbuf_ofs & ~(c->sector_size-1)) ) {
		/* It's a write to a new block */
		if (c->wbuf_len) {
			D1(printk(KERN_DEBUG "jffs2_flash_writev() to 0x%lx causes flush of wbuf at 0x%08x\n", (unsigned long)to, c->wbuf_ofs));
			ret = jffs2_flush_wbuf(c, 1);
			if (ret) {
				/* the underlying layer has to check wbuf_len to do the cleanup */
				D1(printk(KERN_WARNING "jffs2_flush_wbuf() called from jffs2_flash_writev() failed %d\n", ret));
				*retlen = 0;
				return ret;
			}
		}
		/* set pointer to new block */
		c->wbuf_ofs = PAGE_DIV(to);
		c->wbuf_len = PAGE_MOD(to);			
	} 

	if (to != PAD(c->wbuf_ofs + c->wbuf_len)) {
		/* We're not writing immediately after the writebuffer. Bad. */
		printk(KERN_CRIT "jffs2_flash_writev(): Non-contiguous write to %08lx\n", (unsigned long)to);
		if (c->wbuf_len)
			printk(KERN_CRIT "wbuf was previously %08x-%08x\n",
					  c->wbuf_ofs, c->wbuf_ofs+c->wbuf_len);
		BUG();
	}

	/* Note outvecs[3] above. We know count is never greater than 2 */
	if (count > 2) {
		printk(KERN_CRIT "jffs2_flash_writev(): count is %ld\n", count);
		BUG();
	}

	invec = 0;
	outvec = 0;


	/* Fill writebuffer first, if already in use */	
	if (c->wbuf_len) {
		uint32_t invec_ofs = 0;

		/* adjust alignment offset */ 
		if (c->wbuf_len != PAGE_MOD(to)) {
			c->wbuf_len = PAGE_MOD(to);
			/* take care of alignment to next page */
			if (!c->wbuf_len)
				c->wbuf_len = c->wbuf_pagesize;
		}
		
		while(c->wbuf_len < c->wbuf_pagesize) {
			uint32_t thislen;
			
			if (invec == count)
				goto alldone;

			thislen = c->wbuf_pagesize - c->wbuf_len;

			if (thislen >= invecs[invec].iov_len)
				thislen = invecs[invec].iov_len;
	
			invec_ofs = thislen;

			memcpy(c->wbuf + c->wbuf_len, invecs[invec].iov_base, thislen);
			c->wbuf_len += thislen;
			donelen += thislen;
			/* Get next invec, if actual did not fill the buffer */
			if (c->wbuf_len < c->wbuf_pagesize) 
				invec++;
		}			
		
		/* write buffer is full, flush buffer */
		ret = jffs2_flush_wbuf(c, 0);
		if (ret) {
			/* the underlying layer has to check wbuf_len to do the cleanup */
			D1(printk(KERN_WARNING "jffs2_flush_wbuf() called from jffs2_flash_writev() failed %d\n", ret));
			*retlen = 0;
			return ret;
		}
		outvec_to += donelen;
		c->wbuf_ofs = outvec_to;
		
		/* All invecs done ? */
		if (invec == count)
			goto alldone;

		/* Set up the first outvec, containing the remainder of the
		   invec we partially used */
		if (invecs[invec].iov_len > invec_ofs) {
			outvecs[0].iov_base = invecs[invec].iov_base+invec_ofs;
			totlen = outvecs[0].iov_len = invecs[invec].iov_len-invec_ofs;
			if (totlen > c->wbuf_pagesize) {
				splitvec = outvec;
				split_ofs = outvecs[0].iov_len - PAGE_MOD(totlen);
			}
			outvec++;
		}
		invec++;
	}

	/* OK, now we've flushed the wbuf and the start of the bits
	   we have been asked to write, now to write the rest.... */

	/* totlen holds the amount of data still to be written */
	old_totlen = totlen;
	for ( ; invec < count; invec++,outvec++ ) {
		outvecs[outvec].iov_base = invecs[invec].iov_base;
		totlen += outvecs[outvec].iov_len = invecs[invec].iov_len;
		if (PAGE_DIV(totlen) != PAGE_DIV(old_totlen)) {
			splitvec = outvec;
			split_ofs = outvecs[outvec].iov_len - PAGE_MOD(totlen);
			old_totlen = totlen;
		}
	}

	/* Now the outvecs array holds all the remaining data to write */
	/* Up to splitvec,split_ofs is to be written immediately. The rest
	   goes into the (now-empty) wbuf */

	if (splitvec != -1) {
		uint32_t remainder;
		int ret;

		remainder = outvecs[splitvec].iov_len - split_ofs;
		outvecs[splitvec].iov_len = split_ofs;

		/* We did cross a page boundary, so we write some now */
		ret = jffs2_flash_direct_writev(c, outvecs, splitvec+1, outvec_to, &wbuf_retlen); 
		if (ret < 0 || wbuf_retlen != PAGE_DIV(totlen)) {
			/* At this point we have no problem,
			   c->wbuf is empty. 
			*/
			*retlen = donelen;
			return ret;
		}
		
		donelen += wbuf_retlen;
		c->wbuf_ofs = PAGE_DIV(outvec_to) + PAGE_DIV(totlen);

		if (remainder) {
			outvecs[splitvec].iov_base += split_ofs;
			outvecs[splitvec].iov_len = remainder;
		} else {
			splitvec++;
		}

	} else {
		splitvec = 0;
	}

	/* Now splitvec points to the start of the bits we have to copy
	   into the wbuf */
	wbuf_ptr = c->wbuf;

	for ( ; splitvec < outvec; splitvec++) {
		/* Don't copy the wbuf into itself */
		if (outvecs[splitvec].iov_base == c->wbuf)
			continue;
		memcpy(wbuf_ptr, outvecs[splitvec].iov_base, outvecs[splitvec].iov_len);
		wbuf_ptr += outvecs[splitvec].iov_len;
		donelen += outvecs[splitvec].iov_len;
	}
	c->wbuf_len = wbuf_ptr - c->wbuf;

alldone:	
	*retlen = donelen;
	return 0;
}

/*
	This is the entry for NOR-Flash. We use it also for NAND to flush wbuf
*/
int jffs2_flash_write(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, const u_char *buf)
{
	return c->mtd->write(c->mtd, ofs, len, retlen, buf);
}

/*
	Handle readback from writebuffer and ECC failure return
*/
int jffs2_flash_read(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, u_char *buf)
{
	loff_t	orbf = 0, owbf = 0, lwbf = 0;
	int	ret;

	/* Read flash */
	ret = c->mtd->read(c->mtd, ofs, len, retlen, buf);

	if (!jffs2_can_mark_obsolete(c) && (ret == -EIO) && (*retlen == len) ) {
		printk(KERN_WARNING "mtd->read(0x%x bytes from 0x%llx) returned ECC error\n", len, ofs);
		/* 
		 * We have the raw data without ECC correction in the buffer, maybe 
		 * we are lucky and all data or parts are correct. We check the node.
		 * If data are corrupted node check will sort it out.
		 * We keep this block, it will fail on write or erase and the we
		 * mark it bad. Or should we do that now? But we should give him a chance.
		 * Maybe we had a system crash or power loss before the ecc write or  
		 * a erase was completed.
		 * So we return success. :)
		 */
		 ret = 0;	
	}

	/* if no writebuffer available or write buffer empty, return */
	if (!c->wbuf_pagesize || !c->wbuf_len)
		return ret;


	/* if we read in a different block, return */
	if ( (ofs & ~(c->sector_size-1)) != (c->wbuf_ofs & ~(c->sector_size-1)) ) 
		return ret;	

	if (ofs >= c->wbuf_ofs) {
		owbf = (ofs - c->wbuf_ofs);	/* offset in write buffer */
		if (owbf > c->wbuf_len)		/* is read beyond write buffer ? */
			return ret;
		lwbf = c->wbuf_len - owbf;	/* number of bytes to copy */
		if (lwbf > len)	
			lwbf = len;
	} else {	
		orbf = (c->wbuf_ofs - ofs);	/* offset in read buffer */
		if (orbf > len)			/* is write beyond write buffer ? */
			return ret;
		lwbf = len - orbf; 		/* number of bytes to copy */
		if (lwbf > c->wbuf_len)	
			lwbf = c->wbuf_len;
	}	
	if (lwbf > 0)
		memcpy(buf+orbf,c->wbuf+owbf,lwbf);

	return ret;
}

/*
 *	Check, if the out of band area is empty
 */
int jffs2_check_oob_empty( struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, int mode)
{
	unsigned char *buf;
	int 	ret = 0;
	int	i,len,cnt,page;
	size_t  retlen;
	int	fsdata_pos,badblock_pos,oob_size;

	oob_size = c->mtd->oobsize;

	switch(c->mtd->ecctype) {
	case MTD_ECC_SW:		
		fsdata_pos = (c->wbuf_pagesize == 256) ? NAND_JFFS2_OOB8_FSDAPOS : NAND_JFFS2_OOB16_FSDAPOS;
		badblock_pos = NAND_JFFS2_OOB_BADBPOS;
		break;
	default:
		D1(printk(KERN_WARNING "jffs2_write_oob_empty(): Invalid ECC type\n"));
		return -EINVAL;
	}	

	/* allocate a buffer for all oob data in this sector */
	len = oob_size * (c->sector_size/c->mtd->oobblock);
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		printk(KERN_NOTICE "jffs2_check_oob_empty(): allocation of temporary data buffer for oob check failed\n");
		return -ENOMEM;
	}
	/* 
	 * if mode = 0, we scan for a total empty oob area, else we have
	 * to take care of the cleanmarker in the first page of the block
	*/
	ret = jffs2_flash_read_oob(c, jeb->offset, len , &retlen, buf);
	if (ret) {
		D1(printk(KERN_WARNING "jffs2_check_oob_empty(): Read OOB failed %d for block at %08x\n", ret, jeb->offset));
		goto out;
	}
	
	if (retlen < len) {
		D1(printk(KERN_WARNING "jffs2_check_oob_empty(): Read OOB return short read "
			  "(%d bytes not %d) for block at %08x\n", retlen, len, jeb->offset));
		ret = -EIO;
		goto out;
	}
	
	/* Special check for first two pages */
	for (page = 0; page < 2; page += oob_size) {
		/* Check for bad block marker */
		if (buf[page+badblock_pos] != 0xff) {
			D1(printk(KERN_WARNING "jffs2_check_oob_empty(): Bad or failed block at %08x\n",jeb->offset));
			/* Return 2 for bad and 3 for failed block 
			   bad goes to list_bad and failed to list_erase */
			ret = (!page) ? 2 : 3;
			goto out;
		}
		cnt = oob_size;
		if (mode)
			cnt -= fsdata_pos;
		for(i = 0; i < cnt ; i+=sizeof(unsigned short)) {
			unsigned short dat = *(unsigned short *)(&buf[page+i]);
			if(dat != 0xffff) {
				ret = 1; 
				goto out;
			}
		}
		/* only the first page can contain a cleanmarker !*/
		mode = 0;
	}	

	/* we know, we are aligned :) */	
	for (; page < len; page += sizeof(long)) {
		unsigned long dat = *(unsigned long *)(&buf[page]);
		if(dat != -1) {
			ret = 1; 
			goto out;
		}
	}

out:
	kfree(buf);	
	
	return ret;
}

int jffs2_check_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	struct jffs2_unknown_node n;
	unsigned char buf[32];
	unsigned char *p;
	int ret,i;
	size_t retlen;
	int	fsdata_pos,fsdata_len, oob_size, badblock_pos;

	oob_size = c->mtd->oobsize;

	switch(c->mtd->ecctype) {
	case MTD_ECC_SW:	
		fsdata_pos = (c->wbuf_pagesize == 256) ? NAND_JFFS2_OOB8_FSDAPOS : NAND_JFFS2_OOB16_FSDAPOS;
		fsdata_len = (c->wbuf_pagesize == 256) ? NAND_JFFS2_OOB8_FSDALEN : NAND_JFFS2_OOB16_FSDALEN;
		badblock_pos = NAND_JFFS2_OOB_BADBPOS;
		break;
	default:
		D1(printk(KERN_WARNING "jffs2_write_nand_cleanmarker(): Invalid ECC type\n"));
		return -EINVAL;
	}	

	/*
	*	We read oob data from page 0 and 1 of the block.
	*	page 0 contains cleanmarker and badblock info
	*	page 2 contains failure count of this block
	*/
	ret = c->mtd->read_oob(c->mtd, jeb->offset, oob_size << 1 , &retlen, buf);
	
	if (ret) {
		D1(printk(KERN_WARNING "jffs2_check_nand_cleanmarker(): Read OOB failed %d for block at %08x\n", ret, jeb->offset));
		return ret;
	}
	if (retlen < (oob_size << 1) ) {
		D1(printk(KERN_WARNING "jffs2_check_nand_cleanmarker(): Read OOB return short read (%d bytes not %d) for block at %08x\n", retlen, oob_size << 1 , jeb->offset));
		return -EIO;
	}

	/* Check for bad block marker */
	if (buf[badblock_pos] != 0xff) {
		D1(printk(KERN_WARNING "jffs2_check_nand_cleanmarker(): Bad block at %08x\n",jeb->offset));
		return 2;
	}

	/* Check for failure counter in the second page */
	if (buf[badblock_pos+oob_size] != 0xff) {
		D1(printk(KERN_WARNING "jffs2_check_nand_cleanmarker(): Block marked as failed at %08x, fail count:%d\n",jeb->offset,buf[badblock_pos+oob_size]));
		return 3;
	}

	n.magic = JFFS2_MAGIC_BITMASK;
	n.nodetype = JFFS2_NODETYPE_CLEANMARKER;
	n.totlen = 8;
	p = (unsigned char *) &n;
	
	for (i = 0; i < fsdata_len; i++) {
		if (buf[fsdata_pos+i] != p[i]) {
			D2(printk(KERN_WARNING "jffs2_check_nand_cleanmarker(): Cleanmarker node not detected in block at %08x\n", jeb->offset));
			return 1;
		}
	}
	
	return 0;
}

int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	struct 	jffs2_unknown_node n;
	int 	ret;
	int	fsdata_pos,fsdata_len;
	size_t 	retlen;

	switch(c->mtd->ecctype) {
	case MTD_ECC_SW:	
		fsdata_pos = (c->wbuf_pagesize == 256) ? NAND_JFFS2_OOB8_FSDAPOS : NAND_JFFS2_OOB16_FSDAPOS;
		fsdata_len = (c->wbuf_pagesize == 256) ? NAND_JFFS2_OOB8_FSDALEN : NAND_JFFS2_OOB16_FSDALEN;
		break;
	default:
		D1(printk(KERN_WARNING "jffs2_write_nand_cleanmarker(): Invalid ECC type\n"));
		return -EINVAL;
	}	
	
	n.magic = JFFS2_MAGIC_BITMASK;
	n.nodetype = JFFS2_NODETYPE_CLEANMARKER;
	n.totlen = 8;

	ret = jffs2_flash_write_oob(c, jeb->offset + fsdata_pos, fsdata_len, &retlen, (unsigned char *)&n);
	
	if (ret) {
		D1(printk(KERN_WARNING "jffs2_write_nand_cleanmarker(): Write failed for block at %08x: error %d\n", jeb->offset, ret));
		return ret;
	}
	if (retlen != fsdata_len) {
		D1(printk(KERN_WARNING "jffs2_write_nand_cleanmarker(): Short write for block at %08x: %d not %d\n", jeb->offset, retlen, fsdata_len));
		return ret;
	}
	return 0;
}

/* 
 * We try to get the failure count of this block.
 */
int jffs2_nand_read_failcnt(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb) {

	unsigned char buf[16];
	int	ret;
	size_t 	retlen;
	int	oob_size, badblock_pos;

	oob_size = c->mtd->oobsize;

	switch(c->mtd->ecctype) {
	case MTD_ECC_SW:	
		badblock_pos = NAND_JFFS2_OOB_BADBPOS;
		break;
	default:
		D1(printk(KERN_WARNING "jffs2_nand_read_failcnt(): Invalid ECC type\n"));
		return -EINVAL;
	}	
	
	ret = c->mtd->read_oob(c->mtd, jeb->offset + c->mtd->oobblock, oob_size , &retlen, buf);
	
	if (ret) {
		D1(printk(KERN_WARNING "jffs2_nand_read_failcnt(): Read OOB failed %d for block at %08x\n", ret, jeb->offset));
		return ret;
	}

	if (retlen < oob_size) {
		D1(printk(KERN_WARNING "jffs2_nand_read_failcnt(): Read OOB return short read (%d bytes not %d) for block at %08x\n", retlen, oob_size, jeb->offset));
		return -EIO;
	}

	jeb->bad_count =  buf[badblock_pos];	
	return 0;
}

/* 
 * On NAND we try to mark this block bad. We try to write how often
 * the block was erased and mark it finaly bad, if the count
 * is > MAX_ERASE_FAILURES. We read this information on mount !
 * jeb->bad_count contains the count before this erase.
 * Don't care about failures. This block remains on the erase-pending
 * or badblock list as long as nobody manipulates the flash with
 * a bootloader or something like that.
 */

int jffs2_write_nand_badblock(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	unsigned char buf = 0x0;
	int 	ret,pos;
	size_t 	retlen;

	switch(c->mtd->ecctype) {
	case MTD_ECC_SW:	
		pos = NAND_JFFS2_OOB_BADBPOS;
		break;
	default:
		D1(printk(KERN_WARNING "jffs2_write_nand_badblock(): Invalid ECC type\n"));
		return -EINVAL;
	}	

	/* if the count is < max, we try to write the counter to the 2nd page oob area */
	if( ++jeb->bad_count < MAX_ERASE_FAILURES) {
		buf = (unsigned char)jeb->bad_count;
		pos += c->mtd->oobblock;
	}
	
	ret = jffs2_flash_write_oob(c, jeb->offset + pos, 1, &retlen, &buf);
	
	if (ret) {
		D1(printk(KERN_WARNING "jffs2_write_nand_badblock(): Write failed for block at %08x: error %d\n", jeb->offset, ret));
		return ret;
	}
	if (retlen != 1) {
		D1(printk(KERN_WARNING "jffs2_write_nand_badblock(): Short write for block at %08x: %d not 1\n", jeb->offset, retlen));
		return ret;
	}
	return 0;
}

