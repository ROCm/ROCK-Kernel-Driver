/*
 * Resizable virtual memory filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *		 2000 Transmeta Corp.
 *		 2000-2001 Christoph Rohland
 *		 2000-2001 SAP AG
 *		 2002 Red Hat Inc.
 *
 * This file is released under the GPL.
 */

/*
 * This virtual memory filesystem is heavily based on the ramfs. It
 * extends ramfs by the ability to use swap and honor resource limits
 * which makes it a completely usable filesystem.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/shmem_fs.h>

#include <asm/uaccess.h>

/* This magic number is used in glibc for posix shared memory */
#define TMPFS_MAGIC	0x01021994

#define ENTRIES_PER_PAGE (PAGE_CACHE_SIZE/sizeof(unsigned long))
#define BLOCKS_PER_PAGE  (PAGE_CACHE_SIZE/512)

#define SHMEM_MAX_INDEX  (SHMEM_NR_DIRECT + ENTRIES_PER_PAGE * (ENTRIES_PER_PAGE/2) * (ENTRIES_PER_PAGE+1))
#define SHMEM_MAX_BYTES  ((unsigned long long)SHMEM_MAX_INDEX << PAGE_CACHE_SHIFT)

#define VM_ACCT(size)    (((size) + PAGE_CACHE_SIZE - 1) >> PAGE_SHIFT)

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

static inline struct shmem_sb_info *SHMEM_SB(struct super_block *sb)
{
	return sb->u.generic_sbp;
}

static struct super_operations shmem_ops;
static struct address_space_operations shmem_aops;
static struct file_operations shmem_file_operations;
static struct inode_operations shmem_inode_operations;
static struct inode_operations shmem_dir_inode_operations;
static struct vm_operations_struct shmem_vm_ops;

static struct backing_dev_info shmem_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};

LIST_HEAD (shmem_inodes);
static spinlock_t shmem_ilock = SPIN_LOCK_UNLOCKED;
atomic_t shmem_nrpages = ATOMIC_INIT(0); /* Not used right now */

static void shmem_free_block(struct inode *inode)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	spin_lock(&sbinfo->stat_lock);
	sbinfo->free_blocks++;
	inode->i_blocks -= BLOCKS_PER_PAGE;
	spin_unlock(&sbinfo->stat_lock);
}

/*
 * shmem_recalc_inode - recalculate the size of an inode
 *
 * @inode: inode to recalc
 *
 * We have to calculate the free blocks since the mm can drop
 * undirtied hole pages behind our back.  Later we should be
 * able to use the releasepage method to handle this better.
 *
 * But normally   info->alloced == inode->i_mapping->nrpages + info->swapped
 * So mm freed is info->alloced - (inode->i_mapping->nrpages + info->swapped)
 *
 * It has to be called with the spinlock held.
 */
static void shmem_recalc_inode(struct inode * inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	long freed;

	freed = info->alloced - info->swapped - inode->i_mapping->nrpages;
	if (freed > 0) {
		struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
		info->alloced -= freed;
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_blocks += freed;
		inode->i_blocks -= freed*BLOCKS_PER_PAGE;
		spin_unlock(&sbinfo->stat_lock);
	}
}

/*
 * shmem_swp_entry - find the swap vector position in the info structure
 *
 * @info:  info structure for the inode
 * @index: index of the page to find
 * @page:  optional page to add to the structure. Has to be preset to
 *         all zeros
 *
 * If there is no space allocated yet it will return NULL when
 * page is 0, else it will use the page for the needed block,
 * setting it to 0 on return to indicate that it has been used.
 *
 * The swap vector is organized the following way:
 *
 * There are SHMEM_NR_DIRECT entries directly stored in the
 * shmem_inode_info structure. So small files do not need an addional
 * allocation.
 *
 * For pages with index > SHMEM_NR_DIRECT there is the pointer
 * i_indirect which points to a page which holds in the first half
 * doubly indirect blocks, in the second half triple indirect blocks:
 *
 * For an artificial ENTRIES_PER_PAGE = 4 this would lead to the
 * following layout (for SHMEM_NR_DIRECT == 16):
 *
 * i_indirect -> dir --> 16-19
 * 	      |	     +-> 20-23
 * 	      |
 * 	      +-->dir2 --> 24-27
 * 	      |	       +-> 28-31
 * 	      |	       +-> 32-35
 * 	      |	       +-> 36-39
 * 	      |
 * 	      +-->dir3 --> 40-43
 * 	       	       +-> 44-47
 * 	      	       +-> 48-51
 * 	      	       +-> 52-55
 */
static swp_entry_t *shmem_swp_entry(struct shmem_inode_info *info, unsigned long index, unsigned long *page)
{
	unsigned long offset;
	void **dir;

	if (index >= info->next_index)
		return NULL;
	if (index < SHMEM_NR_DIRECT)
		return info->i_direct+index;
	if (!info->i_indirect) {
		if (page) {
			info->i_indirect = (void *) *page;
			*page = 0;
		}
		return NULL;			/* need another page */
	}

	index -= SHMEM_NR_DIRECT;
	offset = index % ENTRIES_PER_PAGE;
	index /= ENTRIES_PER_PAGE;
	dir = info->i_indirect + index;

	if (index >= ENTRIES_PER_PAGE/2) {
		index -= ENTRIES_PER_PAGE/2;
		dir = info->i_indirect + ENTRIES_PER_PAGE/2 
			+ index/ENTRIES_PER_PAGE;
		index %= ENTRIES_PER_PAGE;
		if (!*dir) {
			if (page) {
				*dir = (void *) *page;
				*page = 0;
			}
			return NULL;		/* need another page */
		}
		dir = ((void **)*dir) + index;
	}

	if (!*dir) {
		if (!page || !*page)
			return NULL;		/* need a page */
		*dir = (void *) *page;
		*page = 0;
	}
	return ((swp_entry_t *)*dir) + offset;
}

