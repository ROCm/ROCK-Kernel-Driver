/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/mempool.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_metapage.h"
#include "jfs_txnmgr.h"
#include "jfs_debug.h"

static spinlock_t meta_lock = SPIN_LOCK_UNLOCKED;

#ifdef CONFIG_JFS_STATISTICS
struct {
	uint	pagealloc;	/* # of page allocations */
	uint	pagefree;	/* # of page frees */
	uint	lockwait;	/* # of sleeping lock_metapage() calls */
} mpStat;
#endif


#define HASH_BITS 10		/* This makes hash_table 1 4K page */
#define HASH_SIZE (1 << HASH_BITS)
static metapage_t **hash_table = NULL;
static unsigned long hash_order;


static inline int metapage_locked(struct metapage *mp)
{
	return test_bit(META_locked, &mp->flag);
}

static inline int trylock_metapage(struct metapage *mp)
{
	return test_and_set_bit(META_locked, &mp->flag);
}

static inline void unlock_metapage(struct metapage *mp)
{
	clear_bit(META_locked, &mp->flag);
	wake_up(&mp->wait);
}

static void __lock_metapage(struct metapage *mp)
{
	DECLARE_WAITQUEUE(wait, current);

	INCREMENT(mpStat.lockwait);

	add_wait_queue_exclusive(&mp->wait, &wait);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (metapage_locked(mp)) {
			spin_unlock(&meta_lock);
			schedule();
			spin_lock(&meta_lock);
		}
	} while (trylock_metapage(mp));
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&mp->wait, &wait);
}

/* needs meta_lock */
static inline void lock_metapage(struct metapage *mp)
{
	if (trylock_metapage(mp))
		__lock_metapage(mp);
}

#define METAPOOL_MIN_PAGES 32
static kmem_cache_t *metapage_cache;
static mempool_t *metapage_mempool;

static void init_once(void *foo, kmem_cache_t *cachep, unsigned long flags)
{
	metapage_t *mp = (metapage_t *)foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		mp->lid = 0;
		mp->lsn = 0;
		mp->flag = 0;
		mp->data = NULL;
		mp->clsn = 0;
		mp->log = NULL;
		set_bit(META_free, &mp->flag);
		init_waitqueue_head(&mp->wait);
	}
}

static inline metapage_t *alloc_metapage(int no_wait)
{
	return mempool_alloc(metapage_mempool, no_wait ? GFP_ATOMIC : GFP_NOFS);
}

static inline void free_metapage(metapage_t *mp)
{
	mp->flag = 0;
	set_bit(META_free, &mp->flag);

	mempool_free(mp, metapage_mempool);
}

static void *mp_mempool_alloc(int gfp_mask, void *pool_data)
{
	return kmem_cache_alloc(metapage_cache, gfp_mask);
}
static void mp_mempool_free(void *element, void *pool_data)
{
	return kmem_cache_free(metapage_cache, element);
}

int __init metapage_init(void)
{
	/*
	 * Allocate the metapage structures
	 */
	metapage_cache = kmem_cache_create("jfs_mp", sizeof(metapage_t), 0, 0,
					   init_once, NULL);
	if (metapage_cache == NULL)
		return -ENOMEM;

	metapage_mempool = mempool_create(METAPOOL_MIN_PAGES, mp_mempool_alloc,
					  mp_mempool_free, NULL);

	if (metapage_mempool == NULL) {
		kmem_cache_destroy(metapage_cache);
		return -ENOMEM;
	}
	/*
	 * Now the hash list
	 */
	for (hash_order = 0;
	     ((PAGE_SIZE << hash_order) / sizeof(void *)) < HASH_SIZE;
	     hash_order++);
	hash_table =
	    (metapage_t **) __get_free_pages(GFP_KERNEL, hash_order);
	assert(hash_table);
	memset(hash_table, 0, PAGE_SIZE << hash_order);

	return 0;
}

void metapage_exit(void)
{
	mempool_destroy(metapage_mempool);
	kmem_cache_destroy(metapage_cache);
}

/*
 * Basically same hash as in pagemap.h, but using our hash table
 */
static metapage_t **meta_hash(struct address_space *mapping,
			      unsigned long index)
{
#define i (((unsigned long)mapping)/ \
	   (sizeof(struct inode) & ~(sizeof(struct inode) -1 )))
#define s(x) ((x) + ((x) >> HASH_BITS))
	return hash_table + (s(i + index) & (HASH_SIZE - 1));
#undef i
#undef s
}

