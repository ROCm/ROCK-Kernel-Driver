/*
 * compaops.c - NTFS kernel compressed attributes handling.
 *		Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001 Anton Altaparmakov.
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
#include <linux/ntfs_fs.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/locks.h>

/*
 * We start off with ntfs_read_compressed() from the old NTFS driver and with
 * fs/isofs/compress.c::zisofs_readpage().
 *
 * The aim of the exercise is to have the usual page cache of a compressed
 * inode as the uncompressed data. The 
 */

/**
 * ntfs_file_read_compressed_block - read a compressed block into the page cache
 * page:	locked page in the compression block(s) we need to read
 *
 * When we are called the page has already been verified to be locked and the
 * attribute is known to be non-resident, not encrypted, but compressed.
 *
 * 1. Determine which compression block(s) @page is in.
 * 2. Get hold of all pages corresponding to this/these compression block(s).
 * 3. Read the (first) compression block.
 * 4. Decompress it into the corresponding pages.
 * 5. Throw the compressed data away and proceed to 3. for the next compression
 *    block or return success if no more compression blocks left.
 *
 * Warning: We have to be careful what we do about existing pages. They might
 * have been written to so that we would lose data if we were to just overwrite
 * them with the out-of-date uncompressed data.
 *
 * Note: As an efficiency/latency improvement, it might be a nice idea to
 * create a kernel thread as soon as we have filled @page with data. We can
 * then read the remaining pages at our leisure in the background. However,
 * creation of a kernel thread might actually impact performance so much as to
 * lose all the benefits of returning early... Even further so because when we
 * reach that stage we probably have the whole compression block already in
 * memory, unless we read the block in little chunks and handle each chunk on
 * its own.
 */
int ntfs_file_read_compressed_block(struct page *page)
{
	/* For the moment this will do... */
	UnlockPage(page);
	return -EOPNOTSUPP;
}

#if 0

// From the old NTFS driver:

/* Process compressed attributes. */
int ntfs_read_compressed(ntfs_inode *ino, ntfs_attribute *attr, __s64 offset,
		ntfs_io *dest)
{
	int error = 0;
	int clustersizebits;
	int s_vcn, rnum, vcn, got, l1;
	__s64 copied, len, chunk, offs1, l, chunk2;
	ntfs_cluster_t cluster, cl1;
	char *comp = 0, *comp1;
	char *decomp = 0;
	ntfs_io io;
	ntfs_runlist *rl;

	l = dest->size;
	clustersizebits = ino->vol->cluster_size_bits;
	/* Starting cluster of potential chunk. There are three situations:
	   a) In a large uncompressible or sparse chunk, s_vcn is in the middle
	      of a run.
	   b) s_vcn is right on a run border.
	   c) When several runs make a chunk, s_vcn is before the chunks. */
	s_vcn = offset >> clustersizebits;
	/* Round down to multiple of 16. */
	s_vcn &= ~15;
	rl = attr->d.r.runlist;
	for (rnum = vcn = 0; rnum < attr->d.r.len && vcn + rl->len <= s_vcn;
								rnum++, rl++)
		vcn += rl->len;
	if (rnum == attr->d.r.len) {
		/* Beyond end of file. */
		/* FIXME: Check allocated / initialized. */
		dest->size = 0;
		return 0;
	}
	io.do_read = 1;
	io.fn_put = ntfs_put;
	io.fn_get = 0;
	cluster = rl->lcn;
	len = rl->len;
	copied = 0;
	while (l) {
		chunk = 0;
		if (cluster == (ntfs_cluster_t)-1) {
			/* Sparse cluster. */
			__s64 ll;

			if ((len - (s_vcn - vcn)) & 15)
				ntfs_error("Unexpected sparse chunk size.");
			ll = ((__s64)(vcn + len) << clustersizebits) - offset;
			if (ll > l)
				ll = l;
			chunk = ll;
			error = ntfs_read_zero(dest, ll);
			if (error)
				goto out;
		} else if (dest->do_read) {
			if (!comp) {
				comp = ntfs_malloc(16 << clustersizebits);
				if (!comp) {
					error = -ENOMEM;
					goto out;
				}
			}
			got = 0;
			/* We might need to start in the middle of a run. */
			cl1 = cluster + s_vcn - vcn;
			comp1 = comp;
			do {
				int delta;

				io.param = comp1;
				delta = s_vcn - vcn;
				if (delta < 0)
					delta = 0;
				l1 = len - delta;
				if (l1 > 16 - got)
					l1 = 16 - got;
				io.size = (__s64)l1 << clustersizebits;
				error = ntfs_getput_clusters(ino->vol, cl1, 0,
					       		     &io);
				if (error)
					goto out;
				if (l1 + delta == len) {
					rnum++;
					rl++;
					vcn += len;
					cluster = cl1 = rl->lcn;
					len = rl->len;
				}
				got += l1;
				comp1 += (__s64)l1 << clustersizebits;
			} while (cluster != (ntfs_cluster_t)-1 && got < 16);
							/* Until empty run. */
			chunk = 16 << clustersizebits;
			if (cluster != (ntfs_cluster_t)-1 || got == 16)
				/* Uncompressible */
				comp1 = comp;
			else {
				if (!decomp) {
					decomp = ntfs_malloc(16 << 
							clustersizebits);
					if (!decomp) {
						error = -ENOMEM;
						goto out;
					}
				}
				/* Make sure there are null bytes after the
				 * last block. */
				*(ntfs_u32*)comp1 = 0;
				ntfs_decompress(decomp, comp, chunk);
				comp1 = decomp;
			}
			offs1 = offset - ((__s64)s_vcn << clustersizebits);
			chunk2 = (16 << clustersizebits) - offs1;
			if (chunk2 > l)
				chunk2 = l;
			if (chunk > chunk2)
				chunk = chunk2;
			dest->fn_put(dest, comp1 + offs1, chunk);
		}
		l -= chunk;
		copied += chunk;
		offset += chunk;
		s_vcn = (offset >> clustersizebits) & ~15;
		if (l && offset >= ((__s64)(vcn + len) << clustersizebits)) {
			rnum++;
			rl++;
			vcn += len;
			cluster = rl->lcn;
			len = rl->len;
		}
	}
out:
	if (comp)
		ntfs_free(comp);
	if (decomp)
		ntfs_free(decomp);
	dest->size = copied;
	return error;
}

/*
 * When decompressing, we typically obtain more than one page
 * per reference.  We inject the additional pages into the page
 * cache as a form of readahead.
 */