/*
 * shmem_swp_alloc - get the position of the swap entry for the page.
 *                   If it does not exist allocate the entry.
 *
 * @info:	info structure for the inode
 * @index:	index of the page to find
 */
static swp_entry_t *shmem_swp_alloc(struct shmem_inode_info *info, unsigned long index)
{
	struct inode *inode = &info->vfs_inode;
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	unsigned long page = 0;
	swp_entry_t *entry;

	while (!(entry = shmem_swp_entry(info, index, &page))) {
		if (index >= info->next_index) {
			entry = ERR_PTR(-EFAULT);
			break;
		}

		/*
		 * Test free_blocks against 1 not 0, since we have 1 data
		 * page (and perhaps indirect index pages) yet to allocate:
		 * a waste to allocate index if we cannot allocate data.
		 */
		spin_lock(&sbinfo->stat_lock);
		if (sbinfo->free_blocks <= 1) {
			spin_unlock(&sbinfo->stat_lock);
			return ERR_PTR(-ENOSPC);
		}
		sbinfo->free_blocks--;
		inode->i_blocks += BLOCKS_PER_PAGE;
		spin_unlock(&sbinfo->stat_lock);

		spin_unlock(&info->lock);
		page = get_zeroed_page(GFP_USER);
		spin_lock(&info->lock);

		if (!page) {
			shmem_free_block(inode);
			return ERR_PTR(-ENOMEM);
		}
	}
	if (page) {
		/* another task gave its page, or truncated the file */
		shmem_free_block(inode);
		free_page(page);
	}
	return entry;
}

/*
 * shmem_free_swp - free some swap entries in a directory
 *
 * @dir:   pointer to the directory
 * @count: number of entries to scan
 */
static int shmem_free_swp(swp_entry_t *dir, unsigned int count)
{
	swp_entry_t *ptr, entry;
	int freed = 0;

	for (ptr = dir; ptr < dir + count; ptr++) {
		if (!ptr->val)
			continue;
		entry = *ptr;
		*ptr = (swp_entry_t){0};
		freed++;
		free_swap_and_cache(entry);
	}
	return freed;
}

/*
 * shmem_truncate_direct - free the swap entries of a whole doubly
 *                         indirect block
 *
 * @info:	the info structure of the inode
 * @dir:	pointer to the pointer to the block
 * @start:	offset to start from (in pages)
 * @len:	how many pages are stored in this block
 *
 * Returns the number of freed swap entries.
 */
static inline unsigned long
shmem_truncate_direct(struct shmem_inode_info *info, swp_entry_t ***dir, unsigned long start, unsigned long len)
{
	swp_entry_t **last, **ptr;
	unsigned long off, freed_swp, freed = 0;

	last = *dir + (len + ENTRIES_PER_PAGE-1) / ENTRIES_PER_PAGE;
	off = start % ENTRIES_PER_PAGE;

	for (ptr = *dir + start/ENTRIES_PER_PAGE; ptr < last; ptr++, off = 0) {
		if (!*ptr)
			continue;

		if (info->swapped) {
			freed_swp = shmem_free_swp(*ptr + off,
						ENTRIES_PER_PAGE - off);
			info->swapped -= freed_swp;
			freed += freed_swp;
		}

		if (!off) {
			info->alloced++;
			free_page((unsigned long) *ptr);
			*ptr = 0;
		}
	}

	if (!start) {
		info->alloced++;
		free_page((unsigned long) *dir);
		*dir = 0;
	}
	return freed;
}

/*
 * shmem_truncate_indirect - truncate an inode
 *
 * @info:  the info structure of the inode
 * @index: the index to truncate
 *
 * This function locates the last doubly indirect block and calls
 * then shmem_truncate_direct to do the real work
 */
static inline unsigned long
shmem_truncate_indirect(struct shmem_inode_info *info, unsigned long index)
{
	swp_entry_t ***base;
	unsigned long baseidx, len, start;
	unsigned long max = info->next_index-1;
	unsigned long freed;

	if (max < SHMEM_NR_DIRECT) {
		info->next_index = index;
		if (!info->swapped)
			return 0;
		freed = shmem_free_swp(info->i_direct + index,
					SHMEM_NR_DIRECT - index);
		info->swapped -= freed;
		return freed;
	}

	if (max < ENTRIES_PER_PAGE * ENTRIES_PER_PAGE/2 + SHMEM_NR_DIRECT) {
		max -= SHMEM_NR_DIRECT;
		base = (swp_entry_t ***) &info->i_indirect;
		baseidx = SHMEM_NR_DIRECT;
		len = max+1;
	} else {
		max -= ENTRIES_PER_PAGE*ENTRIES_PER_PAGE/2+SHMEM_NR_DIRECT;
		if (max >= ENTRIES_PER_PAGE*ENTRIES_PER_PAGE*ENTRIES_PER_PAGE/2)
			BUG();

		baseidx = max & ~(ENTRIES_PER_PAGE*ENTRIES_PER_PAGE-1);
		base = (swp_entry_t ***) info->i_indirect + ENTRIES_PER_PAGE/2 + baseidx/ENTRIES_PER_PAGE/ENTRIES_PER_PAGE ;
		len = max - baseidx + 1;
		baseidx += ENTRIES_PER_PAGE*ENTRIES_PER_PAGE/2+SHMEM_NR_DIRECT;
	}

	if (index > baseidx) {
		info->next_index = index;
		start = index - baseidx;
	} else {
		info->next_index = baseidx;
		start = 0;
	}
	return *base? shmem_truncate_direct(info, base, start, len): 0;
}

static void shmem_truncate(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	unsigned long index;

	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	index = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	spin_lock(&info->lock);
	while (index < info->next_index)
		(void) shmem_truncate_indirect(info, index);
	shmem_recalc_inode(inode);
	spin_unlock(&info->lock);
}

