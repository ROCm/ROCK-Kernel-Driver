/**
 * mft.c - NTFS kernel mft record operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
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

#include <linux/buffer_head.h>
#include <linux/swap.h>

#include "bitmap.h"
#include "lcnalloc.h"
#include "aops.h"
#include "debug.h"
#include "mft.h"
#include "malloc.h"
#include "ntfs.h"

/**
 * map_mft_record_page - map the page in which a specific mft record resides
 * @ni:		ntfs inode whose mft record page to map
 *
 * This maps the page in which the mft record of the ntfs inode @ni is situated
 * and returns a pointer to the mft record within the mapped page.
 *
 * Return value needs to be checked with IS_ERR() and if that is true PTR_ERR()
 * contains the negative error code returned.
 */
static inline MFT_RECORD *map_mft_record_page(ntfs_inode *ni)
{
	ntfs_volume *vol = ni->vol;
	struct inode *mft_vi = vol->mft_ino;
	struct page *page;
	unsigned long index, ofs, end_index;

	BUG_ON(ni->page);
	/*
	 * The index into the page cache and the offset within the page cache
	 * page of the wanted mft record. FIXME: We need to check for
	 * overflowing the unsigned long, but I don't think we would ever get
	 * here if the volume was that big...
	 */
	index = ni->mft_no << vol->mft_record_size_bits >> PAGE_CACHE_SHIFT;
	ofs = (ni->mft_no << vol->mft_record_size_bits) & ~PAGE_CACHE_MASK;

	/* The maximum valid index into the page cache for $MFT's data. */
	end_index = mft_vi->i_size >> PAGE_CACHE_SHIFT;

	/* If the wanted index is out of bounds the mft record doesn't exist. */
	if (unlikely(index >= end_index)) {
		if (index > end_index || (mft_vi->i_size & ~PAGE_CACHE_MASK) <
				ofs + vol->mft_record_size) {
			page = ERR_PTR(-ENOENT);
			goto err_out;
		}
	}
	/* Read, map, and pin the page. */
	page = ntfs_map_page(mft_vi->i_mapping, index);
	if (likely(!IS_ERR(page))) {
		ni->page = page;
		ni->page_ofs = ofs;
		return page_address(page) + ofs;
	}
err_out:
	ni->page = NULL;
	ni->page_ofs = 0;
	ntfs_error(vol->sb, "Failed with error code %lu.", -PTR_ERR(page));
	return (void*)page;
}

/**
 * map_mft_record - map, pin and lock an mft record
 * @ni:		ntfs inode whose MFT record to map
 *
 * First, take the mrec_lock semaphore. We might now be sleeping, while waiting
 * for the semaphore if it was already locked by someone else.
 *
 * The page of the record is mapped using map_mft_record_page() before being
 * returned to the caller.
 *
 * This in turn uses ntfs_map_page() to get the page containing the wanted mft
 * record (it in turn calls read_cache_page() which reads it in from disk if
 * necessary, increments the use count on the page so that it cannot disappear
 * under us and returns a reference to the page cache page).
 *
 * If read_cache_page() invokes ntfs_readpage() to load the page from disk, it
 * sets PG_locked and clears PG_uptodate on the page. Once I/O has completed
 * and the post-read mst fixups on each mft record in the page have been
 * performed, the page gets PG_uptodate set and PG_locked cleared (this is done
 * in our asynchronous I/O completion handler end_buffer_read_mft_async()).
 * ntfs_map_page() waits for PG_locked to become clear and checks if
 * PG_uptodate is set and returns an error code if not. This provides
 * sufficient protection against races when reading/using the page.
 *
 * However there is the write mapping to think about. Doing the above described
 * checking here will be fine, because when initiating the write we will set
 * PG_locked and clear PG_uptodate making sure nobody is touching the page
 * contents. Doing the locking this way means that the commit to disk code in
 * the page cache code paths is automatically sufficiently locked with us as
 * we will not touch a page that has been locked or is not uptodate. The only
 * locking problem then is them locking the page while we are accessing it.
 *
 * So that code will end up having to own the mrec_lock of all mft
 * records/inodes present in the page before I/O can proceed. In that case we
 * wouldn't need to bother with PG_locked and PG_uptodate as nobody will be
 * accessing anything without owning the mrec_lock semaphore. But we do need
 * to use them because of the read_cache_page() invocation and the code becomes
 * so much simpler this way that it is well worth it.
 *
 * The mft record is now ours and we return a pointer to it. You need to check
 * the returned pointer with IS_ERR() and if that is true, PTR_ERR() will return
 * the error code.
 *
 * NOTE: Caller is responsible for setting the mft record dirty before calling
 * unmap_mft_record(). This is obviously only necessary if the caller really
 * modified the mft record...
 * Q: Do we want to recycle one of the VFS inode state bits instead?
 * A: No, the inode ones mean we want to change the mft record, not we want to
 * write it out.
 */
MFT_RECORD *map_mft_record(ntfs_inode *ni)
{
	MFT_RECORD *m;

	ntfs_debug("Entering for mft_no 0x%lx.", ni->mft_no);

	/* Make sure the ntfs inode doesn't go away. */
	atomic_inc(&ni->count);

	/* Serialize access to this mft record. */
	down(&ni->mrec_lock);

	m = map_mft_record_page(ni);
	if (likely(!IS_ERR(m)))
		return m;

	up(&ni->mrec_lock);
	atomic_dec(&ni->count);
	ntfs_error(ni->vol->sb, "Failed with error code %lu.", -PTR_ERR(m));
	return m;
}

