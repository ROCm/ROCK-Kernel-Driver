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

#include <linux/swap.h>

#include "ntfs.h"

/**
 * __format_mft_record - initialize an empty mft record
 * @m:		mapped, pinned and locked for writing mft record
 * @size:	size of the mft record
 * @rec_no:	mft record number / inode number
 *
 * Private function to initialize an empty mft record. Use one of the two
 * provided format_mft_record() functions instead.
 */
static void __format_mft_record(MFT_RECORD *m, const int size,
		const unsigned long rec_no)
{
	ATTR_RECORD *a;

	memset(m, 0, size);
	m->magic = magic_FILE;
	/* Aligned to 2-byte boundary. */
	m->usa_ofs = cpu_to_le16((sizeof(MFT_RECORD) + 1) & ~1);
	m->usa_count = cpu_to_le16(size / NTFS_BLOCK_SIZE + 1);
	/* Set the update sequence number to 1. */
	*(u16*)((char*)m + ((sizeof(MFT_RECORD) + 1) & ~1)) = cpu_to_le16(1);
	m->lsn = cpu_to_le64(0LL);
	m->sequence_number = cpu_to_le16(1);
	m->link_count = cpu_to_le16(0);
	/* Aligned to 8-byte boundary. */
	m->attrs_offset = cpu_to_le16((le16_to_cpu(m->usa_ofs) +
			(le16_to_cpu(m->usa_count) << 1) + 7) & ~7);
	m->flags = cpu_to_le16(0);
	/*
	 * Using attrs_offset plus eight bytes (for the termination attribute),
	 * aligned to 8-byte boundary.
	 */
	m->bytes_in_use = cpu_to_le32((le16_to_cpu(m->attrs_offset) + 8 + 7) &
			~7);
	m->bytes_allocated = cpu_to_le32(size);
	m->base_mft_record = cpu_to_le64((MFT_REF)0);
	m->next_attr_instance = cpu_to_le16(0);
	a = (ATTR_RECORD*)((char*)m + le16_to_cpu(m->attrs_offset));
	a->type = AT_END;
	a->length = cpu_to_le32(0);
}

/**
 * format_mft_record - initialize an empty mft record
 * @ni:		ntfs inode of mft record
 * @mft_rec:	mapped, pinned and locked mft record (optional)
 *
 * Initialize an empty mft record. This is used when extending the MFT.
 *
 * If @mft_rec is NULL, we call map_mft_record() to obtain the
 * record and we unmap it again when finished.
 *
 * We return 0 on success or -errno on error.
 */
int format_mft_record(ntfs_inode *ni, MFT_RECORD *mft_rec)
{
	MFT_RECORD *m;

	if (mft_rec)
		m = mft_rec;
	else {
		m = map_mft_record(ni);
		if (IS_ERR(m))
			return PTR_ERR(m);
	}
	__format_mft_record(m, ni->vol->mft_record_size, ni->mft_no);
	if (!mft_rec) {
		// FIXME: Need to set the mft record dirty!
		unmap_mft_record(ni);
	}
	return 0;
}

/**
 * ntfs_readpage - external declaration, function is in fs/ntfs/aops.c
 */
extern int ntfs_readpage(struct file *, struct page *);

#ifdef NTFS_RW
/**
 * ntfs_mft_writepage - forward declaration, function is further below
 */
static int ntfs_mft_writepage(struct page *page, struct writeback_control *wbc);
#endif /* NTFS_RW */

/**
 * ntfs_mft_aops - address space operations for access to $MFT
 *
 * Address space operations for access to $MFT. This allows us to simply use
 * ntfs_map_page() in map_mft_record_page().
 */
