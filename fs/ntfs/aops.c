/*
 * aops.c - NTFS kernel address space operations and page cache handling.
 * 	    Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 * Copyright (C) 2002 Richard Russon.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/locks.h>

#include "ntfs.h"

/**
 * ntfs_file_get_block - read/create inode @ino block @blk into buffer head @bh
 * @ino:	inode to read/create block from/onto
 * @blk:	block number to read/create
 * @bh:		buffer in which to return the read/created block
 * @create:	if not zero, create the block if it doesn't exist already
 * 
 * ntfs_file_get_block() remaps the block number @blk of the inode @ino from
 * file offset into disk block position and returns the result in the buffer
 * head @bh. If the block doesn't exist and create is not zero,
 * ntfs_file_get_block() creates the block before returning it. @blk is the
 * file offset divided by the file system block size, as defined by the field
 * s_blocksize in the super block reachable by @ino->i_sb.
 *
 * If the block doesn't exist, create is true, and the inode is marked
 * for synchronous I/O, then we will wait for creation to complete before
 * returning the created block (which will be zeroed). Otherwise we only
 * schedule creation and return. - FIXME: Need to have a think whether this is
 * really necessary. What would happen if we didn't actually write the block to
 * disk at this stage? We would just save writing a block full of zeroes to the
 * device. - We can always write it synchronously when the user actually writes
 * some data into it. - But this might result in random data being returned
 * should the computer crash. - Hmmm. - This requires more thought.
 *
 * Obviously the block is only created if the file system super block flag
 * MS_RDONLY is not set and only if NTFS write support is compiled in.
 */
int ntfs_file_get_block(struct inode *vi, const sector_t blk,
		struct buffer_head *bh, const int create)
{
	ntfs_inode *ni = NTFS_I(vi);
	ntfs_volume *vol = ni->vol;
	VCN vcn;
	LCN lcn;
	int ofs;
	BOOL is_retry = FALSE;

	//ntfs_debug("Entering for blk 0x%lx.", blk);
	//printk(KERN_DEBUG "NTFS: " __FUNCTION__ "(): Entering for blk "
	//		"0x%lx.\n", blk);

	bh->b_dev = vi->i_dev;
	bh->b_blocknr = -1;
	bh->b_state &= ~(1UL << BH_Mapped);

	/* Convert @blk into a virtual cluster number (vcn) and offset. */
	vcn = (VCN)blk << vol->sb->s_blocksize_bits >> vol->cluster_size_bits;
	ofs = ((VCN)blk << vol->sb->s_blocksize_bits) & vol->cluster_size_mask;

	/* Check for initialized size overflow. */
	if ((vcn << vol->cluster_size_bits) + ofs >= ni->initialized_size)
		return 0;
	/*
	 * Further, we need to be checking i_size and be just doing the
	 * following if it is zero or we are out of bounds:
	 * 	bh->b_blocknr = -1UL;
	 * 	raturn 0;
	 * Also, we need to deal with attr->initialized_size.
	 * Also, we need to deal with the case where the last block is
	 * requested but it is not initialized fully, i.e. it is a partial
	 * block. We then need to read it synchronously and fill the remainder
	 * with zero. Can't do it other way round as reading from the block
	 * device would result in our pre-zeroed data to be overwritten as the
	 * whole block is loaded from disk.
	 * Also, need to lock run_list in inode so we don't have someone
	 * reading it at the same time as someone else writing it.
	 */

retry_remap:

	/* Convert the vcn to the corresponding logical cluster number (lcn). */
	down_read(&ni->run_list.lock);
	lcn = vcn_to_lcn(ni->run_list.rl, vcn);
	up_read(&ni->run_list.lock);
	/* Successful remap. */
	if (lcn >= 0) {
		/* Setup the buffer head to describe the correct block. */
#if 0
		/* Already the case when we are called. */
		bh->b_dev = vfs_ino->i_dev;
#endif
		bh->b_blocknr = ((lcn << vol->cluster_size_bits) + ofs) >>
				vol->sb->s_blocksize_bits;
		bh->b_state |= (1UL << BH_Mapped);
		return 0;
	}
	/* It is a hole. */
	if (lcn == LCN_HOLE) {
		if (create)
			/* FIXME: We should instantiate the hole. */
			return -EROFS;
		/*
		 * Hole. Set the block number to -1 (it is ignored but
		 * just in case and might help with debugging).
		 */
		bh->b_blocknr = -1UL;
		bh->b_state &= ~(1UL << BH_Mapped);
		return 0;
	}
	/* If on first try and the run list was not mapped, map it and retry. */
	if (!is_retry && lcn == LCN_RL_NOT_MAPPED) {
		int err = map_run_list(ni, vcn);
		if (!err) {
			is_retry = TRUE;
			goto retry_remap;
		}
		return err;
	}
	if (create)
		/* FIXME: We might need to extend the attribute. */
		return -EROFS;
	/* Error. */
	return -EIO;

}

/**
 * ntfs_file_readpage - fill a @page of a @file with data from the device
 * @file:	open file to which the page @page belongs or NULL
 * @page:	page cache page to fill with data
 *
 * For non-resident attributes, ntfs_file_readpage() fills the @page of the open
 * file @file by calling the generic block_read_full_page() function provided by
 * the kernel which in turn invokes our ntfs_file_get_block() callback in order
 * to create and read in the buffers associated with the page asynchronously.
 *
 * For resident attributes, OTOH, ntfs_file_readpage() fills @page by copying
 * the data from the mft record (which at this stage is most likely in memory)
 * and fills the remainder with zeroes. Thus, in this case I/O is synchronous,
 * as even if the mft record is not cached at this point in time, we need to
 * wait for it to be read in before we can do the copy.
 *
 * Return zero on success or -errno on error.
 */
static int ntfs_file_readpage(struct file *file, struct page *page)
{
	s64 attr_pos;
	struct inode *vi;
	ntfs_inode *ni;
	char *page_addr;
	u32 attr_len;
	int err = 0;
	attr_search_context *ctx;
	MFT_RECORD *mrec;

	//ntfs_debug("Entering for index 0x%lx.", page->index);
	/* The page must be locked. */
	if (!PageLocked(page))
		PAGE_BUG(page);
	/*
	 * Get the VFS and ntfs inodes associated with the page. This could
	 * be achieved by looking at f->f_dentry->d_inode, too, unless the
	 * dentry is negative, but could it really be negative considering we
	 * are reading from the opened file? - NOTE: We can't get it from file,
	 * because we can use ntfs_file_readpage on inodes not representing
	 * open files!!! So basically we never ever touch file or at least we
	 * must check it is not NULL before doing so.
	 */
	vi = page->mapping->host;
	ni = NTFS_I(vi);

	/* Is the unnamed $DATA attribute resident? */
	if (test_bit(NI_NonResident, &ni->state)) {
		/* Attribute is not resident. */

		/* If the file is encrypted, we deny access, just like NT4. */
		if (test_bit(NI_Encrypted, &ni->state)) {
			err = -EACCES;
			goto unl_err_out;
		}
		if (!test_bit(NI_Compressed, &ni->state))
			/* Normal data stream, use generic functionality. */
			return block_read_full_page(page, ntfs_file_get_block);
		/* Compressed data stream. Handled in compress.c. */
		return ntfs_file_read_compressed_block(page);
	}
	/* Attribute is resident, implying it is not compressed or encrypted. */

	/*
	 * Make sure the inode doesn't disappear under us. - Shouldn't be
	 * needed as the page is locked.
	 */
	// atomic_inc(&vfs_ino->i_count);

	/* Map, pin and lock the mft record for reading. */
	mrec = map_mft_record(READ, ni);
	if (IS_ERR(mrec)) {
		err = PTR_ERR(mrec);
		goto dec_unl_err_out;
	}

	err = get_attr_search_ctx(&ctx, ni, mrec);
	if (err)
		goto unm_dec_unl_err_out;

	/* Find the data attribute in the mft record. */
	if (!lookup_attr(AT_DATA, NULL, 0, 0, 0, NULL, 0, ctx)) {
		err = -ENOENT;
		goto put_unm_dec_unl_err_out;
	}

	/* Starting position of the page within the attribute value. */
	attr_pos = page->index << PAGE_CACHE_SHIFT;

	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(ctx->attr->_ARA(value_length));

	/* Map the page so we can access it. */
	page_addr = kmap(page);
	/*
	 * TODO: Find out whether we really need to zero the page. If it is
	 * initialized to zero already we could skip this.
	 */
	/* 
	 * If we are asking for any in bounds data, copy it over, zeroing the
	 * remainder of the page if necessary. Otherwise just zero the page.
	 */
	if (attr_pos < attr_len) {
		u32 bytes = attr_len - attr_pos;
		if (bytes > PAGE_CACHE_SIZE)
			bytes = PAGE_CACHE_SIZE;
		else if (bytes < PAGE_CACHE_SIZE)
			memset(page_addr + bytes, 0, PAGE_CACHE_SIZE - bytes);
		/* Copy the data to the page. */
		memcpy(page_addr, attr_pos + (char*)ctx->attr +
				le16_to_cpu(ctx->attr->_ARA(value_offset)), bytes);
	} else
		memset(page_addr, 0, PAGE_CACHE_SIZE);
	kunmap(page);
	/* We are done. */
	SetPageUptodate(page);
put_unm_dec_unl_err_out:
	put_attr_search_ctx(ctx);
unm_dec_unl_err_out:
	/* Unlock, unpin and release the mft record. */
	unmap_mft_record(READ, ni);
dec_unl_err_out:
	/* Release the inode. - Shouldn't be needed as the page is locked. */
	// atomic_dec(&vfs_ino->i_count);
unl_err_out:
	UnlockPage(page);
	return err;
}