/**
 * unmap_mft_record_page - unmap the page in which a specific mft record resides
 * @ni:		ntfs inode whose mft record page to unmap
 *
 * This unmaps the page in which the mft record of the ntfs inode @ni is
 * situated and returns. This is a NOOP if highmem is not configured.
 *
 * The unmap happens via ntfs_unmap_page() which in turn decrements the use
 * count on the page thus releasing it from the pinned state.
 *
 * We do not actually unmap the page from memory of course, as that will be
 * done by the page cache code itself when memory pressure increases or
 * whatever.
 */
static inline void unmap_mft_record_page(ntfs_inode *ni)
{
	BUG_ON(!ni->page);

	// TODO: If dirty, blah...
	ntfs_unmap_page(ni->page);
	ni->page = NULL;
	ni->page_ofs = 0;
	return;
}

/**
 * unmap_mft_record - release a mapped mft record
 * @ni:		ntfs inode whose MFT record to unmap
 *
 * We release the page mapping and the mrec_lock mutex which unmaps the mft
 * record and releases it for others to get hold of. We also release the ntfs
 * inode by decrementing the ntfs inode reference count.
 *
 * NOTE: If caller has modified the mft record, it is imperative to set the mft
 * record dirty BEFORE calling unmap_mft_record().
 */
void unmap_mft_record(ntfs_inode *ni)
{
	struct page *page = ni->page;

	BUG_ON(!page);

	ntfs_debug("Entering for mft_no 0x%lx.", ni->mft_no);

	unmap_mft_record_page(ni);
	up(&ni->mrec_lock);
	atomic_dec(&ni->count);
	/*
	 * If pure ntfs_inode, i.e. no vfs inode attached, we leave it to
	 * ntfs_clear_extent_inode() in the extent inode case, and to the
	 * caller in the non-extent, yet pure ntfs inode case, to do the actual
	 * tear down of all structures and freeing of all allocated memory.
	 */
	return;
}

/**
 * map_extent_mft_record - load an extent inode and attach it to its base
 * @base_ni:	base ntfs inode
 * @mref:	mft reference of the extent inode to load
 * @ntfs_ino:	on successful return, pointer to the ntfs_inode structure
 *
 * Load the extent mft record @mref and attach it to its base inode @base_ni.
 * Return the mapped extent mft record if IS_ERR(result) is false.  Otherwise
 * PTR_ERR(result) gives the negative error code.
 *
 * On successful return, @ntfs_ino contains a pointer to the ntfs_inode
 * structure of the mapped extent inode.
 */
