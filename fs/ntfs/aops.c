/**
 * aops.c - NTFS kernel address space operations and page cache handling.
 * 	    Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 * Copyright (c) 2002 Richard Russon.
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
#include <linux/buffer_head.h>

#include "ntfs.h"

/**
 * ntfs_end_buffer_read_async - async io completion for reading attributes
 * @bh:		buffer head on which io is completed
 * @uptodate:	whether @bh is now uptodate or not
 *
 * Asynchronous I/O completion handler for reading pages belonging to the
 * attribute address space of an inode. The inodes can either be files or
 * directories or they can be fake inodes describing some attribute.
 *
 * If NInoMstProtected(), perform the post read mst fixups when all IO on the
 * page has been completed and mark the page uptodate or set the error bit on
 * the page. To determine the size of the records that need fixing up, we cheat
 * a little bit by setting the index_block_size in ntfs_inode to the ntfs
 * record size, and index_block_size_bits, to the log(base 2) of the ntfs
 * record size.
 */
static void ntfs_end_buffer_read_async(struct buffer_head *bh, int uptodate)
{
	static spinlock_t page_uptodate_lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;
	struct buffer_head *tmp;
	struct page *page;
	ntfs_inode *ni;

	if (likely(uptodate))
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);

	page = bh->b_page;

	ni = NTFS_I(page->mapping->host);

	if (likely(uptodate)) {
		s64 file_ofs;

		file_ofs = (page->index << PAGE_CACHE_SHIFT) + bh_offset(bh);
		/* Check for the current buffer head overflowing. */
		if (file_ofs + bh->b_size > ni->initialized_size) {
			char *addr;
			int ofs = 0;

			if (file_ofs < ni->initialized_size)
				ofs = ni->initialized_size - file_ofs;
			addr = kmap_atomic(page, KM_BIO_SRC_IRQ);
			memset(addr + bh_offset(bh) + ofs, 0, bh->b_size - ofs);
			flush_dcache_page(page);
			kunmap_atomic(addr, KM_BIO_SRC_IRQ);
		}
	} else
		SetPageError(page);

	spin_lock_irqsave(&page_uptodate_lock, flags);
	clear_buffer_async_read(bh);
	unlock_buffer(bh);

	tmp = bh->b_this_page;
	while (tmp != bh) {
		if (buffer_locked(tmp)) {
			if (buffer_async_read(tmp))
				goto still_busy;
		} else if (!buffer_uptodate(tmp))
			SetPageError(page);
		tmp = tmp->b_this_page;
	}
	spin_unlock_irqrestore(&page_uptodate_lock, flags);
	/*
	 * If none of the buffers had errors then we can set the page uptodate,
	 * but we first have to perform the post read mst fixups, if the
	 * attribute is mst protected, i.e. if NInoMstProteced(ni) is true.
	 */
	if (!NInoMstProtected(ni)) {
		if (likely(!PageError(page)))
			SetPageUptodate(page);
		unlock_page(page);
		return;
	} else {
		char *addr;
		unsigned int i, recs, nr_err;
		u32 rec_size;

		rec_size = ni->_IDM(index_block_size);
		recs = PAGE_CACHE_SIZE / rec_size;
		addr = kmap_atomic(page, KM_BIO_SRC_IRQ);
		for (i = nr_err = 0; i < recs; i++) {
			if (likely(!post_read_mst_fixup((NTFS_RECORD*)(addr +
					i * rec_size), rec_size)))
				continue;
			nr_err++;
			ntfs_error(ni->vol->sb, "post_read_mst_fixup() failed, "
					"corrupt %s record 0x%Lx. Run chkdsk.",
					ni->mft_no ? "index" : "mft",
					(long long)(((s64)page->index <<
					PAGE_CACHE_SHIFT >>
					ni->_IDM(index_block_size_bits)) + i));
		}
		flush_dcache_page(page);
		kunmap_atomic(addr, KM_BIO_SRC_IRQ);
		if (likely(!nr_err && recs))
			SetPageUptodate(page);
		else {
			ntfs_error(ni->vol->sb, "Setting page error, index "
					"0x%lx.", page->index);
			SetPageError(page);
		}
	}
	unlock_page(page);
	return;
still_busy:
	spin_unlock_irqrestore(&page_uptodate_lock, flags);
	return;
}