static int shmem_notify_change(struct dentry *dentry, struct iattr *attr)
{
	static struct page *shmem_holdpage(struct inode *, unsigned long);
	struct inode *inode = dentry->d_inode;
	struct page *page = NULL;
	long change = 0;
	int error;

	if ((attr->ia_valid & ATTR_SIZE) && (attr->ia_size <= SHMEM_MAX_BYTES)) {
		/*
	 	 * Account swap file usage based on new file size,
		 * but just let vmtruncate fail on out-of-range sizes.
	 	 */
		change = VM_ACCT(attr->ia_size) - VM_ACCT(inode->i_size);
		if (change > 0) {
			if (!vm_enough_memory(change))
				return -ENOMEM;
		} else if (attr->ia_size < inode->i_size) {
			vm_unacct_memory(-change);
			/*
			 * If truncating down to a partial page, then
			 * if that page is already allocated, hold it
			 * in memory until the truncation is over, so
			 * truncate_partial_page cannnot miss it were
			 * it assigned to swap.
			 */
			if (attr->ia_size & (PAGE_CACHE_SIZE-1)) {
				page = shmem_holdpage(inode,
					attr->ia_size >> PAGE_CACHE_SHIFT);
			}
		}
	}

	error = inode_change_ok(inode, attr);
	if (!error)
		error = inode_setattr(inode, attr);
	if (page)
		page_cache_release(page);
	if (error)
		vm_unacct_memory(change);
	return error;
}


static void shmem_delete_inode(struct inode * inode)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	struct shmem_inode_info *info = SHMEM_I(inode);

	if (inode->i_op->truncate == shmem_truncate) {
		spin_lock(&shmem_ilock);
		list_del(&info->list);
		spin_unlock(&shmem_ilock);
		if (info->flags & VM_ACCOUNT)
			vm_unacct_memory(VM_ACCT(inode->i_size));
		inode->i_size = 0;
		shmem_truncate (inode);
	}
	BUG_ON(inode->i_blocks);
	spin_lock (&sbinfo->stat_lock);
	sbinfo->free_inodes++;
	spin_unlock (&sbinfo->stat_lock);
	clear_inode(inode);
}

static inline int shmem_find_swp(swp_entry_t entry, swp_entry_t *ptr, swp_entry_t *eptr)
{
	swp_entry_t *test;

	for (test = ptr; test < eptr; test++) {
		if (test->val == entry.val)
			return test - ptr;
	}
	return -1;
}

static int shmem_unuse_inode(struct shmem_inode_info *info, swp_entry_t entry, struct page *page)
{
	swp_entry_t *ptr;
	unsigned long idx;
	int offset;

	idx = 0;
	ptr = info->i_direct;
	spin_lock (&info->lock);
	offset = info->next_index;
	if (offset > SHMEM_NR_DIRECT)
		offset = SHMEM_NR_DIRECT;
	offset = shmem_find_swp(entry, ptr, ptr + offset);
	if (offset >= 0)
		goto found;

	for (idx = SHMEM_NR_DIRECT; idx < info->next_index; 
	     idx += ENTRIES_PER_PAGE) {
		ptr = shmem_swp_entry(info, idx, NULL);
		if (!ptr)
			continue;
		offset = info->next_index - idx;
		if (offset > ENTRIES_PER_PAGE)
			offset = ENTRIES_PER_PAGE;
		offset = shmem_find_swp(entry, ptr, ptr + offset);
		if (offset >= 0)
			goto found;
	}
	spin_unlock (&info->lock);
	return 0;
found:
	if (move_from_swap_cache(page, idx + offset,
			info->vfs_inode.i_mapping) == 0) {
		ptr[offset] = (swp_entry_t) {0};
		info->swapped--;
	}
	spin_unlock(&info->lock);
	/*
	 * Decrement swap count even when the entry is left behind:
	 * try_to_unuse will skip over mms, then reincrement count.
	 */
	swap_free(entry);
	return 1;
}

/*
 * shmem_unuse() search for an eventually swapped out shmem page.
 */
int shmem_unuse(swp_entry_t entry, struct page *page)
{
	struct list_head *p;
	struct shmem_inode_info * info;
	int found = 0;

	spin_lock (&shmem_ilock);
	list_for_each(p, &shmem_inodes) {
		info = list_entry(p, struct shmem_inode_info, list);

		if (info->swapped && shmem_unuse_inode(info, entry, page)) {
			/* move head to start search for next from here */
			list_move_tail(&shmem_inodes, &info->list);
			found = 1;
			break;
		}
	}
	spin_unlock (&shmem_ilock);
	return found;
}

/*
 * Move the page from the page cache to the swap cache.
 */
static int shmem_writepage(struct page * page)
{
	int err;
	struct shmem_inode_info *info;
	swp_entry_t *entry, swap;
	struct address_space *mapping;
	unsigned long index;
	struct inode *inode;

	if (!PageLocked(page))
		BUG();
	if (page_mapped(page))
		BUG();

	mapping = page->mapping;
	index = page->index;
	inode = mapping->host;
	info = SHMEM_I(inode);
	if (info->flags & VM_LOCKED)
		return fail_writepage(page);
	swap = get_swap_page();
	if (!swap.val)
		return fail_writepage(page);

	spin_lock(&info->lock);
	shmem_recalc_inode(inode);
	entry = shmem_swp_entry(info, index, NULL);
	if (!entry)
		BUG();
	if (entry->val)
		BUG();

	err = move_to_swap_cache(page, swap);
	if (!err) {
		*entry = swap;
		info->swapped++;
		spin_unlock(&info->lock);
		unlock_page(page);
		return 0;
	}

	spin_unlock(&info->lock);
	swap_free(swap);
	return fail_writepage(page);
}

static int shmem_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	return 0;
}

static int shmem_vm_writeback(struct page *page, struct writeback_control *wbc)
{
	clear_page_dirty(page);
	if (shmem_writepage(page) < 0)
		set_page_dirty(page);
	return 0;
}