MFT_RECORD *map_extent_mft_record(ntfs_inode *base_ni, MFT_REF mref,
		ntfs_inode **ntfs_ino)
{
	MFT_RECORD *m;
	ntfs_inode *ni = NULL;
	ntfs_inode **extent_nis = NULL;
	int i;
	unsigned long mft_no = MREF(mref);
	u16 seq_no = MSEQNO(mref);
	BOOL destroy_ni = FALSE;

	ntfs_debug("Mapping extent mft record 0x%lx (base mft record 0x%lx).",
			mft_no, base_ni->mft_no);
	/* Make sure the base ntfs inode doesn't go away. */
	atomic_inc(&base_ni->count);
	/*
	 * Check if this extent inode has already been added to the base inode,
	 * in which case just return it. If not found, add it to the base
	 * inode before returning it.
	 */
	down(&base_ni->extent_lock);
	if (base_ni->nr_extents > 0) {
		extent_nis = base_ni->ext.extent_ntfs_inos;
		for (i = 0; i < base_ni->nr_extents; i++) {
			if (mft_no != extent_nis[i]->mft_no)
				continue;
			ni = extent_nis[i];
			/* Make sure the ntfs inode doesn't go away. */
			atomic_inc(&ni->count);
			break;
		}
	}
	if (likely(ni != NULL)) {
		up(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		/* We found the record; just have to map and return it. */
		m = map_mft_record(ni);
		/* map_mft_record() has incremented this on success. */
		atomic_dec(&ni->count);
		if (likely(!IS_ERR(m))) {
			/* Verify the sequence number. */
			if (likely(le16_to_cpu(m->sequence_number) == seq_no)) {
				ntfs_debug("Done 1.");
				*ntfs_ino = ni;
				return m;
			}
			unmap_mft_record(ni);
			ntfs_error(base_ni->vol->sb, "Found stale extent mft "
					"reference! Corrupt file system. "
					"Run chkdsk.");
			return ERR_PTR(-EIO);
		}
map_err_out:
		ntfs_error(base_ni->vol->sb, "Failed to map extent "
				"mft record, error code %ld.", -PTR_ERR(m));
		return m;
	}
	/* Record wasn't there. Get a new ntfs inode and initialize it. */
	ni = ntfs_new_extent_inode(base_ni->vol->sb, mft_no);
	if (unlikely(!ni)) {
		up(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		return ERR_PTR(-ENOMEM);
	}
	ni->vol = base_ni->vol;
	ni->seq_no = seq_no;
	ni->nr_extents = -1;
	ni->ext.base_ntfs_ino = base_ni;
	/* Now map the record. */
	m = map_mft_record(ni);
	if (IS_ERR(m)) {
		up(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		ntfs_clear_extent_inode(ni);
		goto map_err_out;
	}
	/* Verify the sequence number. */
	if (unlikely(le16_to_cpu(m->sequence_number) != seq_no)) {
		ntfs_error(base_ni->vol->sb, "Found stale extent mft "
				"reference! Corrupt file system. Run chkdsk.");
		destroy_ni = TRUE;
		m = ERR_PTR(-EIO);
		goto unm_err_out;
	}
	/* Attach extent inode to base inode, reallocating memory if needed. */
	if (!(base_ni->nr_extents & 3)) {
		ntfs_inode **tmp;
		int new_size = (base_ni->nr_extents + 4) * sizeof(ntfs_inode *);

		tmp = (ntfs_inode **)kmalloc(new_size, GFP_NOFS);
		if (unlikely(!tmp)) {
			ntfs_error(base_ni->vol->sb, "Failed to allocate "
					"internal buffer.");
			destroy_ni = TRUE;
			m = ERR_PTR(-ENOMEM);
			goto unm_err_out;
		}
		if (base_ni->nr_extents) {
			BUG_ON(!base_ni->ext.extent_ntfs_inos);
			memcpy(tmp, base_ni->ext.extent_ntfs_inos, new_size -
					4 * sizeof(ntfs_inode *));
			kfree(base_ni->ext.extent_ntfs_inos);
		}
		base_ni->ext.extent_ntfs_inos = tmp;
	}
	base_ni->ext.extent_ntfs_inos[base_ni->nr_extents++] = ni;
	up(&base_ni->extent_lock);
	atomic_dec(&base_ni->count);
	ntfs_debug("Done 2.");
	*ntfs_ino = ni;
	return m;
unm_err_out:
	unmap_mft_record(ni);
	up(&base_ni->extent_lock);
	atomic_dec(&base_ni->count);
	/*
	 * If the extent inode was not attached to the base inode we need to
	 * release it or we will leak memory.
	 */
	if (destroy_ni)
		ntfs_clear_extent_inode(ni);
	return m;
}

#ifdef NTFS_RW

/**
 * __mark_mft_record_dirty - set the mft record and the page containing it dirty
 * @ni:		ntfs inode describing the mapped mft record
 *
 * Internal function.  Users should call mark_mft_record_dirty() instead.
 *
 * Set the mapped (extent) mft record of the (base or extent) ntfs inode @ni,
 * as well as the page containing the mft record, dirty.  Also, mark the base
 * vfs inode dirty.  This ensures that any changes to the mft record are
 * written out to disk.
 *
 * NOTE:  We only set I_DIRTY_SYNC and I_DIRTY_DATASYNC (and not I_DIRTY_PAGES)
 * on the base vfs inode, because even though file data may have been modified,
 * it is dirty in the inode meta data rather than the data page cache of the
 * inode, and thus there are no data pages that need writing out.  Therefore, a
 * full mark_inode_dirty() is overkill.  A mark_inode_dirty_sync(), on the
 * other hand, is not sufficient, because I_DIRTY_DATASYNC needs to be set to
 * ensure ->write_inode is called from generic_osync_inode() and this needs to
 * happen or the file data would not necessarily hit the device synchronously,
 * even though the vfs inode has the O_SYNC flag set.  Also, I_DIRTY_DATASYNC
 * simply "feels" better than just I_DIRTY_SYNC, since the file data has not
 * actually hit the block device yet, which is not what I_DIRTY_SYNC on its own
 * would suggest.
 */
void __mark_mft_record_dirty(ntfs_inode *ni)
{
	ntfs_inode *base_ni;

	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);
	BUG_ON(NInoAttr(ni));
	mark_ntfs_record_dirty(ni->page, ni->page_ofs);
	/* Determine the base vfs inode and mark it dirty, too. */
	down(&ni->extent_lock);
	if (likely(ni->nr_extents >= 0))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	up(&ni->extent_lock);
	__mark_inode_dirty(VFS_I(base_ni), I_DIRTY_SYNC | I_DIRTY_DATASYNC);
}

static const char *ntfs_please_email = "Please email "
		"linux-ntfs-dev@lists.sourceforge.net and say that you saw "
		"this message.  Thank you.";

/**
 * ntfs_sync_mft_mirror_umount - synchronise an mft record to the mft mirror
 * @vol:	ntfs volume on which the mft record to synchronize resides
 * @mft_no:	mft record number of mft record to synchronize
 * @m:		mapped, mst protected (extent) mft record to synchronize
 *
 * Write the mapped, mst protected (extent) mft record @m with mft record
 * number @mft_no to the mft mirror ($MFTMirr) of the ntfs volume @vol,
 * bypassing the page cache and the $MFTMirr inode itself.
 *
 * This function is only for use at umount time when the mft mirror inode has
 * already been disposed off.  We BUG() if we are called while the mft mirror
 * inode is still attached to the volume.
 *
 * On success return 0.  On error return -errno.
 *
 * NOTE:  This function is not implemented yet as I am not convinced it can
 * actually be triggered considering the sequence of commits we do in super.c::
 * ntfs_put_super().  But just in case we provide this place holder as the
 * alternative would be either to BUG() or to get a NULL pointer dereference
 * and Oops.
 */
static int ntfs_sync_mft_mirror_umount(ntfs_volume *vol,
		const unsigned long mft_no, MFT_RECORD *m)
{
	BUG_ON(vol->mftmirr_ino);
	ntfs_error(vol->sb, "Umount time mft mirror syncing is not "
			"implemented yet.  %s", ntfs_please_email);
	return -EOPNOTSUPP;
}

/**
 * ntfs_sync_mft_mirror - synchronize an mft record to the mft mirror
 * @vol:	ntfs volume on which the mft record to synchronize resides
 * @mft_no:	mft record number of mft record to synchronize
 * @m:		mapped, mst protected (extent) mft record to synchronize
 * @sync:	if true, wait for i/o completion
 *
 * Write the mapped, mst protected (extent) mft record @m with mft record
 * number @mft_no to the mft mirror ($MFTMirr) of the ntfs volume @vol.
 *
 * On success return 0.  On error return -errno and set the volume errors flag
 * in the ntfs volume @vol.
 *
 * NOTE:  We always perform synchronous i/o and ignore the @sync parameter.
 *
 * TODO:  If @sync is false, want to do truly asynchronous i/o, i.e. just
 * schedule i/o via ->writepage or do it via kntfsd or whatever.
 */
int ntfs_sync_mft_mirror(ntfs_volume *vol, const unsigned long mft_no,
		MFT_RECORD *m, int sync)
{
	struct page *page;
	unsigned int blocksize = vol->sb->s_blocksize;
	int max_bhs = vol->mft_record_size / blocksize;
	struct buffer_head *bhs[max_bhs];
	struct buffer_head *bh, *head;
	u8 *kmirr;
	unsigned int block_start, block_end, m_start, m_end;
	int i_bhs, nr_bhs, err = 0;

	ntfs_debug("Entering for inode 0x%lx.", mft_no);
	BUG_ON(!max_bhs);
	if (unlikely(!vol->mftmirr_ino)) {
		/* This could happen during umount... */
		err = ntfs_sync_mft_mirror_umount(vol, mft_no, m);
		if (likely(!err))
			return err;
		goto err_out;
	}
	/* Get the page containing the mirror copy of the mft record @m. */
	page = ntfs_map_page(vol->mftmirr_ino->i_mapping, mft_no >>
			(PAGE_CACHE_SHIFT - vol->mft_record_size_bits));
	if (IS_ERR(page)) {
		ntfs_error(vol->sb, "Failed to map mft mirror page.");
		err = PTR_ERR(page);
		goto err_out;
	}
	/*
	 * Exclusion against other writers.   This should never be a problem
	 * since the page in which the mft record @m resides is also locked and
	 * hence any other writers would be held up there but it is better to
	 * make sure no one is writing from elsewhere.
	 */
	lock_page(page);
	BUG_ON(!PageUptodate(page));
	ClearPageUptodate(page);
	/* The address in the page of the mirror copy of the mft record @m. */
	kmirr = page_address(page) + ((mft_no << vol->mft_record_size_bits) &
			~PAGE_CACHE_MASK);
	/* Copy the mst protected mft record to the mirror. */
	memcpy(kmirr, m, vol->mft_record_size);
	/* Make sure we have mapped buffers. */
	BUG_ON(!page_has_buffers(page));
	bh = head = page_buffers(page);
	BUG_ON(!bh);
	nr_bhs = 0;
	block_start = 0;
	m_start = kmirr - (u8*)page_address(page);
	m_end = m_start + vol->mft_record_size;
	do {
		block_end = block_start + blocksize;
		/* If the buffer is outside the mft record, skip it. */
		if ((block_end <= m_start) || (block_start >= m_end))
			continue;
		BUG_ON(!buffer_mapped(bh));
		BUG_ON(!buffer_uptodate(bh));
		BUG_ON(!nr_bhs && (m_start != block_start));
		BUG_ON(nr_bhs >= max_bhs);
		bhs[nr_bhs++] = bh;
		BUG_ON((nr_bhs >= max_bhs) && (m_end != block_end));
	} while (block_start = block_end, (bh = bh->b_this_page) != head);
	if (likely(!err)) {
		/* Lock buffers and start synchronous write i/o on them. */
		for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
			struct buffer_head *tbh = bhs[i_bhs];

			if (unlikely(test_set_buffer_locked(tbh)))
				BUG();
			BUG_ON(!buffer_uptodate(tbh));
			if (buffer_dirty(tbh))
				clear_buffer_dirty(tbh);
			get_bh(tbh);
			tbh->b_end_io = end_buffer_write_sync;
			submit_bh(WRITE, tbh);
		}
		/* Wait on i/o completion of buffers. */
		for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
			struct buffer_head *tbh = bhs[i_bhs];

			wait_on_buffer(tbh);
			if (unlikely(!buffer_uptodate(tbh))) {
				err = -EIO;
				/*
				 * Set the buffer uptodate so the page and
				 * buffer states do not become out of sync.
				 */
				set_buffer_uptodate(tbh);
			}
		}
	} else /* if (unlikely(err)) */ {
		/* Clean the buffers. */
		for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++)
			clear_buffer_dirty(bhs[i_bhs]);
	}
	/* Current state: all buffers are clean, unlocked, and uptodate. */
	/* Remove the mst protection fixups again. */
	post_write_mst_fixup((NTFS_RECORD*)kmirr);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);
	ntfs_unmap_page(page);
	if (likely(!err)) {
		ntfs_debug("Done.");
	} else {
		ntfs_error(vol->sb, "I/O error while writing mft mirror "
				"record 0x%lx!", mft_no);
err_out:
		ntfs_error(vol->sb, "Failed to synchronize $MFTMirr (error "
				"code %i).  Volume will be left marked dirty "
				"on umount.  Run ntfsfix on the partition "
				"after umounting to correct this.", -err);
		NVolSetErrors(vol);
	}
	return err;
}