/*
 * Specialized get block for reading the mft bitmap. Adapted from
 * ntfs_file_get_block.
 */
static int ntfs_mftbmp_get_block(ntfs_volume *vol, const sector_t blk,
		struct buffer_head *bh)
{
	VCN vcn = (VCN)blk << vol->sb->s_blocksize_bits >>
			vol->cluster_size_bits;
	int ofs = (blk << vol->sb->s_blocksize_bits) &
			vol->cluster_size_mask;
	LCN lcn;

	ntfs_debug("Entering for blk = 0x%lx, vcn = 0x%Lx, ofs = 0x%x.",
			blk, (long long)vcn, ofs);
	bh->b_dev = vol->mft_ino->i_dev;
	bh->b_state &= ~(1UL << BH_Mapped);
	bh->b_blocknr = -1;
	/* Check for initialized size overflow. */
	if ((vcn << vol->cluster_size_bits) + ofs >=
			vol->mftbmp_initialized_size) {
		ntfs_debug("Done.");
		return 0;
	}
	down_read(&vol->mftbmp_rl.lock);
	lcn = vcn_to_lcn(vol->mftbmp_rl.rl, vcn);
	up_read(&vol->mftbmp_rl.lock);
	ntfs_debug("lcn = 0x%Lx.", (long long)lcn);
	if (lcn < 0LL) {
		ntfs_error(vol->sb, "Returning -EIO, lcn = 0x%Lx.",
				(long long)lcn);
		return -EIO;
	}
	/* Setup the buffer head to describe the correct block. */
	bh->b_blocknr = ((lcn << vol->cluster_size_bits) + ofs) >>
			vol->sb->s_blocksize_bits;
	bh->b_state |= (1UL << BH_Mapped);
	ntfs_debug("Done, bh->b_blocknr = 0x%lx.", bh->b_blocknr);
	return 0;
}

#define MAX_BUF_PER_PAGE (PAGE_CACHE_SIZE / 512)

/*
 * Specialized readpage for accessing mft bitmap. Adapted from
 * block_read_full_page().
 */