/*
 * shmem_getpage - either get the page from swap or allocate a new one
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache
 */
static int shmem_getpage(struct inode *inode, unsigned long idx, struct page **pagep)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo;
	struct page *page;
	swp_entry_t *entry;
	swp_entry_t swap;
	int error = 0;

	if (idx >= SHMEM_MAX_INDEX)
		return -EFBIG;

	/*
	 * When writing, i_sem is held against truncation and other
	 * writing, so next_index will remain as set here; but when
	 * reading, idx must always be checked against next_index
	 * after sleeping, lest truncation occurred meanwhile.
	 */
	spin_lock(&info->lock);
	if (info->next_index <= idx)
		info->next_index = idx + 1;
	spin_unlock(&info->lock);

repeat:
	page = find_lock_page(mapping, idx);
	if (page) {
		*pagep = page;
		return 0;
	}

	spin_lock(&info->lock);
	shmem_recalc_inode(inode);
	entry = shmem_swp_alloc(info, idx);
	if (IS_ERR(entry)) {
		spin_unlock(&info->lock);
		return PTR_ERR(entry);
	}
	swap = *entry;

	if (swap.val) {
		/* Look it up and read it in.. */
		page = lookup_swap_cache(swap);
		if (!page) {
			spin_unlock(&info->lock);
			swapin_readahead(swap);
			page = read_swap_cache_async(swap);
			if (!page) {
				spin_lock(&info->lock);
				entry = shmem_swp_alloc(info, idx);
				if (IS_ERR(entry))
					error = PTR_ERR(entry);
				else if (entry->val == swap.val)
					error = -ENOMEM;
				spin_unlock(&info->lock);
				if (error)
					return error;
				goto repeat;
			}
			wait_on_page_locked(page);
			page_cache_release(page);
			goto repeat;
		}

		/* We have to do this with page locked to prevent races */
		if (TestSetPageLocked(page)) {
			spin_unlock(&info->lock);
			wait_on_page_locked(page);
			page_cache_release(page);
			goto repeat;
		}
		if (PageWriteback(page)) {
			spin_unlock(&info->lock);
			wait_on_page_writeback(page);
			unlock_page(page);
			page_cache_release(page);
			goto repeat;
		}

		error = PageUptodate(page)?
			move_from_swap_cache(page, idx, mapping): -EIO;
		if (error) {
			spin_unlock(&info->lock);
			unlock_page(page);
			page_cache_release(page);
			return error;
		}

		*entry = (swp_entry_t) {0};
		info->swapped--;
		spin_unlock (&info->lock);
		swap_free(swap);
	} else {
		spin_unlock(&info->lock);
		sbinfo = SHMEM_SB(inode->i_sb);
		spin_lock(&sbinfo->stat_lock);
		if (sbinfo->free_blocks == 0) {
			spin_unlock(&sbinfo->stat_lock);
			return -ENOSPC;
		}
		sbinfo->free_blocks--;
		inode->i_blocks += BLOCKS_PER_PAGE;
		spin_unlock(&sbinfo->stat_lock);

		page = page_cache_alloc(mapping);
		if (!page) {
			shmem_free_block(inode);
			return -ENOMEM;
		}

		spin_lock(&info->lock);
		entry = shmem_swp_alloc(info, idx);
		if (IS_ERR(entry))
			error = PTR_ERR(entry);
		if (error || entry->val ||
		    add_to_page_cache_lru(page, mapping, idx) < 0) {
			spin_unlock(&info->lock);
			page_cache_release(page);
			shmem_free_block(inode);
			if (error)
				return error;
			goto repeat;
		}
		info->alloced++;
		spin_unlock(&info->lock);
		clear_highpage(page);
	}

	/* We have the page */
	SetPageUptodate(page);
	*pagep = page;
	return 0;
}

static struct page *shmem_holdpage(struct inode *inode, unsigned long idx)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct page *page;
	swp_entry_t *entry;
	swp_entry_t swap = {0};

	/*
	 * Somehow, it feels wrong for truncation down to cause any
	 * allocation: so instead of a blind shmem_getpage, check that
	 * the page has actually been instantiated before holding it.
	 */
	spin_lock(&info->lock);
	page = find_get_page(inode->i_mapping, idx);
	if (!page) {
		entry = shmem_swp_entry(info, idx, NULL);
		if (entry)
			swap = *entry;
	}
	spin_unlock(&info->lock);
	if (swap.val) {
		if (shmem_getpage(inode, idx, &page) == 0)
			unlock_page(page);
	}
	return page;
}

struct page *shmem_nopage(struct vm_area_struct *vma, unsigned long address, int unused)
{
	struct inode *inode = vma->vm_file->f_dentry->d_inode;
	struct page *page;
	unsigned long idx;
	int error;

	idx = (address - vma->vm_start) >> PAGE_CACHE_SHIFT;
	idx += vma->vm_pgoff;

	if (((loff_t) idx << PAGE_CACHE_SHIFT) >= inode->i_size)
		return NOPAGE_SIGBUS;

	error = shmem_getpage(inode, idx, &page);
	if (error)
		return (error == -ENOMEM)? NOPAGE_OOM: NOPAGE_SIGBUS;

	unlock_page(page);
	flush_page_to_ram(page);
	return page;
}

void shmem_lock(struct file * file, int lock)
{
	struct inode * inode = file->f_dentry->d_inode;
	struct shmem_inode_info * info = SHMEM_I(inode);

	spin_lock(&info->lock);
	if (lock)
		info->flags |= VM_LOCKED;
	else
		info->flags &= ~VM_LOCKED;
	spin_unlock(&info->lock);
}

static int shmem_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct vm_operations_struct * ops;
	struct inode *inode = file->f_dentry->d_inode;

	ops = &shmem_vm_ops;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	UPDATE_ATIME(inode);
	vma->vm_ops = ops;
	return 0;
}