static metapage_t *search_hash(metapage_t ** hash_ptr,
			       struct address_space *mapping,
			       unsigned long index)
{
	metapage_t *ptr;

	for (ptr = *hash_ptr; ptr; ptr = ptr->hash_next) {
		if ((ptr->mapping == mapping) && (ptr->index == index))
			return ptr;
	}

	return NULL;
}

static void add_to_hash(metapage_t * mp, metapage_t ** hash_ptr)
{
	if (*hash_ptr)
		(*hash_ptr)->hash_prev = mp;

	mp->hash_prev = NULL;
	mp->hash_next = *hash_ptr;
	*hash_ptr = mp;
}

static void remove_from_hash(metapage_t * mp, metapage_t ** hash_ptr)
{
	if (mp->hash_prev)
		mp->hash_prev->hash_next = mp->hash_next;
	else {
		assert(*hash_ptr == mp);
		*hash_ptr = mp->hash_next;
	}

	if (mp->hash_next)
		mp->hash_next->hash_prev = mp->hash_prev;
}

/*
 * Direct address space operations
 */

static int direct_get_block(struct inode *ip, sector_t lblock,
			    struct buffer_head *bh_result, int create)
{
	if (create)
		set_buffer_new(bh_result);

	map_bh(bh_result, ip->i_sb, lblock);

	return 0;
}

static int direct_writepage(struct page *page)
{
	return block_write_full_page(page, direct_get_block);
}

static int direct_readpage(struct file *fp, struct page *page)
{
	return block_read_full_page(page, direct_get_block);
}

static int direct_prepare_write(struct file *file, struct page *page,
				unsigned from, unsigned to)
{
	return block_prepare_write(page, from, to, direct_get_block);
}

static int direct_bmap(struct address_space *mapping, long block)
{
	return generic_block_bmap(mapping, block, direct_get_block);
}

struct address_space_operations direct_aops = {
	.readpage	= direct_readpage,
	.writepage	= direct_writepage,
	.sync_page	= block_sync_page,
	.prepare_write	= direct_prepare_write,
	.commit_write	= generic_commit_write,
	.bmap		= direct_bmap,
};