/**
 * write_mft_record_nolock - write out a mapped (extent) mft record
 * @ni:		ntfs inode describing the mapped (extent) mft record
 * @m:		mapped (extent) mft record to write
 * @sync:	if true, wait for i/o completion
 *
 * Write the mapped (extent) mft record @m described by the (regular or extent)
 * ntfs inode @ni to backing store.  If the mft record @m has a counterpart in
 * the mft mirror, that is also updated.
 *
 * We only write the mft record if the ntfs inode @ni is dirty and the first
 * buffer belonging to its mft record is dirty, too.  We ignore the dirty state
 * of subsequent buffers because we could have raced with
 * fs/ntfs/aops.c::mark_ntfs_record_dirty().
 *
 * On success, clean the mft record and return 0.  On error, leave the mft
 * record dirty and return -errno.  The caller should call make_bad_inode() on
 * the base inode to ensure no more access happens to this inode.  We do not do
 * it here as the caller may want to finish writing other extent mft records
 * first to minimize on-disk metadata inconsistencies.
 *
 * NOTE:  We always perform synchronous i/o and ignore the @sync parameter.
 * However, if the mft record has a counterpart in the mft mirror and @sync is
 * true, we write the mft record, wait for i/o completion, and only then write
 * the mft mirror copy.  This ensures that if the system crashes either the mft
 * or the mft mirror will contain a self-consistent mft record @m.  If @sync is
 * false on the other hand, we start i/o on both and then wait for completion
 * on them.  This provides a speedup but no longer guarantees that you will end
 * up with a self-consistent mft record in the case of a crash but if you asked
 * for asynchronous writing you probably do not care about that anyway.
 *
 * TODO:  If @sync is false, want to do truly asynchronous i/o, i.e. just
 * schedule i/o via ->writepage or do it via kntfsd or whatever.
 */