static int zisofs_readpage(struct file *file, struct page *page)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	unsigned int maxpage, xpage, fpage, blockindex;
	unsigned long offset;
	unsigned long blockptr, blockendptr, cstart, cend, csize;
	struct buffer_head *bh, *ptrbh[2];
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	unsigned int bufshift = ISOFS_BUFFER_BITS(inode);
	unsigned long bufmask  = bufsize - 1;
	int err = -EIO;
	int i;
	unsigned int header_size = inode->u.isofs_i.i_format_parm[0];
	unsigned int zisofs_block_shift = inode->u.isofs_i.i_format_parm[1];
	/* unsigned long zisofs_block_size = 1UL << zisofs_block_shift; */
	unsigned int zisofs_block_page_shift = zisofs_block_shift-PAGE_CACHE_SHIFT;
	unsigned long zisofs_block_pages = 1UL << zisofs_block_page_shift;
	unsigned long zisofs_block_page_mask = zisofs_block_pages-1;
	struct page *pages[zisofs_block_pages];
	unsigned long index = page->index;
	int indexblocks;

	/* We have already been given one page, this is the one
	   we must do. */
	xpage = index & zisofs_block_page_mask;
	pages[xpage] = page;
 
	/* The remaining pages need to be allocated and inserted */
	offset = index & ~zisofs_block_page_mask;
	blockindex = offset >> zisofs_block_page_shift;
	maxpage = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	maxpage = min(zisofs_block_pages, maxpage-offset);

	for ( i = 0 ; i < maxpage ; i++, offset++ ) {
		if ( i != xpage ) {
			pages[i] = grab_cache_page_nowait(mapping, offset);
		}
		page = pages[i];
		if ( page ) {
			ClearPageError(page);
			kmap(page);
		}
	}

	/* This is the last page filled, plus one; used in case of abort. */
	fpage = 0;

	/* Find the pointer to this specific chunk */
	/* Note: we're not using isonum_731() here because the data is known aligned */
	/* Note: header_size is in 32-bit words (4 bytes) */
	blockptr = (header_size + blockindex) << 2;
	blockendptr = blockptr + 4;

	indexblocks = ((blockptr^blockendptr) >> bufshift) ? 2 : 1;
	ptrbh[0] = ptrbh[1] = 0;

	if ( isofs_get_blocks(inode, blockptr >> bufshift, ptrbh, indexblocks) != indexblocks ) {
		if ( ptrbh[0] ) brelse(ptrbh[0]);
		printk(KERN_DEBUG "zisofs: Null buffer on reading block table, inode = %lu, block = %lu\n",
		       inode->i_ino, blockptr >> bufshift);
		goto eio;
	}
	ll_rw_block(READ, indexblocks, ptrbh);

	bh = ptrbh[0];
	if ( !bh || (wait_on_buffer(bh), !buffer_uptodate(bh)) ) {
		printk(KERN_DEBUG "zisofs: Failed to read block table, inode = %lu, block = %lu\n",
		       inode->i_ino, blockptr >> bufshift);
		if ( ptrbh[1] )
			brelse(ptrbh[1]);
		goto eio;
	}
	cstart = le32_to_cpu(*(u32 *)(bh->b_data + (blockptr & bufmask)));

	if ( indexblocks == 2 ) {
		/* We just crossed a block boundary.  Switch to the next block */
		brelse(bh);
		bh = ptrbh[1];
		if ( !bh || (wait_on_buffer(bh), !buffer_uptodate(bh)) ) {
			printk(KERN_DEBUG "zisofs: Failed to read block table, inode = %lu, block = %lu\n",
			       inode->i_ino, blockendptr >> bufshift);
			goto eio;
		}
	}
	cend = le32_to_cpu(*(u32 *)(bh->b_data + (blockendptr & bufmask)));
	brelse(bh);

	csize = cend-cstart;

	/* Now page[] contains an array of pages, any of which can be NULL,
	   and the locks on which we hold.  We should now read the data and
	   release the pages.  If the pages are NULL the decompressed data
	   for that particular page should be discarded. */
	
	if ( csize == 0 ) {
		/* This data block is empty. */

		for ( fpage = 0 ; fpage < maxpage ; fpage++ ) {
			if ( (page = pages[fpage]) != NULL ) {
				memset(page_address(page), 0, PAGE_CACHE_SIZE);
				
				flush_dcache_page(page);
				SetPageUptodate(page);
				kunmap(page);
				UnlockPage(page);
				if ( fpage == xpage )
					err = 0; /* The critical page */
				else
					page_cache_release(page);
			}
		}
	} else {
		/* This data block is compressed. */
		z_stream stream;
		int bail = 0, left_out = -1;
		int zerr;
		int needblocks = (csize + (cstart & bufmask) + bufmask) >> bufshift;
		int haveblocks;
		struct buffer_head *bhs[needblocks+1];
		struct buffer_head **bhptr;

		/* Because zlib is not thread-safe, do all the I/O at the top. */

		blockptr = cstart >> bufshift;
		memset(bhs, 0, (needblocks+1)*sizeof(struct buffer_head *));
		haveblocks = isofs_get_blocks(inode, blockptr, bhs, needblocks);
		ll_rw_block(READ, haveblocks, bhs);

		bhptr = &bhs[0];
		bh = *bhptr++;

		/* First block is special since it may be fractional.
		   We also wait for it before grabbing the zlib
		   semaphore; odds are that the subsequent blocks are
		   going to come in in short order so we don't hold
		   the zlib semaphore longer than necessary. */

		if ( !bh || (wait_on_buffer(bh), !buffer_uptodate(bh)) ) {
			printk(KERN_DEBUG "zisofs: Hit null buffer, fpage = %d, xpage = %d, csize = %ld\n",
			       fpage, xpage, csize);
			goto b_eio;
		}
		stream.next_in  = bh->b_data + (cstart & bufmask);
		stream.avail_in = min(bufsize-(cstart & bufmask), csize);
		csize -= stream.avail_in;

		stream.workspace = zisofs_zlib_workspace;
		down(&zisofs_zlib_semaphore);
		
		zerr = zlib_fs_inflateInit(&stream);
		if ( zerr != Z_OK ) {
			if ( err && zerr == Z_MEM_ERROR )
				err = -ENOMEM;
			printk(KERN_DEBUG "zisofs: zisofs_inflateInit returned %d\n",
			       zerr);
			goto z_eio;
		}

		while ( !bail && fpage < maxpage ) {
			page = pages[fpage];
			if ( page )
				stream.next_out = page_address(page);
			else
				stream.next_out = (void *)&zisofs_sink_page;
			stream.avail_out = PAGE_CACHE_SIZE;

			while ( stream.avail_out ) {
				int ao, ai;
				if ( stream.avail_in == 0 && left_out ) {
					if ( !csize ) {
						printk(KERN_WARNING "zisofs: ZF read beyond end of input\n");
						bail = 1;
						break;
					} else {
						bh = *bhptr++;
						if ( !bh ||
						     (wait_on_buffer(bh), !buffer_uptodate(bh)) ) {
							/* Reached an EIO */
 							printk(KERN_DEBUG "zisofs: Hit null buffer, fpage = %d, xpage = %d, csize = %ld\n",
							       fpage, xpage, csize);
							       
							bail = 1;
							break;
						}
						stream.next_in = bh->b_data;
						stream.avail_in = min(csize,bufsize);
						csize -= stream.avail_in;
					}
				}
				ao = stream.avail_out;  ai = stream.avail_in;
				zerr = zlib_fs_inflate(&stream, Z_SYNC_FLUSH);
				left_out = stream.avail_out;
				if ( zerr == Z_BUF_ERROR && stream.avail_in == 0 )
					continue;
				if ( zerr != Z_OK ) {
					/* EOF, error, or trying to read beyond end of input */
					if ( err && zerr == Z_MEM_ERROR )
						err = -ENOMEM;
					if ( zerr != Z_STREAM_END )
						printk(KERN_DEBUG "zisofs: zisofs_inflate returned %d, inode = %lu, index = %lu, fpage = %d, xpage = %d, avail_in = %d, avail_out = %d, ai = %d, ao = %d\n",
						       zerr, inode->i_ino, index,
						       fpage, xpage,
						       stream.avail_in, stream.avail_out,
						       ai, ao);
					bail = 1;
					break;
				}
			}

			if ( stream.avail_out && zerr == Z_STREAM_END ) {
				/* Fractional page written before EOF.  This may
				   be the last page in the file. */
				memset(stream.next_out, 0, stream.avail_out);
				stream.avail_out = 0;
			}

			if ( !stream.avail_out ) {
				/* This page completed */
				if ( page ) {
					flush_dcache_page(page);
					SetPageUptodate(page);
					kunmap(page);
					UnlockPage(page);
					if ( fpage == xpage )
						err = 0; /* The critical page */
					else
						page_cache_release(page);
				}
				fpage++;
			}
		}
		zlib_fs_inflateEnd(&stream);

	z_eio:
		up(&zisofs_zlib_semaphore);

	b_eio:
		for ( i = 0 ; i < haveblocks ; i++ ) {
			if ( bhs[i] )
				brelse(bhs[i]);
		}
	}

eio:

	/* Release any residual pages, do not SetPageUptodate */
	while ( fpage < maxpage ) {
		page = pages[fpage];
		if ( page ) {
			flush_dcache_page(page);
			if ( fpage == xpage )
				SetPageError(page);
			kunmap(page);
			UnlockPage(page);
			if ( fpage != xpage )
				page_cache_release(page);
		}
		fpage++;
	}			

	/* At this point, err contains 0 or -EIO depending on the "critical" page */
	return err;
}

#endif