struct inode *shmem_get_inode(struct super_block *sb, int mode, int dev)
{
	struct inode * inode;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	spin_lock (&sbinfo->stat_lock);
	if (!sbinfo->free_inodes) {
		spin_unlock (&sbinfo->stat_lock);
		return NULL;
	}
	sbinfo->free_inodes--;
	spin_unlock (&sbinfo->stat_lock);

	inode = new_inode(sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_mapping->a_ops = &shmem_aops;
		inode->i_mapping->backing_dev_info = &shmem_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		info = SHMEM_I(inode);
		memset(info, 0, (char *)inode - (char *)info);
		spin_lock_init(&info->lock);
		info->flags = VM_ACCOUNT;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &shmem_inode_operations;
			inode->i_fop = &shmem_file_operations;
			spin_lock (&shmem_ilock);
			list_add_tail(&info->list, &shmem_inodes);
			spin_unlock (&shmem_ilock);
			break;
		case S_IFDIR:
			inode->i_nlink++;
			/* Some things misbehave if size == 0 on a directory */
			inode->i_size = 2 * BOGO_DIRENT_SIZE;
			inode->i_op = &shmem_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			break;
		case S_IFLNK:
			break;
		}
	}
	return inode;
}

static int shmem_set_size(struct shmem_sb_info *info,
			  unsigned long max_blocks, unsigned long max_inodes)
{
	int error;
	unsigned long blocks, inodes;

	spin_lock(&info->stat_lock);
	blocks = info->max_blocks - info->free_blocks;
	inodes = info->max_inodes - info->free_inodes;
	error = -EINVAL;
	if (max_blocks < blocks)
		goto out;
	if (max_inodes < inodes)
		goto out;
	error = 0;
	info->max_blocks  = max_blocks;
	info->free_blocks = max_blocks - blocks;
	info->max_inodes  = max_inodes;
	info->free_inodes = max_inodes - inodes;
out:
	spin_unlock(&info->stat_lock);
	return error;
}

#ifdef CONFIG_TMPFS

static struct inode_operations shmem_symlink_inode_operations;
static struct inode_operations shmem_symlink_inline_operations;

static ssize_t
shmem_file_write(struct file *file,const char *buf,size_t count,loff_t *ppos)
{
	struct inode	*inode = file->f_dentry->d_inode; 
	unsigned long	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	loff_t		pos;
	struct page	*page;
	unsigned long	written;
	long		status;
	int		err;
	loff_t		maxpos;

	if ((ssize_t) count < 0)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	down(&inode->i_sem);

	pos = *ppos;
	err = -EINVAL;
	if (pos < 0)
		goto out_nc;

	err = file->f_error;
	if (err) {
		file->f_error = 0;
		goto out_nc;
	}

	written = 0;

	if (file->f_flags & O_APPEND)
		pos = inode->i_size;

	maxpos = inode->i_size;
	if (pos + count > inode->i_size) {
		maxpos = pos + count;
		if (maxpos > SHMEM_MAX_BYTES)
			maxpos = SHMEM_MAX_BYTES;
		if (!vm_enough_memory(VM_ACCT(maxpos) - VM_ACCT(inode->i_size))) {
			err = -ENOMEM;
			goto out_nc;
		}
	}

	/*
	 * Check whether we've reached the file size limit.
	 */
	err = -EFBIG;
	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (count > limit - pos) {
			send_sig(SIGXFSZ, current, 0);
			count = limit - pos;
		}
	}

	status	= 0;
	if (count) {
		remove_suid(file->f_dentry);
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	}

	while (count) {
		unsigned long bytes, index, offset;
		char *kaddr;

		/*
		 * Try to find the page in the cache. If it isn't there,
		 * allocate a free page.
		 */
		offset = (pos & (PAGE_CACHE_SIZE -1)); /* Within page */
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > count) {
			bytes = count;
		}

		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 */
		{ volatile unsigned char dummy;
			__get_user(dummy, buf);
			__get_user(dummy, buf+bytes-1);
		}

		status = shmem_getpage(inode, index, &page);
		if (status)
			break;

		/* We have exclusive IO access to the page.. */
		if (!PageLocked(page)) {
			PAGE_BUG(page);
		}

		kaddr = kmap(page);
		status = copy_from_user(kaddr+offset, buf, bytes);
		kunmap(page);
		if (status)
			goto fail_write;

		flush_dcache_page(page);
		if (bytes > 0) {
			set_page_dirty(page);
			written += bytes;
			count -= bytes;
			pos += bytes;
			buf += bytes;
			if (pos > inode->i_size) 
				inode->i_size = pos;
		}
unlock:
		/* Mark it unlocked again and drop the page.. */
		unlock_page(page);
		page_cache_release(page);

		if (status < 0)
			break;
	}
	*ppos = pos;

	err = written ? written : status;
out:
	/* Short writes give back address space */
	if (inode->i_size != maxpos)
		vm_unacct_memory(VM_ACCT(maxpos) - VM_ACCT(inode->i_size));
out_nc:
	up(&inode->i_sem);
	return err;
fail_write:
	status = -EFAULT;
	ClearPageUptodate(page);
	goto unlock;
}

static void do_shmem_file_read(struct file * filp, loff_t *ppos, read_descriptor_t * desc)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	unsigned long index, offset;
	int nr = 1;

	index = *ppos >> PAGE_CACHE_SHIFT;
	offset = *ppos & ~PAGE_CACHE_MASK;

	while (nr && desc->count) {
		struct page *page;
		unsigned long end_index, nr;

		end_index = inode->i_size >> PAGE_CACHE_SHIFT;
		if (index > end_index)
			break;
		if (index == end_index) {
			nr = inode->i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset)
				break;
		}

		desc->error = shmem_getpage(inode, index, &page);
		if (desc->error) {
			if (desc->error == -EFAULT)
				desc->error = 0;
			break;
		}

		/*
		 * We must evaluate after, since reads (unlike writes)
		 * are called without i_sem protection against truncate
		 */
		nr = PAGE_CACHE_SIZE;
		end_index = inode->i_size >> PAGE_CACHE_SHIFT;
		if (index == end_index) {
			nr = inode->i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset) {
				unlock_page(page);
				page_cache_release(page);
				break;
			}
		}
		unlock_page(page);
		nr -= offset;

		if (!list_empty(&mapping->i_mmap_shared))
			flush_dcache_page(page);

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 *
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		nr = file_read_actor(desc, page, offset, nr);
		offset += nr;
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;
	
		page_cache_release(page);
	}

	*ppos = ((loff_t) index << PAGE_CACHE_SHIFT) + offset;
	UPDATE_ATIME(inode);
}