int write_mft_record_nolock(ntfs_inode *ni, MFT_RECORD *m, int sync)
{
	ntfs_volume *vol = ni->vol;
	struct page *page = ni->page;
	unsigned int blocksize = vol->sb->s_blocksize;
	int max_bhs = vol->mft_record_size / blocksize;
	struct buffer_head *bhs[max_bhs];
	struct buffer_head *bh, *head;
	unsigned int block_start, block_end, m_start, m_end;
	int i_bhs, nr_bhs, err = 0;
	BOOL rec_is_dirty = TRUE;

	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);
	BUG_ON(NInoAttr(ni));
	BUG_ON(!max_bhs);
	BUG_ON(!PageLocked(page));
	/*
	 * If the ntfs_inode is clean no need to do anything.  If it is dirty,
	 * mark it as clean now so that it can be redirtied later on if needed.
	 * There is no danger of races since the caller is holding the locks
	 * for the mft record @m and the page it is in.
	 */
	if (!NInoTestClearDirty(ni))
		goto done;
	BUG_ON(!page_has_buffers(page));
	bh = head = page_buffers(page);
	BUG_ON(!bh);
	nr_bhs = 0;
	block_start = 0;
	m_start = ni->page_ofs;
	m_end = m_start + vol->mft_record_size;
	do {
		block_end = block_start + blocksize;
		/* If the buffer is outside the mft record, skip it. */
		if (block_end <= m_start)
			continue;
		if (unlikely(block_start >= m_end))
			break;
		if (block_start == m_start) {
			/* This block is the first one in the record. */
			if (!buffer_dirty(bh)) {
				/* Clean records are not written out. */
				rec_is_dirty = FALSE;
				continue;
			}
			rec_is_dirty = TRUE;
		} else {
			/*
			 * This block is not the first one in the record.  We
			 * ignore the buffer's dirty state because we could
			 * have raced with a parallel mark_ntfs_record_dirty().
			 */
			if (!rec_is_dirty)
				continue;
		}
		BUG_ON(!buffer_mapped(bh));
		BUG_ON(!buffer_uptodate(bh));
		BUG_ON(!nr_bhs && (m_start != block_start));
		BUG_ON(nr_bhs >= max_bhs);
		bhs[nr_bhs++] = bh;
		BUG_ON((nr_bhs >= max_bhs) && (m_end != block_end));
	} while (block_start = block_end, (bh = bh->b_this_page) != head);
	if (!rec_is_dirty)
		goto done;
	if (unlikely(err))
		goto cleanup_out;
	/* Apply the mst protection fixups. */
	err = pre_write_mst_fixup((NTFS_RECORD*)m, vol->mft_record_size);
	if (err) {
		ntfs_error(vol->sb, "Failed to apply mst fixups!");
		goto cleanup_out;
	}
	flush_dcache_mft_record_page(ni);
	/* Lock buffers and start synchronous write i/o on them. */
	for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
		struct buffer_head *tbh = bhs[i_bhs];

		if (unlikely(test_set_buffer_locked(tbh)))
			BUG();
		BUG_ON(!buffer_uptodate(tbh));
		clear_buffer_dirty(tbh);
		get_bh(tbh);
		tbh->b_end_io = end_buffer_write_sync;
		submit_bh(WRITE, tbh);
	}
	/* Synchronize the mft mirror now if not @sync. */
	if (!sync && ni->mft_no < vol->mftmirr_size)
		ntfs_sync_mft_mirror(vol, ni->mft_no, m, sync);
	/* Wait on i/o completion of buffers. */
	for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
		struct buffer_head *tbh = bhs[i_bhs];

		wait_on_buffer(tbh);
		if (unlikely(!buffer_uptodate(tbh))) {
			err = -EIO;
			/*
			 * Set the buffer uptodate so the page and buffer
			 * states do not become out of sync.
			 */
			if (PageUptodate(page))
				set_buffer_uptodate(tbh);
		}
	}
	/* If @sync, now synchronize the mft mirror. */
	if (sync && ni->mft_no < vol->mftmirr_size)
		ntfs_sync_mft_mirror(vol, ni->mft_no, m, sync);
	/* Remove the mst protection fixups again. */
	post_write_mst_fixup((NTFS_RECORD*)m);
	flush_dcache_mft_record_page(ni);
	if (unlikely(err)) {
		/* I/O error during writing.  This is really bad! */
		ntfs_error(vol->sb, "I/O error while writing mft record "
				"0x%lx!  Marking base inode as bad.  You "
				"should unmount the volume and run chkdsk.",
				ni->mft_no);
		goto err_out;
	}
done:
	ntfs_debug("Done.");
	return 0;