metapage_t *__get_metapage(struct inode *inode,
			   unsigned long lblock, unsigned int size,
			   int absolute, unsigned long new)
{
	metapage_t **hash_ptr;
	int l2BlocksPerPage;
	int l2bsize;
	struct address_space *mapping;
	metapage_t *mp;
	unsigned long page_index;
	unsigned long page_offset;

	jFYI(1, ("__get_metapage: inode = 0x%p, lblock = 0x%lx\n",
		 inode, lblock));

	if (absolute)
		mapping = JFS_SBI(inode->i_sb)->direct_mapping;
	else
		mapping = inode->i_mapping;

	spin_lock(&meta_lock);

	hash_ptr = meta_hash(mapping, lblock);

	mp = search_hash(hash_ptr, mapping, lblock);
	if (mp) {
	      page_found:
		if (test_bit(META_discard, &mp->flag)) {
			assert(new);	/* It's okay to reuse a discarded
					 * if we expect it to be empty
					 */
			clear_bit(META_discard, &mp->flag);
		}
		mp->count++;
		jFYI(1, ("__get_metapage: found 0x%p, in hash\n", mp));
		assert(mp->logical_size == size);
		lock_metapage(mp);
		spin_unlock(&meta_lock);
	} else {
		l2bsize = inode->i_sb->s_blocksize_bits;
		l2BlocksPerPage = PAGE_CACHE_SHIFT - l2bsize;
		page_index = lblock >> l2BlocksPerPage;
		page_offset = (lblock - (page_index << l2BlocksPerPage)) <<
		    l2bsize;
		if ((page_offset + size) > PAGE_CACHE_SIZE) {
			spin_unlock(&meta_lock);
			jERROR(1, ("MetaData crosses page boundary!!\n"));
			return NULL;
		}
		
		/*
		 * Locks held on aggregate inode pages are usually
		 * not held long, and they are taken in critical code
		 * paths (committing dirty inodes, txCommit thread) 
		 * 
		 * Attempt to get metapage without blocking, tapping into
		 * reserves if necessary.
		 */
		mp = NULL;
		if (JFS_IP(inode)->fileset == AGGREGATE_I) {
			mp =  mempool_alloc(metapage_mempool, GFP_ATOMIC);
			if (!mp) {
				/*
				 * mempool is supposed to protect us from
				 * failing here.  We will try a blocking
				 * call, but a deadlock is possible here
				 */
				printk(KERN_WARNING
				       "__get_metapage: atomic call to mempool_alloc failed.\n");
				printk(KERN_WARNING
				       "Will attempt blocking call\n");
			}
		}
		if (!mp) {
			metapage_t *mp2;

			spin_unlock(&meta_lock);
			mp =  mempool_alloc(metapage_mempool, GFP_NOFS);
			spin_lock(&meta_lock);

			/* we dropped the meta_lock, we need to search the
			 * hash again.
			 */
			mp2 = search_hash(hash_ptr, mapping, lblock);
			if (mp2) {
				free_metapage(mp);
				mp = mp2;
				goto page_found;
			}
		}
		mp->flag = 0;
		lock_metapage(mp);
		if (absolute)
			set_bit(META_absolute, &mp->flag);
		mp->xflag = COMMIT_PAGE;
		mp->count = 1;
		atomic_set(&mp->nohomeok,0);
		mp->mapping = mapping;
		mp->index = lblock;
		mp->page = 0;
		mp->logical_size = size;
		add_to_hash(mp, hash_ptr);
		if (!absolute)
			list_add(&mp->inode_list, &JFS_IP(inode)->mp_list);
		spin_unlock(&meta_lock);

		if (new) {
			jFYI(1,
			     ("__get_metapage: Calling grab_cache_page\n"));
			mp->page = grab_cache_page(mapping, page_index);
			if (!mp->page) {
				jERROR(1, ("grab_cache_page failed!\n"));
				goto freeit;
			} else {
				INCREMENT(mpStat.pagealloc);
				unlock_page(mp->page);
			}
		} else {
			jFYI(1,
			     ("__get_metapage: Calling read_cache_page\n"));
			mp->page = read_cache_page(mapping, lblock,
				    (filler_t *)mapping->a_ops->readpage, NULL);
			if (IS_ERR(mp->page)) {
				jERROR(1, ("read_cache_page failed!\n"));
				goto freeit;
			} else
				INCREMENT(mpStat.pagealloc);
		}
		mp->data = kmap(mp->page) + page_offset;
	}
	jFYI(1, ("__get_metapage: returning = 0x%p\n", mp));
	return mp;

freeit:
	spin_lock(&meta_lock);
	remove_from_hash(mp, hash_ptr);
	if (!absolute)
		list_del(&mp->inode_list);
	free_metapage(mp);
	spin_unlock(&meta_lock);
	return NULL;
}

void hold_metapage(metapage_t * mp, int force)
{
	spin_lock(&meta_lock);

	mp->count++;

	if (force) {
		ASSERT (!(test_bit(META_forced, &mp->flag)));
		if (trylock_metapage(mp))
			set_bit(META_forced, &mp->flag);
	} else
		lock_metapage(mp);

	spin_unlock(&meta_lock);
}

static void __write_metapage(metapage_t * mp)
{
	struct inode *ip = (struct inode *) mp->mapping->host;
	unsigned long page_index;
	unsigned long page_offset;
	int rc;
	int l2bsize = ip->i_sb->s_blocksize_bits;
	int l2BlocksPerPage = PAGE_CACHE_SHIFT - l2bsize;

	jFYI(1, ("__write_metapage: mp = 0x%p\n", mp));

	if (test_bit(META_discard, &mp->flag)) {
		/*
		 * This metadata is no longer valid
		 */
		clear_bit(META_dirty, &mp->flag);
		return;
	}

	page_index = mp->page->index;
	page_offset =
	    (mp->index - (page_index << l2BlocksPerPage)) << l2bsize;

	lock_page(mp->page);
	rc = mp->mapping->a_ops->prepare_write(NULL, mp->page, page_offset,
					       page_offset +
					       mp->logical_size);
	if (rc) {
		jERROR(1, ("prepare_write return %d!\n", rc));
		ClearPageUptodate(mp->page);
		kunmap(mp->page);
		unlock_page(mp->page);
		clear_bit(META_dirty, &mp->flag);
		return;
	}
	rc = mp->mapping->a_ops->commit_write(NULL, mp->page, page_offset,
					      page_offset +
					      mp->logical_size);
	if (rc) {
		jERROR(1, ("commit_write returned %d\n", rc));
	}

	unlock_page(mp->page);
	clear_bit(META_dirty, &mp->flag);

	jFYI(1, ("__write_metapage done\n"));
}

