/*
 *  cache.c
 *
 * Copyright (C) 1997 by Bill Hawes
 *
 * Routines to support directory cacheing using the page cache.
 * Right now this only works for smbfs, but will be generalized
 * for use with other filesystems.
 *
 * Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/dirent.h>
#include <linux/smb_fs.h>
#include <linux/pagemap.h>

#include <asm/page.h>

#include "smb_debug.h"


static inline struct address_space * 
get_cache_inode(struct cache_head *cachep)
{
	return page_cache_entry((unsigned long) cachep)->mapping;
}

/*
 * Try to reassemble the old dircache. If we fail - set ->valid to 0.
 * In any case, get at least the page at offset 0 (with ->valid==0 if
 * the old one didn't make it, indeed).
 */
struct cache_head *
smb_get_dircache(struct dentry * dentry)
{
	struct address_space * mapping = &dentry->d_inode->i_data;
	struct cache_head * cachep = NULL;
	struct page *page;

	page = find_lock_page(mapping, 0);
	if (!page) {
		/* Sorry, not even page 0 around */
		page = grab_cache_page(mapping, 0);
		if (!page)
			goto out;
		cachep = kmap(page);
		memset((char*)cachep, 0, PAGE_SIZE);
		goto out;
	}
	cachep = kmap(page);
	if (cachep->valid) {
		/*
		 * OK, at least the page 0 survived and seems to be promising.
		 * Let's try to reassemble the rest.
		 */
		struct cache_index * index = cachep->index;
		unsigned long offset;
		int i;

		for (offset = 0, i = 0; i < cachep->pages; i++, index++) {
			offset += PAGE_SIZE;
			page = find_lock_page(mapping,offset>>PAGE_CACHE_SHIFT);
			if (!page) {
				/* Alas, poor Yorick */
				cachep->valid = 0;
				goto out;
			}
			index->block = kmap(page);
		}
	}
out:
	return cachep;
}

/*
 * Unlock and release the data blocks.
 */
static void
smb_free_cache_blocks(struct cache_head * cachep)
{
	struct cache_index * index = cachep->index;
	struct page * page;
	int i;

	VERBOSE("freeing %d blocks\n", cachep->pages);
	for (i = 0; i < cachep->pages; i++, index++) {
		if (!index->block)
			continue;
		page = page_cache_entry((unsigned long) index->block);
		index->block = NULL;
		kunmap(page);
		UnlockPage(page);
		page_cache_release(page);
	}
}

/*
 * Unlocks and releases the dircache.
 */
void
smb_free_dircache(struct cache_head * cachep)
{
	struct page *page;
	VERBOSE("freeing cache\n");
	smb_free_cache_blocks(cachep);
	page = page_cache_entry((unsigned long) cachep);
	kunmap(page);
	UnlockPage(page);
	page_cache_release(page);
}

/*
 * Initializes the dircache. We release any existing data blocks,
 * and then clear the cache_head structure.
 */
void
smb_init_dircache(struct cache_head * cachep)
{
	VERBOSE("initializing cache, %d blocks\n", cachep->pages);
	smb_free_cache_blocks(cachep);
	memset(cachep, 0, sizeof(struct cache_head));
}

/*
 * Add a new entry to the cache.  This assumes that the
 * entries are coming in order and are added to the end.
 */