struct address_space_operations ntfs_mft_aops = {
	.readpage	= ntfs_readpage,	/* Fill page with data. */
	.sync_page	= block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
#ifdef NTFS_RW
	.writepage	= ntfs_mft_writepage,	/* Write out the dirty mft
						   records in a page. */
	.set_page_dirty	= __set_page_dirty_nobuffers,	/* Set the page dirty
						   without touching the buffers
						   belonging to the page. */
#endif /* NTFS_RW */
};

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
 * @mref:	mft reference of the extent inode to load (in little endian)
 * @ntfs_ino:	on successful return, pointer to the ntfs_inode structure
 *
 * Load the extent mft record @mref and attach it to its base inode @base_ni.
 * Return the mapped extent mft record if IS_ERR(result) is false. Otherwise
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
	unsigned long mft_no = MREF_LE(mref);
	u16 seq_no = MSEQNO_LE(mref);
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
	if (unlikely(IS_ERR(m))) {
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
		if (base_ni->ext.extent_ntfs_inos) {
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
	struct page *page = ni->page;
	ntfs_inode *base_ni;

	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);
	BUG_ON(!page);
	BUG_ON(NInoAttr(ni));

	/*
	 * Set the page containing the mft record dirty.  This also marks the
	 * $MFT inode dirty (I_DIRTY_PAGES).
	 */
	__set_page_dirty_nobuffers(page);

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
 * sync_mft_mirror_umount - synchronise an mft record to the mft mirror
 * @ni:		ntfs inode whose mft record to synchronize
 * @m:		mapped, mst protected (extent) mft record to synchronize
 *
 * Write the mapped, mst protected (extent) mft record @m described by the
 * (regular or extent) ntfs inode @ni to the mft mirror ($MFTMirr) bypassing
 * the page cache and the $MFTMirr inode itself.
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
static int sync_mft_mirror_umount(ntfs_inode *ni, MFT_RECORD *m)
{
	ntfs_volume *vol = ni->vol;

	BUG_ON(vol->mftmirr_ino);
	ntfs_error(vol->sb, "Umount time mft mirror syncing is not "
			"implemented yet.  %s", ntfs_please_email);
	return -EOPNOTSUPP;
}

/**
 * sync_mft_mirror - synchronize an mft record to the mft mirror
 * @ni:		ntfs inode whose mft record to synchronize
 * @m:		mapped, mst protected (extent) mft record to synchronize
 * @sync:	if true, wait for i/o completion
 *
 * Write the mapped, mst protected (extent) mft record @m described by the
 * (regular or extent) ntfs inode @ni to the mft mirror ($MFTMirr).
 *
 * On success return 0.  On error return -errno and set the volume errors flag
 * in the ntfs_volume to which @ni belongs.
 *
 * NOTE:  We always perform synchronous i/o and ignore the @sync parameter.
 *
 * TODO:  If @sync is false, want to do truly asynchronous i/o, i.e. just
 * schedule i/o via ->writepage or do it via kntfsd or whatever.
 */
static int sync_mft_mirror(ntfs_inode *ni, MFT_RECORD *m, int sync)
{
	ntfs_volume *vol = ni->vol;
	struct page *page;
	unsigned int blocksize = vol->sb->s_blocksize;
	int max_bhs = vol->mft_record_size / blocksize;
	struct buffer_head *bhs[max_bhs];
	struct buffer_head *bh, *head;
	u8 *kmirr;
	unsigned int block_start, block_end, m_start, m_end;
	int i_bhs, nr_bhs, err = 0;

	ntfs_debug("Entering for inode 0x%lx.", ni->mft_no);
	BUG_ON(!max_bhs);
	if (unlikely(!vol->mftmirr_ino)) {
		/* This could happen during umount... */
		err = sync_mft_mirror_umount(ni, m);
		if (likely(!err))
			return err;
		goto err_out;
	}
	/* Get the page containing the mirror copy of the mft record @m. */
	page = ntfs_map_page(vol->mftmirr_ino->i_mapping, ni->mft_no >>
			(PAGE_CACHE_SHIFT - vol->mft_record_size_bits));
	if (unlikely(IS_ERR(page))) {
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
	/* The address in the page of the mirror copy of the mft record @m. */
	kmirr = page_address(page) + ((ni->mft_no << vol->mft_record_size_bits)
			& ~PAGE_CACHE_MASK);
	/* Copy the mst protected mft record to the mirror. */
	memcpy(kmirr, m, vol->mft_record_size);
	/* Make sure we have mapped buffers. */
	if (!page_has_buffers(page)) {
no_buffers_err_out:
		ntfs_error(vol->sb, "Writing mft mirror records without "
				"existing buffers is not implemented yet.  %s",
				ntfs_please_email);
		err = -EOPNOTSUPP;
		goto unlock_err_out;
	}
	bh = head = page_buffers(page);
	if (!bh)
		goto no_buffers_err_out;
	nr_bhs = 0;
	block_start = 0;
	m_start = kmirr - (u8*)page_address(page);
	m_end = m_start + vol->mft_record_size;
	do {
		block_end = block_start + blocksize;
		/*
		 * If the buffer is outside the mft record, just skip it,
		 * clearing it if it is dirty to make sure it is not written
		 * out.  It should never be marked dirty but better be safe.
		 */
		if ((block_end <= m_start) || (block_start >= m_end)) {
			if (buffer_dirty(bh)) {
				ntfs_warning(vol->sb, "Clearing dirty mft "
						"record page buffer.  %s",
						ntfs_please_email);
				clear_buffer_dirty(bh);
			}
			continue;
		}
		if (!buffer_mapped(bh)) {
			ntfs_error(vol->sb, "Writing mft mirror records "
					"without existing mapped buffers is "
					"not implemented yet.  %s",
					ntfs_please_email);
			err = -EOPNOTSUPP;
			continue;
		}
		if (!buffer_uptodate(bh)) {
			ntfs_error(vol->sb, "Writing mft mirror records "
					"without existing uptodate buffers is "
					"not implemented yet.  %s",
					ntfs_please_email);
			err = -EOPNOTSUPP;
			continue;
		}
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
				 * Set the buffer uptodate so the page & buffer
				 * states don't become out of sync.
				 */
				if (PageUptodate(page))
					set_buffer_uptodate(tbh);
			}
		}
	} else /* if (unlikely(err)) */ {
		/* Clean the buffers. */
		for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++)
			clear_buffer_dirty(bhs[i_bhs]);
	}