cleanup_out:
	/* Clean the buffers. */
	for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++)
		clear_buffer_dirty(bhs[i_bhs]);
err_out:
	/*
	 * Current state: all buffers are clean, unlocked, and uptodate.
	 * The caller should mark the base inode as bad so that no more i/o
	 * happens.  ->clear_inode() will still be invoked so all extent inodes
	 * and other allocated memory will be freed.
	 */
	if (err == -ENOMEM) {
		ntfs_error(vol->sb, "Not enough memory to write mft record.  "
				"Redirtying so the write is retried later.");
		mark_mft_record_dirty(ni);
		err = 0;
	}
	return err;
}

/**
 * ntfs_may_write_mft_record - check if an mft record may be written out
 * @vol:	[IN]  ntfs volume on which the mft record to check resides
 * @mft_no:	[IN]  mft record number of the mft record to check
 * @m:		[IN]  mapped mft record to check
 * @locked_ni:	[OUT] caller has to unlock this ntfs inode if one is returned
 *
 * Check if the mapped (base or extent) mft record @m with mft record number
 * @mft_no belonging to the ntfs volume @vol may be written out.  If necessary
 * and possible the ntfs inode of the mft record is locked and the base vfs
 * inode is pinned.  The locked ntfs inode is then returned in @locked_ni.  The
 * caller is responsible for unlocking the ntfs inode and unpinning the base
 * vfs inode.
 *
 * Return TRUE if the mft record may be written out and FALSE if not.
 *
 * The caller has locked the page and cleared the uptodate flag on it which
 * means that we can safely write out any dirty mft records that do not have
 * their inodes in icache as determined by ilookup5() as anyone
 * opening/creating such an inode would block when attempting to map the mft
 * record in read_cache_page() until we are finished with the write out.
 *
 * Here is a description of the tests we perform:
 *
 * If the inode is found in icache we know the mft record must be a base mft
 * record.  If it is dirty, we do not write it and return FALSE as the vfs
 * inode write paths will result in the access times being updated which would
 * cause the base mft record to be redirtied and written out again.  (We know
 * the access time update will modify the base mft record because Windows
 * chkdsk complains if the standard information attribute is not in the base
 * mft record.)
 *
 * If the inode is in icache and not dirty, we attempt to lock the mft record
 * and if we find the lock was already taken, it is not safe to write the mft
 * record and we return FALSE.
 *
 * If we manage to obtain the lock we have exclusive access to the mft record,
 * which also allows us safe writeout of the mft record.  We then set
 * @locked_ni to the locked ntfs inode and return TRUE.
 *
 * Note we cannot just lock the mft record and sleep while waiting for the lock
 * because this would deadlock due to lock reversal (normally the mft record is
 * locked before the page is locked but we already have the page locked here
 * when we try to lock the mft record).
 *
 * If the inode is not in icache we need to perform further checks.
 *
 * If the mft record is not a FILE record or it is a base mft record, we can
 * safely write it and return TRUE.
 *
 * We now know the mft record is an extent mft record.  We check if the inode
 * corresponding to its base mft record is in icache and obtain a reference to
 * it if it is.  If it is not, we can safely write it and return TRUE.
 *
 * We now have the base inode for the extent mft record.  We check if it has an
 * ntfs inode for the extent mft record attached and if not it is safe to write
 * the extent mft record and we return TRUE.
 *
 * The ntfs inode for the extent mft record is attached to the base inode so we
 * attempt to lock the extent mft record and if we find the lock was already
 * taken, it is not safe to write the extent mft record and we return FALSE.
 *
 * If we manage to obtain the lock we have exclusive access to the extent mft
 * record, which also allows us safe writeout of the extent mft record.  We
 * set the ntfs inode of the extent mft record clean and then set @locked_ni to
 * the now locked ntfs inode and return TRUE.
 *
 * Note, the reason for actually writing dirty mft records here and not just
 * relying on the vfs inode dirty code paths is that we can have mft records
 * modified without them ever having actual inodes in memory.  Also we can have
 * dirty mft records with clean ntfs inodes in memory.  None of the described
 * cases would result in the dirty mft records being written out if we only
 * relied on the vfs inode dirty code paths.  And these cases can really occur
 * during allocation of new mft records and in particular when the
 * initialized_size of the $MFT/$DATA attribute is extended and the new space
 * is initialized using ntfs_mft_record_format().  The clean inode can then
 * appear if the mft record is reused for a new inode before it got written
 * out.
 */