void
smb_add_to_cache(struct cache_head * cachep, struct cache_dirent *entry,
			off_t fpos)
{
	struct address_space * mapping = get_cache_inode(cachep);
	struct cache_index * index;
	struct cache_block * block;
	struct page *page;
	unsigned long page_off;
	unsigned int nent, offset, len = entry->len;
	unsigned int needed = len + sizeof(struct cache_entry);

	VERBOSE("cache %p, status %d, adding %.*s at %ld\n",
		mapping, cachep->status, entry->len, entry->name, fpos);

	/*
	 * Don't do anything if we've had an error ...
	 */
	if (cachep->status)
		goto out;

	index = &cachep->index[cachep->idx];
	if (!index->block)
		goto get_block;

	/* space available? */
	if (needed < index->space) {
	add_entry:
		nent = index->num_entries;
		index->num_entries++;
		index->space -= needed;
		offset = index->space + 
			 index->num_entries * sizeof(struct cache_entry);
		block = index->block;
		memcpy(&block->cb_data.names[offset], entry->name, len);
		block->cb_data.table[nent].namelen = len;
		block->cb_data.table[nent].offset = offset;
		block->cb_data.table[nent].ino = entry->ino;
		cachep->entries++;

		VERBOSE("added entry %.*s, len=%d, pos=%ld, entries=%d\n",
			entry->len, entry->name, len, fpos, cachep->entries);
		return;
	}
	/*
	 * This block is full ... advance the index.
	 */
	cachep->idx++;
	if (cachep->idx > NINDEX) /* not likely */
		goto out_full;
	index++;
	/*
	 * Get the next cache block. We don't care for its contents.
	 */
get_block:
	cachep->pages++;
	page_off = PAGE_SIZE + (cachep->idx << PAGE_SHIFT);
	page = grab_cache_page(mapping, page_off>>PAGE_CACHE_SHIFT);
	if (page) {
		block = kmap(page);
		index->block = block;
		index->space = PAGE_SIZE;
		goto add_entry;
	}
	/*
	 * On failure, just set the return status ...
	 */
out_full:
	cachep->status = -ENOMEM;
out:
	return;
}

int
smb_find_in_cache(struct cache_head * cachep, off_t pos, 
		struct cache_dirent *entry)
{
	struct cache_index * index = cachep->index;
	struct cache_block * block;
	unsigned int i, nent, offset = 0;
	off_t next_pos = 2;

	VERBOSE("smb_find_in_cache: cache %p, looking for pos=%ld\n",
		cachep, pos);
	for (i = 0; i < cachep->pages; i++, index++)
	{
		if (pos < next_pos)
			break;
		nent = pos - next_pos;
		next_pos += index->num_entries;
		if (pos >= next_pos)
			continue;
		/*
		 * The entry is in this block. Note: we return
		 * then name as a reference with _no_ null byte.
		 */
		block = index->block;
		entry->ino = block->cb_data.table[nent].ino;
		entry->len = block->cb_data.table[nent].namelen;
		offset = block->cb_data.table[nent].offset;
		entry->name = &block->cb_data.names[offset];

		VERBOSE("found %.*s, len=%d, pos=%ld\n",
			entry->len, entry->name, entry->len, pos);
		break;
	}
	return offset;
}

int
smb_refill_dircache(struct cache_head * cachep, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	int result;

	VERBOSE("smb_refill_dircache: cache %s/%s, blocks=%d\n",
		DENTRY_PATH(dentry), cachep->pages);
	/*
	 * Fill the cache, starting at position 2.
	 */
retry:
	inode->u.smbfs_i.cache_valid |= SMB_F_CACHEVALID;
	result = smb_proc_readdir(dentry, 2, cachep);
	if (result < 0)
	{
		PARANOIA("readdir failed, result=%d\n", result);
		goto out;
	}

	/*
	 * Check whether the cache was invalidated while
	 * we were doing the scan ...
	 */
	if (!(inode->u.smbfs_i.cache_valid & SMB_F_CACHEVALID))
	{
		PARANOIA("cache invalidated, retrying\n");
		goto retry;
	}

	result = cachep->status;
	if (!result)
	{
		cachep->valid = 1;
	}
	VERBOSE("cache %s/%s status=%d, entries=%d\n",
		DENTRY_PATH(dentry), cachep->status, cachep->entries);
out:
	return result;
}

void
smb_invalid_dir_cache(struct inode * dir)
{
	/*
	 * Get rid of any unlocked pages, and clear the
	 * 'valid' flag in case a scan is in progress.
	 */
	invalidate_inode_pages(dir);
	dir->u.smbfs_i.cache_valid &= ~SMB_F_CACHEVALID;
	dir->u.smbfs_i.oldmtime = 0;
}