static ssize_t shmem_file_read(struct file * filp, char * buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = -EFAULT;
	if (access_ok(VERIFY_WRITE, buf, count)) {
		retval = 0;

		if (count) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.count = count;
			desc.buf = buf;
			desc.error = 0;
			do_shmem_file_read(filp, ppos, &desc);

			retval = desc.written;
			if (!retval)
				retval = desc.error;
		}
	}
	return retval;
}

static int shmem_statfs(struct super_block *sb, struct statfs *buf)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	buf->f_type = TMPFS_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	spin_lock (&sbinfo->stat_lock);
	buf->f_blocks = sbinfo->max_blocks;
	buf->f_bavail = buf->f_bfree = sbinfo->free_blocks;
	buf->f_files = sbinfo->max_inodes;
	buf->f_ffree = sbinfo->free_inodes;
	spin_unlock (&sbinfo->stat_lock);
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int shmem_mknod(struct inode *dir, struct dentry *dentry, int mode, int dev)
{
	struct inode * inode = shmem_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		dir->i_size += BOGO_DIRENT_SIZE;
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int shmem_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	int error;

	if ((error = shmem_mknod(dir, dentry, mode | S_IFDIR, 0)))
		return error;
	dir->i_nlink++;
	return 0;
}

static int shmem_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return shmem_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Link a file..
 */
static int shmem_link(struct dentry *old_dentry, struct inode * dir, struct dentry * dentry)
{
	struct inode *inode = old_dentry->d_inode;

	dir->i_size += BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inode->i_nlink++;
	atomic_inc(&inode->i_count);	/* New dentry reference */
	dget(dentry);		/* Extra pinning count for the created dentry */
	d_instantiate(dentry, inode);
	return 0;
}

static inline int shmem_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

/*
 * Check that a directory is empty (this works
 * for regular files too, they'll just always be
 * considered empty..).
 *
 * Note that an empty directory can still have
 * children, they just all have to be negative..
 */
static int shmem_empty(struct dentry *dentry)
{
	struct list_head *list;

	spin_lock(&dcache_lock);
	list = dentry->d_subdirs.next;

	while (list != &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);

		if (shmem_positive(de)) {
			spin_unlock(&dcache_lock);
			return 0;
		}
		list = list->next;
	}
	spin_unlock(&dcache_lock);
	return 1;
}

static int shmem_unlink(struct inode * dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	dir->i_size -= BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inode->i_nlink--;
	dput(dentry);	/* Undo the count from "create" - this does all the work */
	return 0;
}

static int shmem_rmdir(struct inode * dir, struct dentry *dentry)
{
	if (!shmem_empty(dentry))
		return -ENOTEMPTY;

	dir->i_nlink--;
	return shmem_unlink(dir, dentry);
}

/*
 * The VFS layer already does all the dentry stuff for rename,
 * we just have to decrement the usage count for the target if
 * it exists so that the VFS layer correctly free's it when it
 * gets overwritten.
 */
static int shmem_rename(struct inode * old_dir, struct dentry *old_dentry, struct inode * new_dir,struct dentry *new_dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int they_are_dirs = S_ISDIR(inode->i_mode);

	if (!shmem_empty(new_dentry)) 
		return -ENOTEMPTY;

	if (new_dentry->d_inode) {
		(void) shmem_unlink(new_dir, new_dentry);
		if (they_are_dirs)
			old_dir->i_nlink--;
	} else if (they_are_dirs) {
		old_dir->i_nlink--;
		new_dir->i_nlink++;
	}

	old_dir->i_size -= BOGO_DIRENT_SIZE;
	new_dir->i_size += BOGO_DIRENT_SIZE;
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	inode->i_ctime = CURRENT_TIME;
	return 0;
}

static int shmem_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	int error;
	int len;
	struct inode *inode;
	struct page *page;
	char *kaddr;
	struct shmem_inode_info * info;

	len = strlen(symname) + 1;
	if (len > PAGE_CACHE_SIZE)
		return -ENAMETOOLONG;

	inode = shmem_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (!inode)
		return -ENOSPC;

	info = SHMEM_I(inode);
	inode->i_size = len-1;
	if (len <= (char *)inode - (char *)info) {
		/* do it inline */
		memcpy(info, symname, len);
		inode->i_op = &shmem_symlink_inline_operations;
	} else {
		if (!vm_enough_memory(VM_ACCT(1))) {
			iput(inode);
			return -ENOMEM;
		}
		error = shmem_getpage(inode, 0, &page);
		if (error) {
			vm_unacct_memory(VM_ACCT(1));
			iput(inode);
			return error;
		}
		inode->i_op = &shmem_symlink_inode_operations;
		spin_lock (&shmem_ilock);
		list_add_tail(&info->list, &shmem_inodes);
		spin_unlock (&shmem_ilock);
		kaddr = kmap(page);
		memcpy(kaddr, symname, len);
		kunmap(page);
		set_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
	}
	dir->i_size += BOGO_DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
}

static int shmem_readlink_inline(struct dentry *dentry, char *buffer, int buflen)
{
	return vfs_readlink(dentry,buffer,buflen, (const char *)SHMEM_I(dentry->d_inode));
}