BOOL ntfs_may_write_mft_record(ntfs_volume *vol, const unsigned long mft_no,
		const MFT_RECORD *m, ntfs_inode **locked_ni)
{
	struct super_block *sb = vol->sb;
	struct inode *mft_vi = vol->mft_ino;
	struct inode *vi;
	ntfs_inode *ni, *eni, **extent_nis;
	int i;
	ntfs_attr na;

	ntfs_debug("Entering for inode 0x%lx.", mft_no);
	/*
	 * Normally we do not return a locked inode so set @locked_ni to NULL.
	 */
	BUG_ON(!locked_ni);
	*locked_ni = NULL;
	/*
	 * Check if the inode corresponding to this mft record is in the VFS
	 * inode cache and obtain a reference to it if it is.
	 */
	ntfs_debug("Looking for inode 0x%lx in icache.", mft_no);
	na.mft_no = mft_no;
	na.name = NULL;
	na.name_len = 0;
	na.type = AT_UNUSED;
	/*
	 * For inode 0, i.e. $MFT itself, we cannot use ilookup5() from here or
	 * we deadlock because the inode is already locked by the kernel
	 * (fs/fs-writeback.c::__sync_single_inode()) and ilookup5() waits
	 * until the inode is unlocked before returning it and it never gets
	 * unlocked because ntfs_should_write_mft_record() never returns.  )-:
	 * Fortunately, we have inode 0 pinned in icache for the duration of
	 * the mount so we can access it directly.
	 */
	if (!mft_no) {
		/* Balance the below iput(). */
		vi = igrab(mft_vi);
		BUG_ON(vi != mft_vi);
	} else
		vi = ilookup5(sb, mft_no, (test_t)ntfs_test_inode, &na);
	if (vi) {
		ntfs_debug("Base inode 0x%lx is in icache.", mft_no);
		/* The inode is in icache. */
		ni = NTFS_I(vi);
		/* Take a reference to the ntfs inode. */
		atomic_inc(&ni->count);
		/* If the inode is dirty, do not write this record. */
		if (NInoDirty(ni)) {
			ntfs_debug("Inode 0x%lx is dirty, do not write it.",
					mft_no);
			atomic_dec(&ni->count);
			iput(vi);
			return FALSE;
		}
		ntfs_debug("Inode 0x%lx is not dirty.", mft_no);
		/* The inode is not dirty, try to take the mft record lock. */
		if (unlikely(down_trylock(&ni->mrec_lock))) {
			ntfs_debug("Mft record 0x%lx is already locked, do "
					"not write it.", mft_no);
			atomic_dec(&ni->count);
			iput(vi);
			return FALSE;
		}
		ntfs_debug("Managed to lock mft record 0x%lx, write it.",
				mft_no);
		/*
		 * The write has to occur while we hold the mft record lock so
		 * return the locked ntfs inode.
		 */
		*locked_ni = ni;
		return TRUE;
	}
	ntfs_debug("Inode 0x%lx is not in icache.", mft_no);
	/* The inode is not in icache. */
	/* Write the record if it is not a mft record (type "FILE"). */
	if (!ntfs_is_mft_record(m->magic)) {
		ntfs_debug("Mft record 0x%lx is not a FILE record, write it.",
				mft_no);
		return TRUE;
	}
	/* Write the mft record if it is a base inode. */
	if (!m->base_mft_record) {
		ntfs_debug("Mft record 0x%lx is a base record, write it.",
				mft_no);
		return TRUE;
	}
	/*
	 * This is an extent mft record.  Check if the inode corresponding to
	 * its base mft record is in icache and obtain a reference to it if it
	 * is.
	 */
	na.mft_no = MREF_LE(m->base_mft_record);
	ntfs_debug("Mft record 0x%lx is an extent record.  Looking for base "
			"inode 0x%lx in icache.", mft_no, na.mft_no);
	vi = ilookup5(sb, na.mft_no, (test_t)ntfs_test_inode, &na);
	if (!vi) {
		/*
		 * The base inode is not in icache, write this extent mft
		 * record.
		 */
		ntfs_debug("Base inode 0x%lx is not in icache, write the "
				"extent record.", na.mft_no);
		return TRUE;
	}
	ntfs_debug("Base inode 0x%lx is in icache.", na.mft_no);
	/*
	 * The base inode is in icache.  Check if it has the extent inode
	 * corresponding to this extent mft record attached.
	 */
	ni = NTFS_I(vi);
	down(&ni->extent_lock);
	if (ni->nr_extents <= 0) {
		/*
		 * The base inode has no attached extent inodes, write this
		 * extent mft record.
		 */
		up(&ni->extent_lock);
		iput(vi);
		ntfs_debug("Base inode 0x%lx has no attached extent inodes, "
				"write the extent record.", na.mft_no);
		return TRUE;
	}
	/* Iterate over the attached extent inodes. */
	extent_nis = ni->ext.extent_ntfs_inos;
	for (eni = NULL, i = 0; i < ni->nr_extents; ++i) {
		if (mft_no == extent_nis[i]->mft_no) {
			/*
			 * Found the extent inode corresponding to this extent
			 * mft record.
			 */
			eni = extent_nis[i];
			break;
		}
	}
	/*
	 * If the extent inode was not attached to the base inode, write this
	 * extent mft record.
	 */
	if (!eni) {
		up(&ni->extent_lock);
		iput(vi);
		ntfs_debug("Extent inode 0x%lx is not attached to its base "
				"inode 0x%lx, write the extent record.",
				mft_no, na.mft_no);
		return TRUE;
	}
	ntfs_debug("Extent inode 0x%lx is attached to its base inode 0x%lx.",
			mft_no, na.mft_no);
	/* Take a reference to the extent ntfs inode. */
	atomic_inc(&eni->count);
	up(&ni->extent_lock);
	/*
	 * Found the extent inode coresponding to this extent mft record.
	 * Try to take the mft record lock.
	 */
	if (unlikely(down_trylock(&eni->mrec_lock))) {
		atomic_dec(&eni->count);
		iput(vi);
		ntfs_debug("Extent mft record 0x%lx is already locked, do "
				"not write it.", mft_no);
		return FALSE;
	}
	ntfs_debug("Managed to lock extent mft record 0x%lx, write it.",
			mft_no);
	if (NInoTestClearDirty(eni))
		ntfs_debug("Extent inode 0x%lx is dirty, marking it clean.",
				mft_no);
	/*
	 * The write has to occur while we hold the mft record lock so return
	 * the locked extent ntfs inode.
	 */
	*locked_ni = eni;
	return TRUE;
}

static const char *es = "  Leaving inconsistent metadata.  Unmount and run "
		"chkdsk.";