static inline void sync_metapage(metapage_t *mp)
{
	struct page *page = mp->page;

	page_cache_get(page);
	lock_page(page);

	/* we're done with this page - no need to check for errors */
	if (page_has_buffers(page))
		write_one_page(page, 1);
	else
		unlock_page(page);
	page_cache_release(page);
}

void release_metapage(metapage_t * mp)
{
	log_t *log;

	jFYI(1,
	     ("release_metapage: mp = 0x%p, flag = 0x%lx\n", mp,
	      mp->flag));

	spin_lock(&meta_lock);
	if (test_bit(META_forced, &mp->flag)) {
		clear_bit(META_forced, &mp->flag);
		mp->count--;
		spin_unlock(&meta_lock);
		return;
	}

	assert(mp->count);
	if (--mp->count || atomic_read(&mp->nohomeok)) {
		unlock_metapage(mp);
		spin_unlock(&meta_lock);
	} else {
		remove_from_hash(mp, meta_hash(mp->mapping, mp->index));
		if (!test_bit(META_absolute, &mp->flag))
			list_del(&mp->inode_list);
		spin_unlock(&meta_lock);

		if (mp->page) {
			kunmap(mp->page);
			mp->data = 0;
			if (test_bit(META_dirty, &mp->flag))
				__write_metapage(mp);
			if (test_bit(META_sync, &mp->flag)) {
				sync_metapage(mp);
				clear_bit(META_sync, &mp->flag);
			}

			if (test_bit(META_discard, &mp->flag)) {
				lock_page(mp->page);
				block_invalidatepage(mp->page, 0);
				unlock_page(mp->page);
			}

			page_cache_release(mp->page);
			INCREMENT(mpStat.pagefree);
		}

		if (mp->lsn) {
			/*
			 * Remove metapage from logsynclist.
			 */
			log = mp->log;
			LOGSYNC_LOCK(log);
			mp->log = 0;
			mp->lsn = 0;
			mp->clsn = 0;
			log->count--;
			list_del(&mp->synclist);
			LOGSYNC_UNLOCK(log);
		}

		free_metapage(mp);
	}
	jFYI(1, ("release_metapage: done\n"));
}

void __invalidate_metapages(struct inode *ip, s64 addr, int len)
{
	metapage_t **hash_ptr;
	unsigned long lblock;
	int l2BlocksPerPage = PAGE_CACHE_SHIFT - ip->i_sb->s_blocksize_bits;
	struct address_space *mapping = ip->i_mapping;
	metapage_t *mp;
	struct page *page;

	/*
	 * First, mark metapages to discard.  They will eventually be
	 * released, but should not be written.
	 */
	for (lblock = addr; lblock < addr + len;
	     lblock += 1 << l2BlocksPerPage) {
		hash_ptr = meta_hash(mapping, lblock);
		spin_lock(&meta_lock);
		mp = search_hash(hash_ptr, mapping, lblock);
		if (mp) {
			set_bit(META_discard, &mp->flag);
			spin_unlock(&meta_lock);
			lock_page(mp->page);
			block_invalidatepage(mp->page, 0);
			unlock_page(mp->page);
		} else {
			spin_unlock(&meta_lock);
			page = find_lock_page(mapping, lblock>>l2BlocksPerPage);
			if (page) {
				block_invalidatepage(page, 0);
				unlock_page(page);
			}
		}
	}
}

void invalidate_inode_metapages(struct inode *inode)
{
	struct list_head *ptr;
	metapage_t *mp;

	spin_lock(&meta_lock);
	list_for_each(ptr, &JFS_IP(inode)->mp_list) {
		mp = list_entry(ptr, metapage_t, inode_list);
		clear_bit(META_dirty, &mp->flag);
		set_bit(META_discard, &mp->flag);
		kunmap(mp->page);
		page_cache_release(mp->page);
		INCREMENT(mpStat.pagefree);
		mp->data = 0;
		mp->page = 0;
	}
	spin_unlock(&meta_lock);
	truncate_inode_pages(inode->i_mapping, 0);
}

#ifdef CONFIG_JFS_STATISTICS
int jfs_mpstat_read(char *buffer, char **start, off_t offset, int length,
		    int *eof, void *data)
{
	int len = 0;
	off_t begin;

	len += sprintf(buffer,
		       "JFS Metapage statistics\n"
		       "=======================\n"
		       "page allocations = %d\n"
		       "page frees = %d\n"
		       "lock waits = %d\n",
		       mpStat.pagealloc,
		       mpStat.pagefree,
		       mpStat.lockwait);

	begin = offset;
	*start = buffer + begin;
	len -= begin;

	if (len > length)
		len = length;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
#endif