static int shmem_follow_link_inline(struct dentry *dentry, struct nameidata *nd)
{
	return vfs_follow_link(nd, (const char *)SHMEM_I(dentry->d_inode));
}

static int shmem_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct page *page;
	int res = shmem_getpage(dentry->d_inode, 0, &page);
	if (res)
		return res;
	res = vfs_readlink(dentry, buffer, buflen, kmap(page));
	kunmap(page);
	unlock_page(page);
	page_cache_release(page);
	return res;
}

static int shmem_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct page *page;
	int res = shmem_getpage(dentry->d_inode, 0, &page);
	if (res)
		return res;
	res = vfs_follow_link(nd, kmap(page));
	kunmap(page);
	unlock_page(page);
	page_cache_release(page);
	return res;
}

static struct inode_operations shmem_symlink_inline_operations = {
	.readlink	= shmem_readlink_inline,
	.follow_link	= shmem_follow_link_inline,
};

static struct inode_operations shmem_symlink_inode_operations = {
	.truncate	= shmem_truncate,
	.readlink	= shmem_readlink,
	.follow_link	= shmem_follow_link,
};

static int shmem_parse_options(char *options, int *mode, uid_t *uid, gid_t *gid, unsigned long * blocks, unsigned long *inodes)
{
	char *this_char, *value, *rest;

	while ((this_char = strsep(&options, ",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr(this_char,'=')) != NULL) {
			*value++ = 0;
		} else {
			printk(KERN_ERR 
			    "tmpfs: No value for mount option '%s'\n", 
			    this_char);
			return 1;
		}

		if (!strcmp(this_char,"size")) {
			unsigned long long size;
			size = memparse(value,&rest);
			if (*rest)
				goto bad_val;
			*blocks = size >> PAGE_CACHE_SHIFT;
		} else if (!strcmp(this_char,"nr_blocks")) {
			*blocks = memparse(value,&rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"nr_inodes")) {
			*inodes = memparse(value,&rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"mode")) {
			if (!mode)
				continue;
			*mode = simple_strtoul(value,&rest,8);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"uid")) {
			if (!uid)
				continue;
			*uid = simple_strtoul(value,&rest,0);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"gid")) {
			if (!gid)
				continue;
			*gid = simple_strtoul(value,&rest,0);
			if (*rest)
				goto bad_val;
		} else {
			printk(KERN_ERR "tmpfs: Bad mount option %s\n",
			       this_char);
			return 1;
		}
	}
	return 0;

bad_val:
	printk(KERN_ERR "tmpfs: Bad value '%s' for mount option '%s'\n", 
	       value, this_char);
	return 1;

}

static int shmem_remount_fs (struct super_block *sb, int *flags, char *data)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	unsigned long max_blocks = sbinfo->max_blocks;
	unsigned long max_inodes = sbinfo->max_inodes;

	if (shmem_parse_options (data, NULL, NULL, NULL, &max_blocks, &max_inodes))
		return -EINVAL;
	return shmem_set_size(sbinfo, max_blocks, max_inodes);
}

int shmem_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	return 0;
}
#endif

static int shmem_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	unsigned long blocks, inodes;
	int mode   = S_IRWXUGO | S_ISVTX;
	uid_t uid = current->fsuid;
	gid_t gid = current->fsgid;
	struct shmem_sb_info *sbinfo;
	struct sysinfo si;
	int err;

	sbinfo = kmalloc(sizeof(struct shmem_sb_info), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;
	sb->u.generic_sbp = sbinfo;
	memset(sbinfo, 0, sizeof(struct shmem_sb_info));

	/*
	 * Per default we only allow half of the physical ram per
	 * tmpfs instance
	 */
	si_meminfo(&si);
	blocks = inodes = si.totalram / 2;

#ifdef CONFIG_TMPFS
	if (shmem_parse_options (data, &mode, &uid, &gid, &blocks, &inodes)) {
		err = -EINVAL;
		goto failed;
	}
#else
	sb->s_flags |= MS_NOUSER;
#endif

	spin_lock_init (&sbinfo->stat_lock);
	sbinfo->max_blocks = blocks;
	sbinfo->free_blocks = blocks;
	sbinfo->max_inodes = inodes;
	sbinfo->free_inodes = inodes;
	sb->s_maxbytes = SHMEM_MAX_BYTES;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = TMPFS_MAGIC;
	sb->s_op = &shmem_ops;
	inode = shmem_get_inode(sb, S_IFDIR | mode, 0);
	if (!inode) {
		err = -ENOMEM;
		goto failed;
	}

	inode->i_uid = uid;
	inode->i_gid = gid;
	root = d_alloc_root(inode);
	if (!root) {
		err = -ENOMEM;
		goto failed_iput;
	}
	sb->s_root = root;
	return 0;

failed_iput:
	iput(inode);
failed:
	kfree(sbinfo);
	sb->u.generic_sbp = NULL;
	return err;
}

static void shmem_put_super(struct super_block *sb)
{
	kfree(sb->u.generic_sbp);
	sb->u.generic_sbp = NULL;
}

static kmem_cache_t * shmem_inode_cachep;

static struct inode *shmem_alloc_inode(struct super_block *sb)
{
	struct shmem_inode_info *p;
	p = (struct shmem_inode_info *)kmem_cache_alloc(shmem_inode_cachep, SLAB_KERNEL);
	if (!p)
		return NULL;
	return &p->vfs_inode;
}