/**
 * ntfs_extent_mft_record_free - free an extent mft record on an ntfs volume
 * @ni:		ntfs inode of the mapped extent mft record to free
 * @m:		mapped extent mft record of the ntfs inode @ni
 *
 * Free the mapped extent mft record @m of the extent ntfs inode @ni.
 *
 * Note that this function unmaps the mft record and closes and destroys @ni
 * internally and hence you cannot use either @ni nor @m any more after this
 * function returns success.
 *
 * On success return 0 and on error return -errno.  @ni and @m are still valid
 * in this case and have not been freed.
 *
 * For some errors an error message is displayed and the success code 0 is
 * returned and the volume is then left dirty on umount.  This makes sense in
 * case we could not rollback the changes that were already done since the
 * caller no longer wants to reference this mft record so it does not matter to
 * the caller if something is wrong with it as long as it is properly detached
 * from the base inode.
 */
int ntfs_extent_mft_record_free(ntfs_inode *ni, MFT_RECORD *m)
{
	unsigned long mft_no = ni->mft_no;
	ntfs_volume *vol = ni->vol;
	ntfs_inode *base_ni;
	ntfs_inode **extent_nis;
	int i, err;
	le16 old_seq_no;
	u16 seq_no;
	
	BUG_ON(NInoAttr(ni));
	BUG_ON(ni->nr_extents != -1);

	down(&ni->extent_lock);
	base_ni = ni->ext.base_ntfs_ino;
	up(&ni->extent_lock);

	BUG_ON(base_ni->nr_extents <= 0);

	ntfs_debug("Entering for extent inode 0x%lx, base inode 0x%lx.\n",
			mft_no, base_ni->mft_no);

	down(&base_ni->extent_lock);

	/* Make sure we are holding the only reference to the extent inode. */
	if (atomic_read(&ni->count) > 2) {
		ntfs_error(vol->sb, "Tried to free busy extent inode 0x%lx, "
				"not freeing.", base_ni->mft_no);
		up(&base_ni->extent_lock);
		return -EBUSY;
	}

	/* Dissociate the ntfs inode from the base inode. */
	extent_nis = base_ni->ext.extent_ntfs_inos;
	err = -ENOENT;
	for (i = 0; i < base_ni->nr_extents; i++) {
		if (ni != extent_nis[i])
			continue;
		extent_nis += i;
		base_ni->nr_extents--;
		memmove(extent_nis, extent_nis + 1, (base_ni->nr_extents - i) *
				sizeof(ntfs_inode*));
		err = 0;
		break;
	}

	up(&base_ni->extent_lock);

	if (unlikely(err)) {
		ntfs_error(vol->sb, "Extent inode 0x%lx is not attached to "
				"its base inode 0x%lx.", mft_no,
				base_ni->mft_no);
		BUG();
	}

	/*
	 * The extent inode is no longer attached to the base inode so no one
	 * can get a reference to it any more.
	 */

	/* Mark the mft record as not in use. */
	m->flags &= const_cpu_to_le16(~const_le16_to_cpu(MFT_RECORD_IN_USE));

	/* Increment the sequence number, skipping zero, if it is not zero. */
	old_seq_no = m->sequence_number;
	seq_no = le16_to_cpu(old_seq_no);
	if (seq_no == 0xffff)
		seq_no = 1;
	else if (seq_no)
		seq_no++;
	m->sequence_number = cpu_to_le16(seq_no);

	/*
	 * Set the ntfs inode dirty and write it out.  We do not need to worry
	 * about the base inode here since whatever caused the extent mft
	 * record to be freed is guaranteed to do it already.
	 */
	NInoSetDirty(ni);
	err = write_mft_record(ni, m, 0);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Failed to write mft record 0x%lx, not "
				"freeing.", mft_no);
		goto rollback;
	}
rollback_error:
	/* Unmap and throw away the now freed extent inode. */
	unmap_extent_mft_record(ni);
	ntfs_clear_extent_inode(ni);

	/* Clear the bit in the $MFT/$BITMAP corresponding to this record. */
	down_write(&vol->mftbmp_lock);
	err = ntfs_bitmap_clear_bit(vol->mftbmp_ino, mft_no);
	up_write(&vol->mftbmp_lock);
	if (unlikely(err)) {
		/*
		 * The extent inode is gone but we failed to deallocate it in
		 * the mft bitmap.  Just emit a warning and leave the volume
		 * dirty on umount.
		 */
		ntfs_error(vol->sb, "Failed to clear bit in mft bitmap.%s", es);
		NVolSetErrors(vol);
	}
	return 0;
rollback:
	/* Rollback what we did... */
	down(&base_ni->extent_lock);
	extent_nis = base_ni->ext.extent_ntfs_inos;
	if (!(base_ni->nr_extents & 3)) {
		int new_size = (base_ni->nr_extents + 4) * sizeof(ntfs_inode*);

		extent_nis = (ntfs_inode**)kmalloc(new_size, GFP_NOFS);
		if (unlikely(!extent_nis)) {
			ntfs_error(vol->sb, "Failed to allocate internal "
					"buffer during rollback.%s", es);
			up(&base_ni->extent_lock);
			NVolSetErrors(vol);
			goto rollback_error;
		}
		if (base_ni->nr_extents) {
			BUG_ON(!base_ni->ext.extent_ntfs_inos);
			memcpy(extent_nis, base_ni->ext.extent_ntfs_inos,
					new_size - 4 * sizeof(ntfs_inode*));
			kfree(base_ni->ext.extent_ntfs_inos);
		}
		base_ni->ext.extent_ntfs_inos = extent_nis;
	}
	m->flags |= MFT_RECORD_IN_USE;
	m->sequence_number = old_seq_no;
	extent_nis[base_ni->nr_extents++] = ni;
	up(&base_ni->extent_lock);
	mark_mft_record_dirty(ni);
	return err;
}
#endif /* NTFS_RW */