static int ntfs_mftbmp_readpage(ntfs_volume *vol, struct page *page)
{
	sector_t iblock, lblock;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	unsigned int blocksize, blocks;
	int nr, i;
	unsigned char blocksize_bits;

	ntfs_debug("Entering for index 0x%lx.", page->index);
	if (!PageLocked(page))
		PAGE_BUG(page);
	blocksize = vol->sb->s_blocksize;
	blocksize_bits = vol->sb->s_blocksize_bits;
	if (!page->buffers)
		create_empty_buffers(page, blocksize);
	head = page->buffers;
	if (!head) {
		ntfs_error(vol->sb, "Creation of empty buffers failed, cannot "
				"read page.");
		return -EINVAL;
	}
	blocks = PAGE_CACHE_SIZE >> blocksize_bits;
	iblock = page->index << (PAGE_CACHE_SHIFT - blocksize_bits);
	lblock = (((vol->_VMM(nr_mft_records) + 7) >> 3) + blocksize - 1) >>
			blocksize_bits;
	ntfs_debug("blocks = 0x%x, iblock = 0x%lx, lblock = 0x%lx.", blocks,
			iblock, lblock);
	bh = head;
	nr = i = 0;
	do {
		ntfs_debug("In do loop, i = 0x%x, iblock = 0x%lx.", i,
				iblock);
		if (buffer_uptodate(bh)) {
			ntfs_debug("Buffer is already uptodate.");
			continue;
		}
		if (!buffer_mapped(bh)) {
			if (iblock < lblock) {
				if (ntfs_mftbmp_get_block(vol, iblock, bh))
					continue;
			}
			if (!buffer_mapped(bh)) {
				ntfs_debug("Buffer is not mapped, setting "
						"uptodate.");
				memset(kmap(page) + i*blocksize, 0, blocksize);
				flush_dcache_page(page);
				kunmap(page);
				set_bit(BH_Uptodate, &bh->b_state);
				continue;
			}
			/*
			 * ntfs_mftbmp_get_block() might have updated the
			 * buffer synchronously.
			 */
			if (buffer_uptodate(bh)) {
				ntfs_debug("Buffer is now uptodate.");
				continue;
			}
		}
		arr[nr++] = bh;
	} while (i++, iblock++, (bh = bh->b_this_page) != head);
	ntfs_debug("After do loop, i = 0x%x, iblock = 0x%lx, nr = 0x%x.", i,
			iblock, nr);
	if (!nr) {
		/* All buffers are uptodate - set the page uptodate as well. */
		ntfs_debug("All buffers are uptodate, returning 0.");
		SetPageUptodate(page);
		UnlockPage(page);
		return 0;
	}
	/* Stage two: lock the buffers */
	ntfs_debug("Locking buffers.");
	for (i = 0; i < nr; i++) {
		struct buffer_head *bh = arr[i];
		lock_buffer(bh);
		set_buffer_async_io(bh);
	}
	/* Stage 3: start the IO */
	ntfs_debug("Starting IO on buffers.");
	for (i = 0; i < nr; i++)
		submit_bh(READ, arr[i]);
	ntfs_debug("Done.");
	return 0;
}

/**
 * end_buffer_read_index_async - async io completion for reading index records
 * @bh:		buffer head on which io is completed
 * @uptodate:	whether @bh is now uptodate or not
 *
 * Asynchronous I/O completion handler for reading pages belogning to the
 * index allocation attribute address space of directory inodes.
 *
 * Perform the post read mst fixups when all IO on the page has been completed
 * and marks the page uptodate or sets the error bit on the page.
 *
 * Adapted from fs/buffer.c.
 *
 * NOTE: We use this function as async io completion handler for reading pages
 * belonging to the mft data attribute address space, too as this saves
 * duplicating an almost identical function. We do this by cheating a little
 * bit in setting the index_block_size in the mft ntfs_inode to the mft record
 * size of the volume (vol->mft_record_size), and index_block_size_bits to
 * mft_record_size_bits, respectively.
 */