/**
 * ntfs_read_block - fill a @page of an address space with data
 * @page:	page cache page to fill with data
 *
 * Fill the page @page of the address space belonging to the @page->host inode.
 * We read each buffer asynchronously and when all buffers are read in, our io
 * completion handler ntfs_end_buffer_read_async(), if required, automatically
 * applies the mst fixups to the page before finally marking it uptodate and
 * unlocking it.
 *
 * Return 0 on success and -errno on error.
 *
 * Contains an adapted version of fs/buffer.c::block_read_full_page().
 */
static int ntfs_read_block(struct page *page)
{
	VCN vcn;
	LCN lcn;
	ntfs_inode *ni;
	ntfs_volume *vol;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	sector_t iblock, lblock, zblock;
	unsigned int blocksize, blocks, vcn_ofs;
	int i, nr;
	unsigned char blocksize_bits;

	ni = NTFS_I(page->mapping->host);
	vol = ni->vol;

	blocksize_bits = VFS_I(ni)->i_blkbits;
	blocksize = 1 << blocksize_bits;

	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);
	bh = head = page_buffers(page);
	if (unlikely(!bh))
		return -ENOMEM;

	blocks = PAGE_CACHE_SIZE >> blocksize_bits;
	iblock = page->index << (PAGE_CACHE_SHIFT - blocksize_bits);
	lblock = (ni->allocated_size + blocksize - 1) >> blocksize_bits;
	zblock = (ni->initialized_size + blocksize - 1) >> blocksize_bits;

#ifdef DEBUG
	if (unlikely(!ni->run_list.rl && !ni->mft_no && !NInoAttr(ni)))
		panic("NTFS: $MFT/$DATA run list has been unmapped! This is a "
				"very serious bug! Cannot continue...");
#endif

	/* Loop through all the buffers in the page. */
	nr = i = 0;
	do {
		if (unlikely(buffer_uptodate(bh)))
			continue;
		if (unlikely(buffer_mapped(bh))) {
			arr[nr++] = bh;
			continue;
		}
		bh->b_bdev = vol->sb->s_bdev;
		/* Is the block within the allowed limits? */
		if (iblock < lblock) {
			BOOL is_retry = FALSE;

			/* Convert iblock into corresponding vcn and offset. */
			vcn = (VCN)iblock << blocksize_bits >>
					vol->cluster_size_bits;
			vcn_ofs = ((VCN)iblock << blocksize_bits) &
					vol->cluster_size_mask;
retry_remap:
			/* Convert the vcn to the corresponding lcn. */
			down_read(&ni->run_list.lock);
			lcn = vcn_to_lcn(ni->run_list.rl, vcn);
			up_read(&ni->run_list.lock);
			/* Successful remap. */
			if (lcn >= 0) {
				/* Setup buffer head to correct block. */
				bh->b_blocknr = ((lcn << vol->cluster_size_bits)
						+ vcn_ofs) >> blocksize_bits;
				set_buffer_mapped(bh);
				/* Only read initialized data blocks. */
				if (iblock < zblock) {
					arr[nr++] = bh;
					continue;
				}
				/* Fully non-initialized data block, zero it. */
				goto handle_zblock;
			}
			/* It is a hole, need to zero it. */
			if (lcn == LCN_HOLE)
				goto handle_hole;
			/* If first try and run list unmapped, map and retry. */
			if (!is_retry && lcn == LCN_RL_NOT_MAPPED) {
				is_retry = TRUE;
				if (!map_run_list(ni, vcn))
					goto retry_remap;
			}
			/* Hard error, zero out region. */
			SetPageError(page);
			ntfs_error(vol->sb, "vcn_to_lcn(vcn = 0x%Lx) failed "
					"with error code 0x%Lx%s.",
					(long long)vcn, (long long)-lcn,
					is_retry ? " even after retrying" : "");
			// FIXME: Depending on vol->on_errors, do something.
		}
		/*
		 * Either iblock was outside lblock limits or vcn_to_lcn()
		 * returned error. Just zero that portion of the page and set
		 * the buffer uptodate.
		 */
handle_hole:
		bh->b_blocknr = -1UL;
		clear_buffer_mapped(bh);
handle_zblock:
		memset(kmap(page) + i * blocksize, 0, blocksize);
		flush_dcache_page(page);
		kunmap(page);
		set_buffer_uptodate(bh);
	} while (i++, iblock++, (bh = bh->b_this_page) != head);

	/* Check we have at least one buffer ready for i/o. */
	if (nr) {
		/* Lock the buffers. */
		for (i = 0; i < nr; i++) {
			struct buffer_head *tbh = arr[i];
			lock_buffer(tbh);
			tbh->b_end_io = ntfs_end_buffer_read_async;
			set_buffer_async_read(tbh);
		}
		/* Finally, start i/o on the buffers. */
		for (i = 0; i < nr; i++)
			submit_bh(READ, arr[i]);
		return 0;
	}
	/* No i/o was scheduled on any of the buffers. */
	if (likely(!PageError(page)))
		SetPageUptodate(page);
	else /* Signal synchronous i/o error. */
		nr = -EIO;
	unlock_page(page);
	return nr;
}