unlock_err_out:
	/* Current state: all buffers are clean, unlocked, and uptodate. */
	/* Remove the mst protection fixups again. */
	post_write_mst_fixup((NTFS_RECORD*)kmirr);
	flush_dcache_page(page);
	unlock_page(page);
	ntfs_unmap_page(page);
	if (unlikely(err)) {
		/* I/O error during writing.  This is really bad! */
		ntfs_error(vol->sb, "I/O error while writing mft mirror "
				"record 0x%lx!  You should unmount the volume "
				"and run chkdsk or ntfsfix.", ni->mft_no);
		goto err_out;
	}
	ntfs_debug("Done.");
	return 0;
err_out:
	ntfs_error(vol->sb, "Failed to synchronize $MFTMirr (error code %i).  "
			"Volume will be left marked dirty on umount.  Run "
			"ntfsfix on the partition after umounting to correct "
			"this.", -err);
	/* We don't want to clear the dirty bit on umount. */
	NVolSetErrors(vol);
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
	/* Make sure we have mapped buffers. */
	if (!page_has_buffers(page)) {
no_buffers_err_out:
		ntfs_error(vol->sb, "Writing mft records without existing "
				"buffers is not implemented yet.  %s",
				ntfs_please_email);
		err = -EOPNOTSUPP;
		goto err_out;
	}
	bh = head = page_buffers(page);
	if (!bh)
		goto no_buffers_err_out;
	nr_bhs = 0;
	block_start = 0;
	m_start = ni->page_ofs;
	m_end = m_start + vol->mft_record_size;
	do {
		block_end = block_start + blocksize;
		/*
		 * If the buffer is outside the mft record, just skip it,
		 * clearing it if it is dirty to make sure it is not written
		 * out.  It should never be marked dirty but better be safe.
		 */
		if ((block_end <= m_start) || (block_start >= m_end)) {
			if (buffer_dirty(bh)) {
				ntfs_warning(vol->sb, "Clearing dirty mft "
						"record page buffer.  %s",
						ntfs_please_email);
				clear_buffer_dirty(bh);
			}
			continue;
		}
		if (!buffer_mapped(bh)) {
			ntfs_error(vol->sb, "Writing mft records without "
					"existing mapped buffers is not "
					"implemented yet.  %s",
					ntfs_please_email);
			err = -EOPNOTSUPP;
			continue;
		}
		if (!buffer_uptodate(bh)) {
			ntfs_error(vol->sb, "Writing mft records without "
					"existing uptodate buffers is not "
					"implemented yet.  %s",
					ntfs_please_email);
			err = -EOPNOTSUPP;
			continue;
		}
		BUG_ON(!nr_bhs && (m_start != block_start));
		BUG_ON(nr_bhs >= max_bhs);
		bhs[nr_bhs++] = bh;
		BUG_ON((nr_bhs >= max_bhs) && (m_end != block_end));
	} while (block_start = block_end, (bh = bh->b_this_page) != head);
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
		if (buffer_dirty(tbh))
			clear_buffer_dirty(tbh);
		get_bh(tbh);
		tbh->b_end_io = end_buffer_write_sync;
		submit_bh(WRITE, tbh);
	}
	/* Synchronize the mft mirror now if not @sync. */
	if (!sync && ni->mft_no < vol->mftmirr_size)
		sync_mft_mirror(ni, m, sync);
	/* Wait on i/o completion of buffers. */
	for (i_bhs = 0; i_bhs < nr_bhs; i_bhs++) {
		struct buffer_head *tbh = bhs[i_bhs];

		wait_on_buffer(tbh);
		if (unlikely(!buffer_uptodate(tbh))) {
			err = -EIO;
			/*
			 * Set the buffer uptodate so the page & buffer states
			 * don't become out of sync.
			 */
			if (PageUptodate(page))
				set_buffer_uptodate(tbh);
		}
	}
	/* If @sync, now synchronize the mft mirror. */
	if (sync && ni->mft_no < vol->mftmirr_size)
		sync_mft_mirror(ni, m, sync);
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
 * ntfs_mft_writepage - check if a metadata page contains dirty mft records
 * @page:	metadata page possibly containing dirty mft records
 * @wbc:	writeback control structure
 *
 * This is called from the VM when it wants to have a dirty $MFT/$DATA metadata
 * page cache page cleaned.  The VM has already locked the page and marked it
 * clean.  Instead of writing the page as a conventional ->writepage function
 * would do, we check if the page still contains any dirty mft records (it must
 * have done at some point in the past since the page was marked dirty) and if
 * none are found, i.e. all mft records are clean, we unlock the page and
 * return.  The VM is then free to do with the page as it pleases.  If on the
 * other hand we do find any dirty mft records in the page, we redirty the page
 * before unlocking it and returning so the VM knows that the page is still
 * busy and cannot be thrown out.
 *
 * Note, we do not actually write any dirty mft records here because they are
 * dirty inodes and hence will be written by the VFS inode dirty code paths.
 * There is no need to write them from the VM page dirty code paths, too and in
 * fact once we implement journalling it would be a complete nightmare having
 * two code paths leading to mft record writeout.
 */