static void shmem_destroy_inode(struct inode *inode)
{
	kmem_cache_free(shmem_inode_cachep, SHMEM_I(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct shmem_inode_info *p = (struct shmem_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&p->vfs_inode);
	}
}
 
static int init_inodecache(void)
{
	shmem_inode_cachep = kmem_cache_create("shmem_inode_cache",
					     sizeof(struct shmem_inode_info),
					     0, SLAB_HWCACHE_ALIGN,
					     init_once, NULL);
	if (shmem_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	if (kmem_cache_destroy(shmem_inode_cachep))
		printk(KERN_INFO "shmem_inode_cache: not all structures were freed\n");
}

static struct address_space_operations shmem_aops = {
	.writepage	= shmem_writepage,
	.writepages	= shmem_writepages,
	.vm_writeback	= shmem_vm_writeback,
	.set_page_dirty	= __set_page_dirty_nobuffers,
};

static struct file_operations shmem_file_operations = {
	.mmap		= shmem_mmap,
#ifdef CONFIG_TMPFS
	.read		= shmem_file_read,
	.write		= shmem_file_write,
	.fsync		= shmem_sync_file,
#endif
};

static struct inode_operations shmem_inode_operations = {
	.truncate	= shmem_truncate,
	.setattr	= shmem_notify_change,
};

static struct inode_operations shmem_dir_inode_operations = {
#ifdef CONFIG_TMPFS
	.create		= shmem_create,
	.lookup		= simple_lookup,
	.link		= shmem_link,
	.unlink		= shmem_unlink,
	.symlink	= shmem_symlink,
	.mkdir		= shmem_mkdir,
	.rmdir		= shmem_rmdir,
	.mknod		= shmem_mknod,
	.rename		= shmem_rename,
#endif
};

static struct super_operations shmem_ops = {
	.alloc_inode	= shmem_alloc_inode,
	.destroy_inode	= shmem_destroy_inode,
#ifdef CONFIG_TMPFS
	.statfs		= shmem_statfs,
	.remount_fs	= shmem_remount_fs,
#endif
	.delete_inode	= shmem_delete_inode,
	.drop_inode	= generic_delete_inode,
	.put_super	= shmem_put_super,
};

static struct vm_operations_struct shmem_vm_ops = {
	.nopage		= shmem_nopage,
};

static struct super_block *shmem_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_nodev(fs_type, flags, data, shmem_fill_super);
}

#ifdef CONFIG_TMPFS
/* type "shm" will be tagged obsolete in 2.5 */
static struct file_system_type shmem_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "shmem",
	.get_sb		= shmem_get_sb,
	.kill_sb	= kill_litter_super,
};
#endif
static struct file_system_type tmpfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "tmpfs",
	.get_sb		= shmem_get_sb,
	.kill_sb	= kill_litter_super,
};
static struct vfsmount *shm_mnt;

static int __init init_shmem_fs(void)
{
	int error;
	struct vfsmount * res;

	error = init_inodecache();
	if (error)
		goto out3;

	error = register_filesystem(&tmpfs_fs_type);
	if (error) {
		printk (KERN_ERR "Could not register tmpfs\n");
		goto out2;
	}
#ifdef CONFIG_TMPFS
	error = register_filesystem(&shmem_fs_type);
	if (error) {
		printk (KERN_ERR "Could not register shm fs\n");
		goto out1;
	}
	devfs_mk_dir (NULL, "shm", NULL);
#endif
	res = kern_mount(&tmpfs_fs_type);
	if (IS_ERR (res)) {
		error = PTR_ERR(res);
		printk (KERN_ERR "could not kern_mount tmpfs\n");
		goto out;
	}
	shm_mnt = res;

	/* The internal instance should not do size checking */
	shmem_set_size(SHMEM_SB(res->mnt_sb), ULONG_MAX, ULONG_MAX);
	return 0;

out:
#ifdef CONFIG_TMPFS
	unregister_filesystem(&shmem_fs_type);
out1:
#endif
	unregister_filesystem(&tmpfs_fs_type);
out2:
	destroy_inodecache();
out3:
	return error;
}

static void __exit exit_shmem_fs(void)
{
#ifdef CONFIG_TMPFS
	unregister_filesystem(&shmem_fs_type);
#endif
	unregister_filesystem(&tmpfs_fs_type);
	mntput(shm_mnt);
	destroy_inodecache();
}

module_init(init_shmem_fs)
module_exit(exit_shmem_fs)

/*
 * shmem_file_setup - get an unlinked file living in shmem fs
 *
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 *
 */
struct file *shmem_file_setup(char * name, loff_t size, unsigned long flags)
{
	int error;
	struct file *file;
	struct inode * inode;
	struct dentry *dentry, *root;
	struct qstr this;

	if (size > SHMEM_MAX_BYTES)
		return ERR_PTR(-EINVAL);

	if ((flags & VM_ACCOUNT) && !vm_enough_memory(VM_ACCT(size)))
		return ERR_PTR(-ENOMEM);

	error = -ENOMEM;
	this.name = name;
	this.len = strlen(name);
	this.hash = 0; /* will go */
	root = shm_mnt->mnt_root;
	dentry = d_alloc(root, &this);
	if (!dentry)
		goto put_memory;

	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto put_dentry;

	error = -ENOSPC;
	inode = shmem_get_inode(root->d_sb, S_IFREG | S_IRWXUGO, 0);
	if (!inode) 
		goto close_file;

	SHMEM_I(inode)->flags &= flags;
	d_instantiate(dentry, inode);
	inode->i_size = size;
	inode->i_nlink = 0;	/* It is unlinked */
	file->f_vfsmnt = mntget(shm_mnt);
	file->f_dentry = dentry;
	file->f_op = &shmem_file_operations;
	file->f_mode = FMODE_WRITE | FMODE_READ;
	return(file);

close_file:
	put_filp(file);
put_dentry:
	dput (dentry);
put_memory:
	if (flags & VM_ACCOUNT)
		vm_unacct_memory(VM_ACCT(size));
	return ERR_PTR(error);	
}

/*
 * shmem_zero_setup - setup a shared anonymous mapping
 *
 * @vma: the vma to be mmapped is prepared by do_mmap_pgoff
 */
int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;
	
	file = shmem_file_setup("dev/zero", size, vma->vm_flags);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput (vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &shmem_vm_ops;
	return 0;
}

EXPORT_SYMBOL(shmem_file_setup);