/**
 * ntfs_readpage - fill a @page of a @file with data from the device
 * @file:	open file to which the page @page belongs or NULL
 * @page:	page cache page to fill with data
 *
 * For non-resident attributes, ntfs_readpage() fills the @page of the open
 * file @file by calling the ntfs version of the generic block_read_full_page()
 * function, ntfs_read_block(), which in turn creates and reads in the buffers
 * associated with the page asynchronously.
 *
 * For resident attributes, OTOH, ntfs_readpage() fills @page by copying the
 * data from the mft record (which at this stage is most likely in memory) and
 * fills the remainder with zeroes. Thus, in this case, I/O is synchronous, as
 * even if the mft record is not cached at this point in time, we need to wait
 * for it to be read in before we can do the copy.
 *
 * Return 0 on success and -errno on error.
 */
int ntfs_readpage(struct file *file, struct page *page)
{
	s64 attr_pos;
	ntfs_inode *ni, *base_ni;
	char *addr;
	attr_search_context *ctx;
	MFT_RECORD *mrec;
	u32 attr_len;
	int err = 0;

	if (unlikely(!PageLocked(page)))
		PAGE_BUG(page);

	ni = NTFS_I(page->mapping->host);

	if (NInoNonResident(ni)) {
		/*
		 * Only unnamed $DATA attributes can be compressed or
		 * encrypted.
		 */
		if (ni->type == AT_DATA && !ni->name_len) {
			/* If file is encrypted, deny access, just like NT4. */
			if (NInoEncrypted(ni)) {
				err = -EACCES;
				goto err_out;
			}
			/* Compressed data streams are handled in compress.c. */
			if (NInoCompressed(ni))
				return ntfs_file_read_compressed_block(page);
		}
		/* Normal data stream. */
		return ntfs_read_block(page);
	}
	/* Attribute is resident, implying it is not compressed or encrypted. */
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->_INE(base_ntfs_ino);

	/* Map, pin and lock the mft record for reading. */
	mrec = map_mft_record(READ, base_ni);
	if (unlikely(IS_ERR(mrec))) {
		err = PTR_ERR(mrec);
		goto err_out;
	}
	ctx = get_attr_search_ctx(base_ni, mrec);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto unm_err_out;
	}
	if (unlikely(!lookup_attr(ni->type, ni->name, ni->name_len,
			IGNORE_CASE, 0, NULL, 0, ctx))) {
		err = -ENOENT;
		goto put_unm_err_out;
	}

	/* Starting position of the page within the attribute value. */
	attr_pos = page->index << PAGE_CACHE_SHIFT;

	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(ctx->attr->_ARA(value_length));

	addr = kmap(page);
	/* Copy over in bounds data, zeroing the remainder of the page. */
	if (attr_pos < attr_len) {
		u32 bytes = attr_len - attr_pos;
		if (bytes > PAGE_CACHE_SIZE)
			bytes = PAGE_CACHE_SIZE;
		else if (bytes < PAGE_CACHE_SIZE)
			memset(addr + bytes, 0, PAGE_CACHE_SIZE - bytes);
		/* Copy the data to the page. */
		memcpy(addr, attr_pos + (char*)ctx->attr +
				le16_to_cpu(ctx->attr->_ARA(value_offset)),
				bytes);
	} else
		memset(addr, 0, PAGE_CACHE_SIZE);
	flush_dcache_page(page);
	kunmap(page);

	SetPageUptodate(page);
put_unm_err_out:
	put_attr_search_ctx(ctx);
unm_err_out:
	unmap_mft_record(READ, base_ni);
err_out:
	unlock_page(page);
	return err;
}

/**
 * ntfs_aops - general address space operations for inodes and attributes
 */
struct address_space_operations ntfs_aops = {
	writepage:	NULL,			/* Write dirty page to disk. */
	readpage:	ntfs_readpage,		/* Fill page with data. */
	sync_page:	block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
	prepare_write:	NULL,			/* . */
	commit_write:	NULL,			/* . */
};