static int ntfs_mft_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *mft_vi = page->mapping->host;
	struct super_block *sb = mft_vi->i_sb;
	ntfs_volume *vol = NTFS_SB(sb);
	u8 *maddr;
	MFT_RECORD *m;
	ntfs_inode **extent_nis;
	unsigned long mft_no;
	int nr, i, j;
	BOOL is_dirty = FALSE;

	BUG_ON(!PageLocked(page));
	BUG_ON(PageWriteback(page));
	BUG_ON(mft_vi != vol->mft_ino);
	/* The first mft record number in the page. */
	mft_no = page->index << (PAGE_CACHE_SHIFT - vol->mft_record_size_bits);
	/* Number of mft records in the page. */
	nr = PAGE_CACHE_SIZE >> vol->mft_record_size_bits;
	BUG_ON(!nr);
	ntfs_debug("Entering for %i inodes starting at 0x%lx.", nr, mft_no);
	/* Iterate over the mft records in the page looking for a dirty one. */
	maddr = (u8*)kmap(page);
	for (i = 0; i < nr; ++i, ++mft_no, maddr += vol->mft_record_size) {
		struct inode *vi;
		ntfs_inode *ni, *eni;
		ntfs_attr na;

		na.mft_no = mft_no;
		na.name = NULL;
		na.name_len = 0;
		na.type = AT_UNUSED;
		/*
		 * Check if the inode corresponding to this mft record is in
		 * the VFS inode cache and obtain a reference to it if it is.
		 */
		ntfs_debug("Looking for inode 0x%lx in icache.", mft_no);
		/*
		 * For inode 0, i.e. $MFT itself, we cannot use ilookup5() from
		 * here or we deadlock because the inode is already locked by
		 * the kernel (fs/fs-writeback.c::__sync_single_inode()) and
		 * ilookup5() waits until the inode is unlocked before
		 * returning it and it never gets unlocked because
		 * ntfs_mft_writepage() never returns.  )-:  Fortunately, we
		 * have inode 0 pinned in icache for the duration of the mount
		 * so we can access it directly.
		 */
		if (!mft_no) {
			/* Balance the below iput(). */
			vi = igrab(mft_vi);
			BUG_ON(vi != mft_vi);
		} else
			vi = ilookup5(sb, mft_no, (test_t)ntfs_test_inode, &na);
		if (vi) {
			ntfs_debug("Inode 0x%lx is in icache.", mft_no);
			/* The inode is in icache.  Check if it is dirty. */
			ni = NTFS_I(vi);
			if (!NInoDirty(ni)) {
				/* The inode is not dirty, skip this record. */
				ntfs_debug("Inode 0x%lx is not dirty, "
						"continuing search.", mft_no);
				iput(vi);
				continue;
			}
			ntfs_debug("Inode 0x%lx is dirty, aborting search.",
					mft_no);
			/* The inode is dirty, no need to search further. */
			iput(vi);
			is_dirty = TRUE;
			break;
		}
		ntfs_debug("Inode 0x%lx is not in icache.", mft_no);
		/* The inode is not in icache. */
		/* Skip the record if it is not a mft record (type "FILE"). */
		if (!ntfs_is_mft_recordp(maddr)) {
			ntfs_debug("Mft record 0x%lx is not a FILE record, "
					"continuing search.", mft_no);
			continue;
		}
		m = (MFT_RECORD*)maddr;
		/*
		 * Skip the mft record if it is not in use.  FIXME:  What about
		 * deleted/deallocated (extent) inodes?  (AIA)
		 */
		if (!(m->flags & MFT_RECORD_IN_USE)) {
			ntfs_debug("Mft record 0x%lx is not in use, "
					"continuing search.", mft_no);
			continue;
		}
		/* Skip the mft record if it is a base inode. */
		if (!m->base_mft_record) {
			ntfs_debug("Mft record 0x%lx is a base record, "
					"continuing search.", mft_no);
			continue;
		}
		/*
		 * This is an extent mft record.  Check if the inode
		 * corresponding to its base mft record is in icache.
		 */
		na.mft_no = MREF_LE(m->base_mft_record);
		ntfs_debug("Mft record 0x%lx is an extent record.  Looking "
				"for base inode 0x%lx in icache.", mft_no,
				na.mft_no);
		vi = ilookup5(sb, na.mft_no, (test_t)ntfs_test_inode,
				&na);
		if (!vi) {
			/*
			 * The base inode is not in icache.  Skip this extent
			 * mft record.
			 */
			ntfs_debug("Base inode 0x%lx is not in icache, "
					"continuing search.", na.mft_no);
			continue;
		}
		ntfs_debug("Base inode 0x%lx is in icache.", na.mft_no);
		/*
		 * The base inode is in icache.  Check if it has the extent
		 * inode corresponding to this extent mft record attached.
		 */
		ni = NTFS_I(vi);
		down(&ni->extent_lock);
		if (ni->nr_extents <= 0) {
			/*
			 * The base inode has no attached extent inodes.  Skip
			 * this extent mft record.
			 */
			up(&ni->extent_lock);
			iput(vi);
			continue;
		}
		/* Iterate over the attached extent inodes. */
		extent_nis = ni->ext.extent_ntfs_inos;
		for (eni = NULL, j = 0; j < ni->nr_extents; ++j) {
			if (mft_no == extent_nis[j]->mft_no) {
				/*
				 * Found the extent inode corresponding to this
				 * extent mft record.
				 */
				eni = extent_nis[j];
				break;
			}
		}
		/*
		 * If the extent inode was not attached to the base inode, skip
		 * this extent mft record.
		 */
		if (!eni) {
			up(&ni->extent_lock);
			iput(vi);
			continue;
		}
		/*
		 * Found the extent inode corrsponding to this extent mft
		 * record.  If it is dirty, no need to search further.
		 */
		if (NInoDirty(eni)) {
			up(&ni->extent_lock);
			iput(vi);
			is_dirty = TRUE;
			break;
		}
		/* The extent inode is not dirty, so do the next record. */
		up(&ni->extent_lock);
		iput(vi);
	}
	kunmap(page);
	/* If a dirty mft record was found, redirty the page. */
	if (is_dirty) {
		ntfs_debug("Inode 0x%lx is dirty.  Redirtying the page "
				"starting at inode 0x%lx.", mft_no,
				page->index << (PAGE_CACHE_SHIFT -
				vol->mft_record_size_bits));
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
	} else {
		/*
		 * Keep the VM happy.  This must be done otherwise the
		 * radix-tree tag PAGECACHE_TAG_DIRTY remains set even though
		 * the page is clean.
		 */
		BUG_ON(PageWriteback(page));
		set_page_writeback(page);
		unlock_page(page);
		end_page_writeback(page);
	}
	ntfs_debug("Done.");
	return 0;
}

#endif /* NTFS_RW */