void end_buffer_read_index_async(struct buffer_head *bh, int uptodate)
{
	static spinlock_t page_uptodate_lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;
	struct buffer_head *tmp;
	struct page *page;

	mark_buffer_uptodate(bh, uptodate);

	/* This is a temporary buffer used for page I/O. */
	page = bh->b_page;
	if (!uptodate)
		SetPageError(page);
	/*
	 * Be _very_ careful from here on. Bad things can happen if
	 * two buffer heads end IO at almost the same time and both
	 * decide that the page is now completely done.
	 *
	 * Async buffer_heads are here only as labels for IO, and get
	 * thrown away once the IO for this page is complete.  IO is
	 * deemed complete once all buffers have been visited
	 * (b_count==0) and are now unlocked. We must make sure that
	 * only the _last_ buffer that decrements its count is the one
	 * that unlock the page..
	 */
	spin_lock_irqsave(&page_uptodate_lock, flags);
	mark_buffer_async(bh, 0);
	unlock_buffer(bh);

	tmp = bh->b_this_page;
	while (tmp != bh) {
		if (buffer_async(tmp) && buffer_locked(tmp))
			goto still_busy;
		tmp = tmp->b_this_page;
	}
	/* OK, the async IO on this page is complete. */
	spin_unlock_irqrestore(&page_uptodate_lock, flags);
	/*
	 * If none of the buffers had errors then we can set the page uptodate,
	 * but we first have to perform the post read mst fixups.
	 */
	if (!PageError(page)) {
		char *addr;
		unsigned int i, recs, nr_err = 0;
		u32 rec_size;
		ntfs_inode *ni = NTFS_I(page->mapping->host);

		addr = kmap_atomic(page, KM_BIO_IRQ);
		rec_size = ni->_IDM(index_block_size);
		recs = PAGE_CACHE_SIZE / rec_size;
		for (i = 0; i < recs; i++) {
			if (!post_read_mst_fixup((NTFS_RECORD*)(addr +
					i * rec_size), rec_size))
				continue;
			nr_err++;
			ntfs_error(ni->vol->sb, "post_read_mst_fixup() failed, "
					"corrupt %s record 0x%Lx. Run chkdsk.",
					ni->mft_no ? "index" : "mft",
					(long long)((page->index <<
					PAGE_CACHE_SHIFT >>
					ni->_IDM(index_block_size_bits)) + i));
		}
		kunmap_atomic(addr, KM_BIO_IRQ);
		if (!nr_err && recs)
			SetPageUptodate(page);
		else {
			ntfs_error(ni->vol->sb, "Setting page error, index "
					"0x%lx.", page->index);
			SetPageError(page);
		}
	}
	UnlockPage(page);
	return;
still_busy:
	spin_unlock_irqrestore(&page_uptodate_lock, flags);
	return;
}

/**
 * ntfs_dir_readpage - fill a @page of a directory with data from the device
 * @dir:	open directory to which the page @page belongs
 * @page:	page cache page to fill with data
 *
 * Fill the page @page of the open directory @dir. We read each buffer
 * asynchronously and when all buffers are read in our io completion
 * handler end_buffer_read_index_block_async() automatically applies the mst
 * fixups to the page before finally marking it uptodate and unlocking it.
 *
 * Contains an adapted version of fs/buffer.c::block_read_full_page(), a
 * generic "read page" function for block devices that have the normal
 * get_block functionality. This is most of the block device filesystems.
 * Reads the page asynchronously --- the unlock_buffer() and
 * mark_buffer_uptodate() functions propagate buffer state into the
 * page struct once IO has completed.
 */
static int ntfs_dir_readpage(struct file *dir, struct page *page)
{
	struct inode *vi;
	struct super_block *sb;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	sector_t iblock, lblock;
	unsigned int blocksize, blocks, nr_bu;
	int nr, i;
	unsigned char blocksize_bits;

	/* The page must be locked. */
	if (!PageLocked(page))
		PAGE_BUG(page);
	/*
	 * Get the VFS/ntfs inodes, the super block and ntfs volume associated
	 * with the page.
	 */
	vi = page->mapping->host;
	sb = vi->i_sb;

	/* We need to create buffers for the page so we can do low level io. */
	blocksize = sb->s_blocksize;
	blocksize_bits = sb->s_blocksize_bits;

	if (!page->buffers)
		create_empty_buffers(page, blocksize);
	else
		ntfs_error(sb, "Page (index 0x%lx) already has buffers.",
				page->index);

	nr_bu = blocks = PAGE_CACHE_SIZE >> blocksize_bits;
	iblock = page->index << (PAGE_CACHE_SHIFT - blocksize_bits);
	lblock = (vi->i_size + blocksize - 1) >> blocksize_bits;

	bh = head = page->buffers;
	BUG_ON(!bh);

	/* Loop through all the buffers in the page. */
	i = nr = 0;
	do {
		if (buffer_uptodate(bh)) {
			nr_bu--;
			continue;
		}
		if (!buffer_mapped(bh)) {
			/* Is the block within the allowed limits? */
			if (iblock < lblock) {
				/* Remap the inode offset to its disk block. */
				if (ntfs_file_get_block(vi, iblock, bh, 0))
					continue;
			}
			if (!buffer_mapped(bh)) {
				/*
				 * Error. Zero this portion of the page and set
				 * the buffer uptodate.
				 */
				memset(kmap(page) + i * blocksize, 0,
						blocksize);
				flush_dcache_page(page);
				kunmap(page);
				set_bit(BH_Uptodate, &bh->b_state);
				continue;
			}
			/* The buffer might have been updated synchronousle. */
			if (buffer_uptodate(bh))
				continue;
		}
		arr[nr++] = bh;
	} while (i++, iblock++, (bh = bh->b_this_page) != head);

	/* Check we have at least one buffer ready for io. */
	if (nr) {
		/* Lock the buffers. */
		for (i = 0; i < nr; i++) {
			struct buffer_head *tbh = arr[i];
			lock_buffer(tbh);
			tbh->b_end_io = end_buffer_read_index_async;
			mark_buffer_async(tbh, 1);
		}
		/* Finally, start io on the buffers. */
		for (i = 0; i < nr; i++)
			submit_bh(READ, arr[i]);
		/* We are done. */
		return 0;
	}
	if (!nr_bu) {
		ntfs_debug("All buffers in the page were already uptodate, "
				"assuming mst fixups were already applied.");
		SetPageUptodate(page);
		UnlockPage(page);
		return 0;
	}
	ntfs_error(sb, "No io was scheduled on any of the buffers in the page, "
			"but buffers were not all uptodate to start with. "
			"Setting page error flag and returning io error.");
	SetPageError(page);
	UnlockPage(page);
	return -EIO;
}

/* Address space operations for accessing normal file data. */
struct address_space_operations ntfs_file_aops = {
	writepage:	NULL,			/* Write dirty page to disk. */
	readpage:	ntfs_file_readpage,	/* Fill page with data. */
	sync_page:	block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
	prepare_write:	NULL,			/* . */
	commit_write:	NULL,			/* . */
	//truncatepage:	NULL,			/* . */
};

typedef int readpage_t(struct file *, struct page *);

/* FIXME: Kludge: Address space operations for accessing mftbmp. */
struct address_space_operations ntfs_mftbmp_aops = {
	writepage:	NULL,			/* Write dirty page to disk. */
	readpage:	(readpage_t*)ntfs_mftbmp_readpage, /* Fill page with
							      data. */
	sync_page:	block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
	prepare_write:	NULL,			/* . */
	commit_write:	NULL,			/* . */
	//truncatepage:	NULL,			/* . */
};

/*
 * Address space operations for accessing normal directory data (i.e. index
 * allocation attribute). We can't just use the same operations as for files
 * because 1) the attribute is different and even more importantly 2) the index
 * records have to be multi sector transfer deprotected (i.e. fixed-up).
 */
struct address_space_operations ntfs_dir_aops = {
	writepage:	NULL,			/* Write dirty page to disk. */
	readpage:	ntfs_dir_readpage,	/* Fill page with data. */
	sync_page:	block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
	prepare_write:	NULL,			/* . */
	commit_write:	NULL,			/* . */
	//truncatepage:	NULL,			/* . */
};

