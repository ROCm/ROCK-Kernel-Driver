/*
 *  linux/fs/buffer.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting an interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it.
 */

/* Start bdflush() with kernel_thread not syscall - Paul Gortmaker, 12/95 */

/* Removed a lot of unnecessary code and simplified things now that
 * the buffer cache isn't our primary cache - Andrew Tridgell 12/96
 */

/* Speed up hash, lru, and free list operations.  Use gfp() for allocating
 * hash table, use SLAB cache for buffer heads. -DaveM
 */

/* Added 32k buffer block sizes - these are required older ARM systems.
 * - RMK
 */

/* Thread it... -DaveM */

/* async buffer flushing, 1999 Andrea Arcangeli <andrea@suse.de> */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/locks.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/sysrq.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/quotaops.h>
#include <linux/iobuf.h>
#include <linux/highmem.h>
#include <linux/completion.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>

#define MAX_BUF_PER_PAGE (PAGE_CACHE_SIZE / 512)
#define NR_RESERVED (10*MAX_BUF_PER_PAGE)
#define MAX_UNUSED_BUFFERS NR_RESERVED+20 /* don't ever have more than this 
					     number of unused buffer heads */

/* Anti-deadlock ordering:
 *	lru_list_lock > hash_table_lock > unused_list_lock
 */

#define BH_ENTRY(list) list_entry((list), struct buffer_head, b_inode_buffers)

/*
 * Hash table gook..
 */
static unsigned int bh_hash_mask;
static unsigned int bh_hash_shift;
static struct buffer_head **hash_table;
static rwlock_t hash_table_lock = RW_LOCK_UNLOCKED;

static struct buffer_head *lru_list[NR_LIST];
static spinlock_t lru_list_lock = SPIN_LOCK_UNLOCKED;
static int nr_buffers_type[NR_LIST];
static unsigned long size_buffers_type[NR_LIST];

static struct buffer_head * unused_list;
static int nr_unused_buffer_heads;
static spinlock_t unused_list_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_WAIT_QUEUE_HEAD(buffer_wait);

static int grow_buffers(kdev_t dev, unsigned long block, int size);
static void __refile_buffer(struct buffer_head *);

/* This is used by some architectures to estimate available memory. */
atomic_t buffermem_pages = ATOMIC_INIT(0);

/* Here is the parameter block for the bdflush process. If you add or
 * remove any of the parameters, make sure to update kernel/sysctl.c
 * and the documentation at linux/Documentation/sysctl/vm.txt.
 */

#define N_PARAM 9

/* The dummy values in this structure are left in there for compatibility
 * with old programs that play with the /proc entries.
 */
union bdflush_param {
	struct {
		int nfract;	/* Percentage of buffer cache dirty to 
				   activate bdflush */
		int dummy1;	/* old "ndirty" */
		int dummy2;	/* old "nrefill" */
		int dummy3;	/* unused */
		int interval;	/* jiffies delay between kupdate flushes */
		int age_buffer;	/* Time for normal buffer to age before we flush it */
		int nfract_sync;/* Percentage of buffer cache dirty to 
				   activate bdflush synchronously */
		int dummy4;	/* unused */
		int dummy5;	/* unused */
	} b_un;
	unsigned int data[N_PARAM];
} bdf_prm = {{30, 64, 64, 256, 5*HZ, 30*HZ, 60, 0, 0}};

/* These are the min and max parameter values that we will allow to be assigned */
int bdflush_min[N_PARAM] = {  0,  10,    5,   25,  0,   1*HZ,   0, 0, 0};
int bdflush_max[N_PARAM] = {100,50000, 20000, 20000,10000*HZ, 6000*HZ, 100, 0, 0};

inline void unlock_buffer(struct buffer_head *bh)
{
	clear_bit(BH_Wait_IO, &bh->b_state);
	clear_bit(BH_Lock, &bh->b_state);
	smp_mb__after_clear_bit();
	if (waitqueue_active(&bh->b_wait))
		wake_up(&bh->b_wait);
}

/*
 * Rewrote the wait-routines to use the "new" wait-queue functionality,
 * and getting rid of the cli-sti pairs. The wait-queue routines still
 * need cli-sti, but now it's just a couple of 386 instructions or so.
 *
 * Note that the real wait_on_buffer() is an inline function that checks
 * if 'b_wait' is set before calling this, so that the queues aren't set
 * up unnecessarily.
 */
void __wait_on_buffer(struct buffer_head * bh)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	get_bh(bh);
	add_wait_queue(&bh->b_wait, &wait);
	do {
		run_task_queue(&tq_disk);
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (!buffer_locked(bh))
			break;
		schedule();
	} while (buffer_locked(bh));
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&bh->b_wait, &wait);
	put_bh(bh);
}

/*
 * Default synchronous end-of-IO handler..  Just mark it up-to-date and
 * unlock the buffer. This is what ll_rw_block uses too.
 */
void end_buffer_io_sync(struct buffer_head *bh, int uptodate)
{
	mark_buffer_uptodate(bh, uptodate);
	unlock_buffer(bh);
	put_bh(bh);
}

/*
 * The buffers have been marked clean and locked.  Just submit the dang
 * things.. 
 */
static void write_locked_buffers(struct buffer_head **array, unsigned int count)
{
	do {
		struct buffer_head * bh = *array++;
		bh->b_end_io = end_buffer_io_sync;
		submit_bh(WRITE, bh);
	} while (--count);
}

/*
 * Write some buffers from the head of the dirty queue.
 *
 * This must be called with the LRU lock held, and will
 * return without it!
 */
#define NRSYNC (32)
static int write_some_buffers(kdev_t dev)
{
	struct buffer_head *next;
	struct buffer_head *array[NRSYNC];
	unsigned int count;
	int nr;

	next = lru_list[BUF_DIRTY];
	nr = nr_buffers_type[BUF_DIRTY];
	count = 0;
	while (next && --nr >= 0) {
		struct buffer_head * bh = next;
		next = bh->b_next_free;

		if (dev && bh->b_dev != dev)
			continue;
		if (test_and_set_bit(BH_Lock, &bh->b_state))
			continue;
		if (atomic_set_buffer_clean(bh)) {
			__refile_buffer(bh);
			get_bh(bh);
			array[count++] = bh;
			if (count < NRSYNC)
				continue;

			spin_unlock(&lru_list_lock);
			write_locked_buffers(array, count);
			return -EAGAIN;
		}
		unlock_buffer(bh);
		__refile_buffer(bh);
	}
	spin_unlock(&lru_list_lock);

	if (count)
		write_locked_buffers(array, count);
	return 0;
}

/*
 * Write out all buffers on the dirty list.
 */
static void write_unlocked_buffers(kdev_t dev)
{
	do {
		spin_lock(&lru_list_lock);
	} while (write_some_buffers(dev));
	run_task_queue(&tq_disk);
}

/*
 * Wait for a buffer on the proper list.
 *
 * This must be called with the LRU lock held, and
 * will return with it released.
 */
static int wait_for_buffers(kdev_t dev, int index, int refile)
{
	struct buffer_head * next;
	int nr;

	next = lru_list[index];
	nr = nr_buffers_type[index];
	while (next && --nr >= 0) {
		struct buffer_head *bh = next;
		next = bh->b_next_free;

		if (!buffer_locked(bh)) {
			if (refile)
				__refile_buffer(bh);
			continue;
		}
		if (dev && bh->b_dev != dev)
			continue;

		get_bh(bh);
		spin_unlock(&lru_list_lock);
		wait_on_buffer (bh);
		put_bh(bh);
		return -EAGAIN;
	}
	spin_unlock(&lru_list_lock);
	return 0;
}

static inline void wait_for_some_buffers(kdev_t dev)
{
	spin_lock(&lru_list_lock);
	wait_for_buffers(dev, BUF_LOCKED, 1);
}

static int wait_for_locked_buffers(kdev_t dev, int index, int refile)
{
	do {
		spin_lock(&lru_list_lock);
	} while (wait_for_buffers(dev, index, refile));
	return 0;
}

/* Call sync_buffers with wait!=0 to ensure that the call does not
 * return until all buffer writes have completed.  Sync() may return
 * before the writes have finished; fsync() may not.
 */

/* Godamity-damn.  Some buffers (bitmaps for filesystems)
 * spontaneously dirty themselves without ever brelse being called.
 * We will ultimately want to put these in a separate list, but for
 * now we search all of the lists for dirty buffers.
 */
int sync_buffers(kdev_t dev, int wait)
{
	int err = 0;

	/* One pass for no-wait, three for wait:
	 * 0) write out all dirty, unlocked buffers;
	 * 1) wait for all dirty locked buffers;
	 * 2) write out all dirty, unlocked buffers;
	 * 2) wait for completion by waiting for all buffers to unlock.
	 */
	write_unlocked_buffers(dev);
	if (wait) {
		err = wait_for_locked_buffers(dev, BUF_DIRTY, 0);
		write_unlocked_buffers(dev);
		err |= wait_for_locked_buffers(dev, BUF_LOCKED, 1);
	}
	return err;
}

int fsync_super(struct super_block *sb)
{
	kdev_t dev = sb->s_dev;
	sync_buffers(dev, 0);

	lock_kernel();
	sync_inodes_sb(sb);
	DQUOT_SYNC(dev);
	lock_super(sb);
	if (sb->s_dirt && sb->s_op && sb->s_op->write_super)
		sb->s_op->write_super(sb);
	unlock_super(sb);
	unlock_kernel();

	return sync_buffers(dev, 1);
}

int fsync_no_super(kdev_t dev)
{
	sync_buffers(dev, 0);
	return sync_buffers(dev, 1);
}

int fsync_dev(kdev_t dev)
{
	sync_buffers(dev, 0);

	lock_kernel();
	sync_inodes(dev);
	DQUOT_SYNC(dev);
	sync_supers(dev);
	unlock_kernel();

	return sync_buffers(dev, 1);
}

/*
 * There's no real reason to pretend we should
 * ever do anything differently
 */
void sync_dev(kdev_t dev)
{
	fsync_dev(dev);
}

asmlinkage long sys_sync(void)
{
	fsync_dev(0);
	return 0;
}

/*
 *	filp may be NULL if called via the msync of a vma.
 */
 
int file_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	struct inode * inode = dentry->d_inode;
	struct super_block * sb;
	kdev_t dev;
	int ret;

	lock_kernel();
	/* sync the inode to buffers */
	write_inode_now(inode, 0);

	/* sync the superblock to buffers */
	sb = inode->i_sb;
	lock_super(sb);
	if (sb->s_op && sb->s_op->write_super)
		sb->s_op->write_super(sb);
	unlock_super(sb);

	/* .. finally sync the buffers to disk */
	dev = inode->i_dev;
	ret = sync_buffers(dev, 1);
	unlock_kernel();
	return ret;
}

asmlinkage long sys_fsync(unsigned int fd)
{
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;
	int err;

	err = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	dentry = file->f_dentry;
	inode = dentry->d_inode;

	err = -EINVAL;
	if (!file->f_op || !file->f_op->fsync)
		goto out_putf;

	/* We need to protect against concurrent writers.. */
	down(&inode->i_sem);
	filemap_fdatasync(inode->i_mapping);
	err = file->f_op->fsync(file, dentry, 0);
	filemap_fdatawait(inode->i_mapping);
	up(&inode->i_sem);

out_putf:
	fput(file);
out:
	return err;
}

asmlinkage long sys_fdatasync(unsigned int fd)
{
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;
	int err;

	err = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	dentry = file->f_dentry;
	inode = dentry->d_inode;

	err = -EINVAL;
	if (!file->f_op || !file->f_op->fsync)
		goto out_putf;

	down(&inode->i_sem);
	filemap_fdatasync(inode->i_mapping);
	err = file->f_op->fsync(file, dentry, 1);
	filemap_fdatawait(inode->i_mapping);
	up(&inode->i_sem);

out_putf:
	fput(file);
out:
	return err;
}

/* After several hours of tedious analysis, the following hash
 * function won.  Do not mess with it... -DaveM
 */
#define _hashfn(dev,block)	\
	((((dev)<<(bh_hash_shift - 6)) ^ ((dev)<<(bh_hash_shift - 9))) ^ \
	 (((block)<<(bh_hash_shift - 6)) ^ ((block) >> 13) ^ \
	  ((block) << (bh_hash_shift - 12))))
#define hash(dev,block) hash_table[(_hashfn(HASHDEV(dev),block) & bh_hash_mask)]

static inline void __insert_into_hash_list(struct buffer_head *bh)
{
	struct buffer_head **head = &hash(bh->b_dev, bh->b_blocknr);
	struct buffer_head *next = *head;

	*head = bh;
	bh->b_pprev = head;
	bh->b_next = next;
	if (next != NULL)
		next->b_pprev = &bh->b_next;
}

static __inline__ void __hash_unlink(struct buffer_head *bh)
{
	if (bh->b_pprev) {
		if (bh->b_next)
			bh->b_next->b_pprev = bh->b_pprev;
		*(bh->b_pprev) = bh->b_next;
		bh->b_pprev = NULL;
	}
}

static void __insert_into_lru_list(struct buffer_head * bh, int blist)
{
	struct buffer_head **bhp = &lru_list[blist];

	if (bh->b_prev_free || bh->b_next_free) BUG();

	if(!*bhp) {
		*bhp = bh;
		bh->b_prev_free = bh;
	}
	bh->b_next_free = *bhp;
	bh->b_prev_free = (*bhp)->b_prev_free;
	(*bhp)->b_prev_free->b_next_free = bh;
	(*bhp)->b_prev_free = bh;
	nr_buffers_type[blist]++;
	size_buffers_type[blist] += bh->b_size;
}

static void __remove_from_lru_list(struct buffer_head * bh, int blist)
{
	if (bh->b_prev_free || bh->b_next_free) {
		bh->b_prev_free->b_next_free = bh->b_next_free;
		bh->b_next_free->b_prev_free = bh->b_prev_free;
		if (lru_list[blist] == bh)
			lru_list[blist] = bh->b_next_free;
		if (lru_list[blist] == bh)
			lru_list[blist] = NULL;
		bh->b_next_free = bh->b_prev_free = NULL;
		nr_buffers_type[blist]--;
		size_buffers_type[blist] -= bh->b_size;
	}
}

/* must be called with both the hash_table_lock and the lru_list_lock
   held */
static void __remove_from_queues(struct buffer_head *bh)
{
	__hash_unlink(bh);
	__remove_from_lru_list(bh, bh->b_list);
}

struct buffer_head * get_hash_table(kdev_t dev, int block, int size)
{
	struct buffer_head *bh, **p = &hash(dev, block);

	read_lock(&hash_table_lock);

	for (;;) {
		bh = *p;
		if (!bh)
			break;
		p = &bh->b_next;
		if (bh->b_blocknr != block)
			continue;
		if (bh->b_size != size)
			continue;
		if (bh->b_dev != dev)
			continue;
		get_bh(bh);
		break;
	}

	read_unlock(&hash_table_lock);
	return bh;
}

void buffer_insert_inode_queue(struct buffer_head *bh, struct inode *inode)
{
	spin_lock(&lru_list_lock);
	if (bh->b_inode)
		list_del(&bh->b_inode_buffers);
	bh->b_inode = inode;
	list_add(&bh->b_inode_buffers, &inode->i_dirty_buffers);
	spin_unlock(&lru_list_lock);
}

void buffer_insert_inode_data_queue(struct buffer_head *bh, struct inode *inode)
{
	spin_lock(&lru_list_lock);
	if (bh->b_inode)
		list_del(&bh->b_inode_buffers);
	bh->b_inode = inode;
	list_add(&bh->b_inode_buffers, &inode->i_dirty_data_buffers);
	spin_unlock(&lru_list_lock);
}

/* The caller must have the lru_list lock before calling the 
   remove_inode_queue functions.  */
static void __remove_inode_queue(struct buffer_head *bh)
{
	bh->b_inode = NULL;
	list_del(&bh->b_inode_buffers);
}

static inline void remove_inode_queue(struct buffer_head *bh)
{
	if (bh->b_inode)
		__remove_inode_queue(bh);
}

int inode_has_buffers(struct inode *inode)
{
	int ret;
	
	spin_lock(&lru_list_lock);
	ret = !list_empty(&inode->i_dirty_buffers) || !list_empty(&inode->i_dirty_data_buffers);
	spin_unlock(&lru_list_lock);
	
	return ret;
}

/* If invalidate_buffers() will trash dirty buffers, it means some kind
   of fs corruption is going on. Trashing dirty data always imply losing
   information that was supposed to be just stored on the physical layer
   by the user.

   Thus invalidate_buffers in general usage is not allwowed to trash dirty
   buffers. For example ioctl(FLSBLKBUF) expects dirty data to be preserved.

   NOTE: In the case where the user removed a removable-media-disk even if
   there's still dirty data not synced on disk (due a bug in the device driver
   or due an error of the user), by not destroying the dirty buffers we could
   generate corruption also on the next media inserted, thus a parameter is
   necessary to handle this case in the most safe way possible (trying
   to not corrupt also the new disk inserted with the data belonging to
   the old now corrupted disk). Also for the ramdisk the natural thing
   to do in order to release the ramdisk memory is to destroy dirty buffers.

   These are two special cases. Normal usage imply the device driver
   to issue a sync on the device (without waiting I/O completion) and
   then an invalidate_buffers call that doesn't trash dirty buffers.

   For handling cache coherency with the blkdev pagecache the 'update' case
   is been introduced. It is needed to re-read from disk any pinned
   buffer. NOTE: re-reading from disk is destructive so we can do it only
   when we assume nobody is changing the buffercache under our I/O and when
   we think the disk contains more recent information than the buffercache.
   The update == 1 pass marks the buffers we need to update, the update == 2
   pass does the actual I/O. */
void invalidate_bdev(struct block_device *bdev, int destroy_dirty_buffers)
{
	int i, nlist, slept;
	struct buffer_head * bh, * bh_next;
	kdev_t dev = to_kdev_t(bdev->bd_dev);	/* will become bdev */

 retry:
	slept = 0;
	spin_lock(&lru_list_lock);
	for(nlist = 0; nlist < NR_LIST; nlist++) {
		bh = lru_list[nlist];
		if (!bh)
			continue;
		for (i = nr_buffers_type[nlist]; i > 0 ; bh = bh_next, i--) {
			bh_next = bh->b_next_free;

			/* Another device? */
			if (bh->b_dev != dev)
				continue;
			/* Not hashed? */
			if (!bh->b_pprev)
				continue;
			if (buffer_locked(bh)) {
				get_bh(bh);
				spin_unlock(&lru_list_lock);
				wait_on_buffer(bh);
				slept = 1;
				spin_lock(&lru_list_lock);
				put_bh(bh);
			}

			write_lock(&hash_table_lock);
			/* All buffers in the lru lists are mapped */
			if (!buffer_mapped(bh))
				BUG();
			if (buffer_dirty(bh))
				printk("invalidate: dirty buffer\n");
			if (!atomic_read(&bh->b_count)) {
				if (destroy_dirty_buffers || !buffer_dirty(bh)) {
					remove_inode_queue(bh);
				}
			} else
				printk("invalidate: busy buffer\n");

			write_unlock(&hash_table_lock);
			if (slept)
				goto out;
		}
	}
out:
	spin_unlock(&lru_list_lock);
	if (slept)
		goto retry;

	/* Get rid of the page cache */
	invalidate_inode_pages(bdev->bd_inode);
}

void __invalidate_buffers(kdev_t dev, int destroy_dirty_buffers)
{
	struct block_device *bdev = bdget(dev);
	if (bdev) {
		invalidate_bdev(bdev, destroy_dirty_buffers);
		bdput(bdev);
	}
}

static void free_more_memory(void)
{
	balance_dirty();
	wakeup_bdflush();
	current->policy |= SCHED_YIELD;
	__set_current_state(TASK_RUNNING);
	schedule();
}

void init_buffer(struct buffer_head *bh, bh_end_io_t *handler, void *private)
{
	bh->b_list = BUF_CLEAN;
	bh->b_end_io = handler;
	bh->b_private = private;
}

static void end_buffer_io_async(struct buffer_head * bh, int uptodate)
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
	 * if none of the buffers had errors then we can set the
	 * page uptodate:
	 */
	if (!PageError(page))
		SetPageUptodate(page);

	/*
	 * Run the hooks that have to be done when a page I/O has completed.
	 */
	if (PageTestandClearDecrAfter(page))
		atomic_dec(&nr_async_pages);

	UnlockPage(page);

	return;

still_busy:
	spin_unlock_irqrestore(&page_uptodate_lock, flags);
	return;
}

inline void set_buffer_async_io(struct buffer_head *bh) {
    bh->b_end_io = end_buffer_io_async ;
    mark_buffer_async(bh, 1);
}

/*
 * Synchronise all the inode's dirty buffers to the disk.
 *
 * We have conflicting pressures: we want to make sure that all
 * initially dirty buffers get waited on, but that any subsequently
 * dirtied buffers don't.  After all, we don't want fsync to last
 * forever if somebody is actively writing to the file.
 *
 * Do this in two main stages: first we copy dirty buffers to a
 * temporary inode list, queueing the writes as we go.  Then we clean
 * up, waiting for those writes to complete.
 * 
 * During this second stage, any subsequent updates to the file may end
 * up refiling the buffer on the original inode's dirty list again, so
 * there is a chance we will end up with a buffer queued for write but
 * not yet completed on that list.  So, as a final cleanup we go through
 * the osync code to catch these locked, dirty buffers without requeuing
 * any newly dirty buffers for write.
 */

int fsync_inode_buffers(struct inode *inode)
{
	struct buffer_head *bh;
	struct inode tmp;
	int err = 0, err2;
	
	INIT_LIST_HEAD(&tmp.i_dirty_buffers);
	
	spin_lock(&lru_list_lock);

	while (!list_empty(&inode->i_dirty_buffers)) {
		bh = BH_ENTRY(inode->i_dirty_buffers.next);
		list_del(&bh->b_inode_buffers);
		if (!buffer_dirty(bh) && !buffer_locked(bh))
			bh->b_inode = NULL;
		else {
			bh->b_inode = &tmp;
			list_add(&bh->b_inode_buffers, &tmp.i_dirty_buffers);
			if (buffer_dirty(bh)) {
				get_bh(bh);
				spin_unlock(&lru_list_lock);
				ll_rw_block(WRITE, 1, &bh);
				brelse(bh);
				spin_lock(&lru_list_lock);
			}
		}
	}

	while (!list_empty(&tmp.i_dirty_buffers)) {
		bh = BH_ENTRY(tmp.i_dirty_buffers.prev);
		remove_inode_queue(bh);
		get_bh(bh);
		spin_unlock(&lru_list_lock);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			err = -EIO;
		brelse(bh);
		spin_lock(&lru_list_lock);
	}
	
	spin_unlock(&lru_list_lock);
	err2 = osync_inode_buffers(inode);

	if (err)
		return err;
	else
		return err2;
}

int fsync_inode_data_buffers(struct inode *inode)
{
	struct buffer_head *bh;
	struct inode tmp;
	int err = 0, err2;
	
	INIT_LIST_HEAD(&tmp.i_dirty_data_buffers);
	
	spin_lock(&lru_list_lock);

	while (!list_empty(&inode->i_dirty_data_buffers)) {
		bh = BH_ENTRY(inode->i_dirty_data_buffers.next);
		list_del(&bh->b_inode_buffers);
		if (!buffer_dirty(bh) && !buffer_locked(bh))
			bh->b_inode = NULL;
		else {
			bh->b_inode = &tmp;
			list_add(&bh->b_inode_buffers, &tmp.i_dirty_data_buffers);
			if (buffer_dirty(bh)) {
				get_bh(bh);
				spin_unlock(&lru_list_lock);
				ll_rw_block(WRITE, 1, &bh);
				brelse(bh);
				spin_lock(&lru_list_lock);
			}
		}
	}

	while (!list_empty(&tmp.i_dirty_data_buffers)) {
		bh = BH_ENTRY(tmp.i_dirty_data_buffers.prev);
		remove_inode_queue(bh);
		get_bh(bh);
		spin_unlock(&lru_list_lock);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			err = -EIO;
		brelse(bh);
		spin_lock(&lru_list_lock);
	}
	
	spin_unlock(&lru_list_lock);
	err2 = osync_inode_data_buffers(inode);

	if (err)
		return err;
	else
		return err2;
}

/*
 * osync is designed to support O_SYNC io.  It waits synchronously for
 * all already-submitted IO to complete, but does not queue any new
 * writes to the disk.
 *
 * To do O_SYNC writes, just queue the buffer writes with ll_rw_block as
 * you dirty the buffers, and then use osync_inode_buffers to wait for
 * completion.  Any other dirty buffers which are not yet queued for
 * write will not be flushed to disk by the osync.
 */

int osync_inode_buffers(struct inode *inode)
{
	struct buffer_head *bh;
	struct list_head *list;
	int err = 0;

	spin_lock(&lru_list_lock);
	
 repeat:
	
	for (list = inode->i_dirty_buffers.prev; 
	     bh = BH_ENTRY(list), list != &inode->i_dirty_buffers;
	     list = bh->b_inode_buffers.prev) {
		if (buffer_locked(bh)) {
			get_bh(bh);
			spin_unlock(&lru_list_lock);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh))
				err = -EIO;
			brelse(bh);
			spin_lock(&lru_list_lock);
			goto repeat;
		}
	}

	spin_unlock(&lru_list_lock);
	return err;
}

int osync_inode_data_buffers(struct inode *inode)
{
	struct buffer_head *bh;
	struct list_head *list;
	int err = 0;

	spin_lock(&lru_list_lock);
	
 repeat:

	for (list = inode->i_dirty_data_buffers.prev; 
	     bh = BH_ENTRY(list), list != &inode->i_dirty_data_buffers;
	     list = bh->b_inode_buffers.prev) {
		if (buffer_locked(bh)) {
			get_bh(bh);
			spin_unlock(&lru_list_lock);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh))
				err = -EIO;
			brelse(bh);
			spin_lock(&lru_list_lock);
			goto repeat;
		}
	}

	spin_unlock(&lru_list_lock);
	return err;
}


/*
 * Invalidate any and all dirty buffers on a given inode.  We are
 * probably unmounting the fs, but that doesn't mean we have already
 * done a sync().  Just drop the buffers from the inode list.
 */
void invalidate_inode_buffers(struct inode *inode)
{
	struct list_head * entry;
	
	spin_lock(&lru_list_lock);
	while ((entry = inode->i_dirty_buffers.next) != &inode->i_dirty_buffers)
		remove_inode_queue(BH_ENTRY(entry));
	while ((entry = inode->i_dirty_data_buffers.next) != &inode->i_dirty_data_buffers)
		remove_inode_queue(BH_ENTRY(entry));
	spin_unlock(&lru_list_lock);
}


/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algorithm is changed: hopefully better, and an elusive bug removed.
 *
 * 14.02.92: changed it to sync dirty buffers a bit: better performance
 * when the filesystem starts to get full of dirty blocks (I hope).
 */
struct buffer_head * getblk(kdev_t dev, int block, int size)
{
	for (;;) {
		struct buffer_head * bh;

		bh = get_hash_table(dev, block, size);
		if (bh)
			return bh;

		if (!grow_buffers(dev, block, size))
			free_more_memory();
	}
}

/* -1 -> no need to flush
    0 -> async flush
    1 -> sync flush (wait for I/O completion) */
static int balance_dirty_state(void)
{
	unsigned long dirty, tot, hard_dirty_limit, soft_dirty_limit;

	dirty = size_buffers_type[BUF_DIRTY] >> PAGE_SHIFT;
	tot = nr_free_buffer_pages();

	dirty *= 100;
	soft_dirty_limit = tot * bdf_prm.b_un.nfract;
	hard_dirty_limit = tot * bdf_prm.b_un.nfract_sync;

	/* First, check for the "real" dirty limit. */
	if (dirty > soft_dirty_limit) {
		if (dirty > hard_dirty_limit)
			return 1;
		return 0;
	}

	return -1;
}

/*
 * if a new dirty buffer is created we need to balance bdflush.
 *
 * in the future we might want to make bdflush aware of different
 * pressures on different devices - thus the (currently unused)
 * 'dev' parameter.
 */
void balance_dirty(void)
{
	int state = balance_dirty_state();

	if (state < 0)
		return;

	/* If we're getting into imbalance, start write-out */
	spin_lock(&lru_list_lock);
	write_some_buffers(NODEV);

	/*
	 * And if we're _really_ out of balance, wait for
	 * some of the dirty/locked buffers ourselves and
	 * start bdflush.
	 * This will throttle heavy writers.
	 */
	if (state > 0) {
		wait_for_some_buffers(NODEV);
		wakeup_bdflush();
	}
}

inline void __mark_dirty(struct buffer_head *bh)
{
	bh->b_flushtime = jiffies + bdf_prm.b_un.age_buffer;
	refile_buffer(bh);
}

/* atomic version, the user must call balance_dirty() by hand
   as soon as it become possible to block */
void __mark_buffer_dirty(struct buffer_head *bh)
{
	if (!atomic_set_buffer_dirty(bh))
		__mark_dirty(bh);
}

void mark_buffer_dirty(struct buffer_head *bh)
{
	if (!atomic_set_buffer_dirty(bh)) {
		__mark_dirty(bh);
		balance_dirty();
	}
}

/*
 * A buffer may need to be moved from one buffer list to another
 * (e.g. in case it is not shared any more). Handle this.
 */
static void __refile_buffer(struct buffer_head *bh)
{
	int dispose = BUF_CLEAN;
	if (buffer_locked(bh))
		dispose = BUF_LOCKED;
	if (buffer_dirty(bh))
		dispose = BUF_DIRTY;
	if (dispose != bh->b_list) {
		__remove_from_lru_list(bh, bh->b_list);
		bh->b_list = dispose;
		if (dispose == BUF_CLEAN)
			remove_inode_queue(bh);
		__insert_into_lru_list(bh, dispose);
	}
}

void refile_buffer(struct buffer_head *bh)
{
	spin_lock(&lru_list_lock);
	__refile_buffer(bh);
	spin_unlock(&lru_list_lock);
}

/*
 * Release a buffer head
 */
void __brelse(struct buffer_head * buf)
{
	if (atomic_read(&buf->b_count)) {
		put_bh(buf);
		return;
	}
	printk(KERN_ERR "VFS: brelse: Trying to free free buffer\n");
}

/*
 * bforget() is like brelse(), except it puts the buffer on the
 * free list if it can.. We can NOT free the buffer if:
 *  - there are other users of it
 *  - it is locked and thus can have active IO
 */
void __bforget(struct buffer_head * buf)
{
	__brelse(buf);
}

/**
 *	bread() - reads a specified block and returns the bh
 *	@block: number of block
 *	@size: size (in bytes) to read
 * 
 *	Reads a specified block, and returns buffer head that
 *	contains it. It returns NULL if the block was unreadable.
 */
struct buffer_head * bread(kdev_t dev, int block, int size)
{
	struct buffer_head * bh;

	bh = getblk(dev, block, size);
	if (buffer_uptodate(bh))
		return bh;
	ll_rw_block(READ, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse(bh);
	return NULL;
}

/*
 * Note: the caller should wake up the buffer_wait list if needed.
 */
static __inline__ void __put_unused_buffer_head(struct buffer_head * bh)
{
	if (bh->b_inode)
		BUG();
	if (nr_unused_buffer_heads >= MAX_UNUSED_BUFFERS) {
		kmem_cache_free(bh_cachep, bh);
	} else {
		bh->b_dev = B_FREE;
		bh->b_blocknr = -1;
		bh->b_this_page = NULL;

		nr_unused_buffer_heads++;
		bh->b_next_free = unused_list;
		unused_list = bh;
	}
}

/*
 * Reserve NR_RESERVED buffer heads for async IO requests to avoid
 * no-buffer-head deadlock.  Return NULL on failure; waiting for
 * buffer heads is now handled in create_buffers().
 */ 
static struct buffer_head * get_unused_buffer_head(int async)
{
	struct buffer_head * bh;

	spin_lock(&unused_list_lock);
	if (nr_unused_buffer_heads > NR_RESERVED) {
		bh = unused_list;
		unused_list = bh->b_next_free;
		nr_unused_buffer_heads--;
		spin_unlock(&unused_list_lock);
		return bh;
	}
	spin_unlock(&unused_list_lock);

	/* This is critical.  We can't call out to the FS
	 * to get more buffer heads, because the FS may need
	 * more buffer-heads itself.  Thus SLAB_NOFS.
	 */
	if((bh = kmem_cache_alloc(bh_cachep, SLAB_NOFS)) != NULL) {
		bh->b_blocknr = -1;
		bh->b_this_page = NULL;
		return bh;
	}

	/*
	 * If we need an async buffer, use the reserved buffer heads.
	 */
	if (async) {
		spin_lock(&unused_list_lock);
		if (unused_list) {
			bh = unused_list;
			unused_list = bh->b_next_free;
			nr_unused_buffer_heads--;
			spin_unlock(&unused_list_lock);
			return bh;
		}
		spin_unlock(&unused_list_lock);
	}

	return NULL;
}

void set_bh_page (struct buffer_head *bh, struct page *page, unsigned long offset)
{
	bh->b_page = page;
	if (offset >= PAGE_SIZE)
		BUG();
	if (PageHighMem(page))
		/*
		 * This catches illegal uses and preserves the offset:
		 */
		bh->b_data = (char *)(0 + offset);
	else
		bh->b_data = page_address(page) + offset;
}

/*
 * Create the appropriate buffers when given a page for data area and
 * the size of each buffer.. Use the bh->b_this_page linked list to
 * follow the buffers created.  Return NULL if unable to create more
 * buffers.
 * The async flag is used to differentiate async IO (paging, swapping)
 * from ordinary buffer allocations, and only async requests are allowed
 * to sleep waiting for buffer heads. 
 */
static struct buffer_head * create_buffers(struct page * page, unsigned long size, int async)
{
	struct buffer_head *bh, *head;
	long offset;

try_again:
	head = NULL;
	offset = PAGE_SIZE;
	while ((offset -= size) >= 0) {
		bh = get_unused_buffer_head(async);
		if (!bh)
			goto no_grow;

		bh->b_dev = NODEV;
		bh->b_this_page = head;
		head = bh;

		bh->b_state = 0;
		bh->b_next_free = NULL;
		bh->b_pprev = NULL;
		atomic_set(&bh->b_count, 0);
		bh->b_size = size;

		set_bh_page(bh, page, offset);

		bh->b_list = BUF_CLEAN;
		bh->b_end_io = NULL;
	}
	return head;
/*
 * In case anything failed, we just free everything we got.
 */
no_grow:
	if (head) {
		spin_lock(&unused_list_lock);
		do {
			bh = head;
			head = head->b_this_page;
			__put_unused_buffer_head(bh);
		} while (head);
		spin_unlock(&unused_list_lock);

		/* Wake up any waiters ... */
		wake_up(&buffer_wait);
	}

	/*
	 * Return failure for non-async IO requests.  Async IO requests
	 * are not allowed to fail, so we have to wait until buffer heads
	 * become available.  But we don't want tasks sleeping with 
	 * partially complete buffers, so all were released above.
	 */
	if (!async)
		return NULL;

	/* We're _really_ low on memory. Now we just
	 * wait for old buffer heads to become free due to
	 * finishing IO.  Since this is an async request and
	 * the reserve list is empty, we're sure there are 
	 * async buffer heads in use.
	 */
	run_task_queue(&tq_disk);

	free_more_memory();
	goto try_again;
}

/*
 * Called when truncating a buffer on a page completely.
 */
static void discard_buffer(struct buffer_head * bh)
{
	if (buffer_mapped(bh)) {
		mark_buffer_clean(bh);
		lock_buffer(bh);
		clear_bit(BH_Uptodate, &bh->b_state);
		clear_bit(BH_Mapped, &bh->b_state);
		clear_bit(BH_Req, &bh->b_state);
		clear_bit(BH_New, &bh->b_state);
		unlock_buffer(bh);
	}
}

/*
 * We don't have to release all buffers here, but
 * we have to be sure that no dirty buffer is left
 * and no IO is going on (no buffer is locked), because
 * we have truncated the file and are going to free the
 * blocks on-disk..
 */
int discard_bh_page(struct page *page, unsigned long offset, int drop_pagecache)
{
	struct buffer_head *head, *bh, *next;
	unsigned int curr_off = 0;

	if (!PageLocked(page))
		BUG();
	if (!page->buffers)
		return 1;

	head = page->buffers;
	bh = head;
	do {
		unsigned int next_off = curr_off + bh->b_size;
		next = bh->b_this_page;

		/*
		 * is this block fully flushed?
		 */
		if (offset <= curr_off)
			discard_buffer(bh);
		curr_off = next_off;
		bh = next;
	} while (bh != head);

	/*
	 * subtle. We release buffer-heads only if this is
	 * the 'final' flushpage. We have invalidated the get_block
	 * cached value unconditionally, so real IO is not
	 * possible anymore.
	 *
	 * If the free doesn't work out, the buffers can be
	 * left around - they just turn into anonymous buffers
	 * instead.
	 */
	if (!offset) {
		if (!try_to_free_buffers(page, 0))
			return 0;
	}

	return 1;
}

void create_empty_buffers(struct page *page, kdev_t dev, unsigned long blocksize)
{
	struct buffer_head *bh, *head, *tail;

	/* FIXME: create_buffers should fail if there's no enough memory */
	head = create_buffers(page, blocksize, 1);
	if (page->buffers)
		BUG();

	bh = head;
	do {
		bh->b_dev = dev;
		bh->b_blocknr = 0;
		bh->b_end_io = NULL;
		tail = bh;
		bh = bh->b_this_page;
	} while (bh);
	tail->b_this_page = head;
	page->buffers = head;
	page_cache_get(page);
}

/*
 * We are taking a block for data and we don't want any output from any
 * buffer-cache aliases starting from return from that function and
 * until the moment when something will explicitly mark the buffer
 * dirty (hopefully that will not happen until we will free that block ;-)
 * We don't even need to mark it not-uptodate - nobody can expect
 * anything from a newly allocated buffer anyway. We used to used
 * unmap_buffer() for such invalidation, but that was wrong. We definitely
 * don't want to mark the alias unmapped, for example - it would confuse
 * anyone who might pick it with bread() afterwards...
 */

static void unmap_underlying_metadata(struct buffer_head * bh)
{
	struct buffer_head *old_bh;

	old_bh = get_hash_table(bh->b_dev, bh->b_blocknr, bh->b_size);
	if (old_bh) {
		mark_buffer_clean(old_bh);
		wait_on_buffer(old_bh);
		clear_bit(BH_Req, &old_bh->b_state);
		/* Here we could run brelse or bforget. We use
		   bforget because it will try to put the buffer
		   in the freelist. */
		__bforget(old_bh);
	}
}

/*
 * NOTE! All mapped/uptodate combinations are valid:
 *
 *	Mapped	Uptodate	Meaning
 *
 *	No	No		"unknown" - must do get_block()
 *	No	Yes		"hole" - zero-filled
 *	Yes	No		"allocated" - allocated on disk, not read in
 *	Yes	Yes		"valid" - allocated and up-to-date in memory.
 *
 * "Dirty" is valid only with the last case (mapped+uptodate).
 */

/*
 * block_write_full_page() is SMP-safe - currently it's still
 * being called with the kernel lock held, but the code is ready.
 */
static int __block_write_full_page(struct inode *inode, struct page *page, get_block_t *get_block)
{
	int err, i;
	unsigned long block;
	struct buffer_head *bh, *head;

	if (!PageLocked(page))
		BUG();

	if (!page->buffers)
		create_empty_buffers(page, inode->i_dev, 1 << inode->i_blkbits);
	head = page->buffers;

	block = page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);

	bh = head;
	i = 0;

	/* Stage 1: make sure we have all the buffers mapped! */
	do {
		/*
		 * If the buffer isn't up-to-date, we can't be sure
		 * that the buffer has been initialized with the proper
		 * block number information etc..
		 *
		 * Leave it to the low-level FS to make all those
		 * decisions (block #0 may actually be a valid block)
		 */
		if (!buffer_mapped(bh)) {
			err = get_block(inode, block, bh, 1);
			if (err)
				goto out;
			if (buffer_new(bh))
				unmap_underlying_metadata(bh);
		}
		bh = bh->b_this_page;
		block++;
	} while (bh != head);

	/* Stage 2: lock the buffers, mark them clean */
	do {
		lock_buffer(bh);
		set_buffer_async_io(bh);
		set_bit(BH_Uptodate, &bh->b_state);
		clear_bit(BH_Dirty, &bh->b_state);
		bh = bh->b_this_page;
	} while (bh != head);

	/* Stage 3: submit the IO */
	do {
		struct buffer_head *next = bh->b_this_page;
		submit_bh(WRITE, bh);
		bh = next;
	} while (bh != head);

	/* Done - end_buffer_io_async will unlock */
	SetPageUptodate(page);
	return 0;

out:
	ClearPageUptodate(page);
	UnlockPage(page);
	return err;
}

static int __block_prepare_write(struct inode *inode, struct page *page,
		unsigned from, unsigned to, get_block_t *get_block)
{
	unsigned block_start, block_end;
	unsigned long block;
	int err = 0;
	unsigned blocksize, bbits;
	struct buffer_head *bh, *head, *wait[2], **wait_bh=wait;
	char *kaddr = kmap(page);

	blocksize = 1 << inode->i_blkbits;
	if (!page->buffers)
		create_empty_buffers(page, inode->i_dev, blocksize);
	head = page->buffers;

	bbits = inode->i_blkbits;
	block = page->index << (PAGE_CACHE_SHIFT - bbits);

	for(bh = head, block_start = 0; bh != head || !block_start;
	    block++, block_start=block_end, bh = bh->b_this_page) {
		if (!bh)
			BUG();
		block_end = block_start+blocksize;
		if (block_end <= from)
			continue;
		if (block_start >= to)
			break;
		if (!buffer_mapped(bh)) {
			err = get_block(inode, block, bh, 1);
			if (err)
				goto out;
			if (buffer_new(bh)) {
				unmap_underlying_metadata(bh);
				if (Page_Uptodate(page)) {
					set_bit(BH_Uptodate, &bh->b_state);
					continue;
				}
				if (block_end > to)
					memset(kaddr+to, 0, block_end-to);
				if (block_start < from)
					memset(kaddr+block_start, 0, from-block_start);
				if (block_end > to || block_start < from)
					flush_dcache_page(page);
				continue;
			}
		}
		if (Page_Uptodate(page)) {
			set_bit(BH_Uptodate, &bh->b_state);
			continue; 
		}
		if (!buffer_uptodate(bh) &&
		     (block_start < from || block_end > to)) {
			ll_rw_block(READ, 1, &bh);
			*wait_bh++=bh;
		}
	}
	/*
	 * If we issued read requests - let them complete.
	 */
	while(wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		err = -EIO;
		if (!buffer_uptodate(*wait_bh))
			goto out;
	}
	return 0;
out:
	return err;
}

static int __block_commit_write(struct inode *inode, struct page *page,
		unsigned from, unsigned to)
{
	unsigned block_start, block_end;
	int partial = 0, need_balance_dirty = 0;
	unsigned blocksize;
	struct buffer_head *bh, *head;

	blocksize = 1 << inode->i_blkbits;

	for(bh = head = page->buffers, block_start = 0;
	    bh != head || !block_start;
	    block_start=block_end, bh = bh->b_this_page) {
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = 1;
		} else {
			set_bit(BH_Uptodate, &bh->b_state);
			if (!atomic_set_buffer_dirty(bh)) {
				__mark_dirty(bh);
				buffer_insert_inode_data_queue(bh, inode);
				need_balance_dirty = 1;
			}
		}
	}

	if (need_balance_dirty)
		balance_dirty();
	/*
	 * is this a partial write that happened to make all buffers
	 * uptodate then we can optimize away a bogus readpage() for
	 * the next read(). Here we 'discover' wether the page went
	 * uptodate as a result of this (potentially partial) write.
	 */
	if (!partial)
		SetPageUptodate(page);
	return 0;
}

/*
 * Generic "read page" function for block devices that have the normal
 * get_block functionality. This is most of the block device filesystems.
 * Reads the page asynchronously --- the unlock_buffer() and
 * mark_buffer_uptodate() functions propagate buffer state into the
 * page struct once IO has completed.
 */
int block_read_full_page(struct page *page, get_block_t *get_block)
{
	struct inode *inode = page->mapping->host;
	unsigned long iblock, lblock;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	unsigned int blocksize, blocks;
	int nr, i;

	if (!PageLocked(page))
		PAGE_BUG(page);
	blocksize = 1 << inode->i_blkbits;
	if (!page->buffers)
		create_empty_buffers(page, inode->i_dev, blocksize);
	head = page->buffers;

	blocks = PAGE_CACHE_SIZE >> inode->i_blkbits;
	iblock = page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);
	lblock = (inode->i_size+blocksize-1) >> inode->i_blkbits;
	bh = head;
	nr = 0;
	i = 0;

	do {
		if (buffer_uptodate(bh))
			continue;

		if (!buffer_mapped(bh)) {
			if (iblock < lblock) {
				if (get_block(inode, iblock, bh, 0))
					continue;
			}
			if (!buffer_mapped(bh)) {
				memset(kmap(page) + i*blocksize, 0, blocksize);
				flush_dcache_page(page);
				kunmap(page);
				set_bit(BH_Uptodate, &bh->b_state);
				continue;
			}
			/* get_block() might have updated the buffer synchronously */
			if (buffer_uptodate(bh))
				continue;
		}

		arr[nr] = bh;
		nr++;
	} while (i++, iblock++, (bh = bh->b_this_page) != head);

	if (!nr) {
		/*
		 * all buffers are uptodate - we can set the page
		 * uptodate as well.
		 */
		SetPageUptodate(page);
		UnlockPage(page);
		return 0;
	}

	/* Stage two: lock the buffers */
	for (i = 0; i < nr; i++) {
		struct buffer_head * bh = arr[i];
		lock_buffer(bh);
		set_buffer_async_io(bh);
	}

	/* Stage 3: start the IO */
	for (i = 0; i < nr; i++)
		submit_bh(READ, arr[i]);

	return 0;
}

/*
 * For moronic filesystems that do not allow holes in file.
 * We may have to extend the file.
 */

int cont_prepare_write(struct page *page, unsigned offset, unsigned to, get_block_t *get_block, unsigned long *bytes)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct page *new_page;
	unsigned long pgpos;
	long status;
	unsigned zerofrom;
	unsigned blocksize = 1 << inode->i_blkbits;
	char *kaddr;

	while(page->index > (pgpos = *bytes>>PAGE_CACHE_SHIFT)) {
		status = -ENOMEM;
		new_page = grab_cache_page(mapping, pgpos);
		if (!new_page)
			goto out;
		/* we might sleep */
		if (*bytes>>PAGE_CACHE_SHIFT != pgpos) {
			UnlockPage(new_page);
			page_cache_release(new_page);
			continue;
		}
		zerofrom = *bytes & ~PAGE_CACHE_MASK;
		if (zerofrom & (blocksize-1)) {
			*bytes |= (blocksize-1);
			(*bytes)++;
		}
		status = __block_prepare_write(inode, new_page, zerofrom,
						PAGE_CACHE_SIZE, get_block);
		if (status)
			goto out_unmap;
		kaddr = page_address(new_page);
		memset(kaddr+zerofrom, 0, PAGE_CACHE_SIZE-zerofrom);
		flush_dcache_page(new_page);
		__block_commit_write(inode, new_page, zerofrom, PAGE_CACHE_SIZE);
		kunmap(new_page);
		UnlockPage(new_page);
		page_cache_release(new_page);
	}

	if (page->index < pgpos) {
		/* completely inside the area */
		zerofrom = offset;
	} else {
		/* page covers the boundary, find the boundary offset */
		zerofrom = *bytes & ~PAGE_CACHE_MASK;

		/* if we will expand the thing last block will be filled */
		if (to > zerofrom && (zerofrom & (blocksize-1))) {
			*bytes |= (blocksize-1);
			(*bytes)++;
		}

		/* starting below the boundary? Nothing to zero out */
		if (offset <= zerofrom)
			zerofrom = offset;
	}
	status = __block_prepare_write(inode, page, zerofrom, to, get_block);
	if (status)
		goto out1;
	kaddr = page_address(page);
	if (zerofrom < offset) {
		memset(kaddr+zerofrom, 0, offset-zerofrom);
		flush_dcache_page(page);
		__block_commit_write(inode, page, zerofrom, offset);
	}
	return 0;
out1:
	ClearPageUptodate(page);
	kunmap(page);
	return status;

out_unmap:
	ClearPageUptodate(new_page);
	kunmap(new_page);
	UnlockPage(new_page);
	page_cache_release(new_page);
out:
	return status;
}

int block_prepare_write(struct page *page, unsigned from, unsigned to,
			get_block_t *get_block)
{
	struct inode *inode = page->mapping->host;
	int err = __block_prepare_write(inode, page, from, to, get_block);
	if (err) {
		ClearPageUptodate(page);
		kunmap(page);
	}
	return err;
}

int block_commit_write(struct page *page, unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	__block_commit_write(inode,page,from,to);
	kunmap(page);
	return 0;
}

int generic_commit_write(struct file *file, struct page *page,
		unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;
	__block_commit_write(inode,page,from,to);
	kunmap(page);
	if (pos > inode->i_size) {
		inode->i_size = pos;
		mark_inode_dirty(inode);
	}
	return 0;
}

int block_truncate_page(struct address_space *mapping, loff_t from, get_block_t *get_block)
{
	unsigned long index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize, iblock, length, pos;
	struct inode *inode = mapping->host;
	struct page *page;
	struct buffer_head *bh;
	int err;

	blocksize = 1 << inode->i_blkbits;
	length = offset & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;

	length = blocksize - length;
	iblock = index << (PAGE_CACHE_SHIFT - inode->i_blkbits);
	
	page = grab_cache_page(mapping, index);
	err = -ENOMEM;
	if (!page)
		goto out;

	if (!page->buffers)
		create_empty_buffers(page, inode->i_dev, blocksize);

	/* Find the buffer that contains "offset" */
	bh = page->buffers;
	pos = blocksize;
	while (offset >= pos) {
		bh = bh->b_this_page;
		iblock++;
		pos += blocksize;
	}

	err = 0;
	if (!buffer_mapped(bh)) {
		/* Hole? Nothing to do */
		if (buffer_uptodate(bh))
			goto unlock;
		get_block(inode, iblock, bh, 0);
		/* Still unmapped? Nothing to do */
		if (!buffer_mapped(bh))
			goto unlock;
	}

	/* Ok, it's mapped. Make sure it's up-to-date */
	if (Page_Uptodate(page))
		set_bit(BH_Uptodate, &bh->b_state);

	if (!buffer_uptodate(bh)) {
		err = -EIO;
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		/* Uhhuh. Read error. Complain and punt. */
		if (!buffer_uptodate(bh))
			goto unlock;
	}

	memset(kmap(page) + offset, 0, length);
	flush_dcache_page(page);
	kunmap(page);

	__mark_buffer_dirty(bh);
	err = 0;

unlock:
	UnlockPage(page);
	page_cache_release(page);
out:
	return err;
}

int block_write_full_page(struct page *page, get_block_t *get_block)
{
	struct inode *inode = page->mapping->host;
	unsigned long end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	unsigned offset;
	int err;

	/* easy case */
	if (page->index < end_index)
		return __block_write_full_page(inode, page, get_block);

	/* things got complicated... */
	offset = inode->i_size & (PAGE_CACHE_SIZE-1);
	/* OK, are we completely out? */
	if (page->index >= end_index+1 || !offset) {
		UnlockPage(page);
		return -EIO;
	}

	/* Sigh... will have to work, then... */
	err = __block_prepare_write(inode, page, 0, offset, get_block);
	if (!err) {
		memset(page_address(page) + offset, 0, PAGE_CACHE_SIZE - offset);
		flush_dcache_page(page);
		__block_commit_write(inode,page,0,offset);
done:
		kunmap(page);
		UnlockPage(page);
		return err;
	}
	ClearPageUptodate(page);
	goto done;
}

int generic_block_bmap(struct address_space *mapping, long block, get_block_t *get_block)
{
	struct buffer_head tmp;
	struct inode *inode = mapping->host;
	tmp.b_state = 0;
	tmp.b_blocknr = 0;
	get_block(inode, block, &tmp, 0);
	return tmp.b_blocknr;
}

/*
 * IO completion routine for a buffer_head being used for kiobuf IO: we
 * can't dispatch the kiobuf callback until io_count reaches 0.  
 */

static void end_buffer_io_kiobuf(struct buffer_head *bh, int uptodate)
{
	struct kiobuf *kiobuf;
	
	mark_buffer_uptodate(bh, uptodate);

	kiobuf = bh->b_private;
	unlock_buffer(bh);
	end_kio_request(kiobuf, uptodate);
}

/*
 * For brw_kiovec: submit a set of buffer_head temporary IOs and wait
 * for them to complete.  Clean up the buffer_heads afterwards.  
 */

static int wait_kio(int rw, int nr, struct buffer_head *bh[], int size)
{
	int iosize, err;
	int i;
	struct buffer_head *tmp;

	iosize = 0;
	err = 0;

	for (i = nr; --i >= 0; ) {
		iosize += size;
		tmp = bh[i];
		if (buffer_locked(tmp)) {
			wait_on_buffer(tmp);
		}
		
		if (!buffer_uptodate(tmp)) {
			/* We are traversing bh'es in reverse order so
                           clearing iosize on error calculates the
                           amount of IO before the first error. */
			iosize = 0;
			err = -EIO;
		}
	}
	
	if (iosize)
		return iosize;
	return err;
}

/*
 * Start I/O on a physical range of kernel memory, defined by a vector
 * of kiobuf structs (much like a user-space iovec list).
 *
 * The kiobuf must already be locked for IO.  IO is submitted
 * asynchronously: you need to check page->locked, page->uptodate, and
 * maybe wait on page->wait.
 *
 * It is up to the caller to make sure that there are enough blocks
 * passed in to completely map the iobufs to disk.
 */

int brw_kiovec(int rw, int nr, struct kiobuf *iovec[], 
	       kdev_t dev, unsigned long b[], int size)
{
	int		err;
	int		length;
	int		transferred;
	int		i;
	int		bufind;
	int		pageind;
	int		bhind;
	int		offset;
	unsigned long	blocknr;
	struct kiobuf *	iobuf = NULL;
	struct page *	map;
	struct buffer_head *tmp, **bhs = NULL;

	if (!nr)
		return 0;
	
	/* 
	 * First, do some alignment and validity checks 
	 */
	for (i = 0; i < nr; i++) {
		iobuf = iovec[i];
		if ((iobuf->offset & (size-1)) ||
		    (iobuf->length & (size-1)))
			return -EINVAL;
		if (!iobuf->nr_pages)
			panic("brw_kiovec: iobuf not initialised");
	}

	/* 
	 * OK to walk down the iovec doing page IO on each page we find. 
	 */
	bufind = bhind = transferred = err = 0;
	for (i = 0; i < nr; i++) {
		iobuf = iovec[i];
		offset = iobuf->offset;
		length = iobuf->length;
		iobuf->errno = 0;
		if (!bhs)
			bhs = iobuf->bh;
		
		for (pageind = 0; pageind < iobuf->nr_pages; pageind++) {
			map  = iobuf->maplist[pageind];
			if (!map) {
				err = -EFAULT;
				goto finished;
			}
			
			while (length > 0) {
				blocknr = b[bufind++];
				if (blocknr == -1UL) {
					if (rw == READ) {
						/* there was an hole in the filesystem */
						memset(kmap(map) + offset, 0, size);
						flush_dcache_page(map);
						kunmap(map);

						transferred += size;
						goto skip_block;
					} else
						BUG();
				}
				tmp = bhs[bhind++];

				tmp->b_size = size;
				set_bh_page(tmp, map, offset);
				tmp->b_this_page = tmp;

				init_buffer(tmp, end_buffer_io_kiobuf, iobuf);
				tmp->b_dev = dev;
				tmp->b_blocknr = blocknr;
				tmp->b_state = (1 << BH_Mapped) | (1 << BH_Lock) | (1 << BH_Req);

				if (rw == WRITE) {
					set_bit(BH_Uptodate, &tmp->b_state);
					clear_bit(BH_Dirty, &tmp->b_state);
				} else
					set_bit(BH_Uptodate, &tmp->b_state);

				atomic_inc(&iobuf->io_count);
				submit_bh(rw, tmp);
				/* 
				 * Wait for IO if we have got too much 
				 */
				if (bhind >= KIO_MAX_SECTORS) {
					kiobuf_wait_for_io(iobuf); /* wake-one */
					err = wait_kio(rw, bhind, bhs, size);
					if (err >= 0)
						transferred += err;
					else
						goto finished;
					bhind = 0;
				}

			skip_block:
				length -= size;
				offset += size;

				if (offset >= PAGE_SIZE) {
					offset = 0;
					break;
				}
			} /* End of block loop */
		} /* End of page loop */		
	} /* End of iovec loop */

	/* Is there any IO still left to submit? */
	if (bhind) {
		kiobuf_wait_for_io(iobuf); /* wake-one */
		err = wait_kio(rw, bhind, bhs, size);
		if (err >= 0)
			transferred += err;
		else
			goto finished;
	}

 finished:
	if (transferred)
		return transferred;
	return err;
}

/*
 * Start I/O on a page.
 * This function expects the page to be locked and may return
 * before I/O is complete. You then have to check page->locked,
 * page->uptodate, and maybe wait on page->wait.
 *
 * brw_page() is SMP-safe, although it's being called with the
 * kernel lock held - but the code is ready.
 *
 * FIXME: we need a swapper_inode->get_block function to remove
 *        some of the bmap kludges and interface ugliness here.
 */
int brw_page(int rw, struct page *page, kdev_t dev, int b[], int size)
{
	struct buffer_head *head, *bh;

	if (!PageLocked(page))
		panic("brw_page: page not locked for I/O");

	if (!page->buffers)
		create_empty_buffers(page, dev, size);
	head = bh = page->buffers;

	/* Stage 1: lock all the buffers */
	do {
		lock_buffer(bh);
		bh->b_blocknr = *(b++);
		set_bit(BH_Mapped, &bh->b_state);
		set_buffer_async_io(bh);
		bh = bh->b_this_page;
	} while (bh != head);

	/* Stage 2: start the IO */
	do {
		struct buffer_head *next = bh->b_this_page;
		submit_bh(rw, bh);
		bh = next;
	} while (bh != head);
	return 0;
}

int block_symlink(struct inode *inode, const char *symname, int len)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page = grab_cache_page(mapping, 0);
	int err = -ENOMEM;
	char *kaddr;

	if (!page)
		goto fail;
	err = mapping->a_ops->prepare_write(NULL, page, 0, len-1);
	if (err)
		goto fail_map;
	kaddr = page_address(page);
	memcpy(kaddr, symname, len-1);
	mapping->a_ops->commit_write(NULL, page, 0, len-1);
	/*
	 * Notice that we are _not_ going to block here - end of page is
	 * unmapped, so this will only try to map the rest of page, see
	 * that it is unmapped (typically even will not look into inode -
	 * ->i_size will be enough for everything) and zero it out.
	 * OTOH it's obviously correct and should make the page up-to-date.
	 */
	err = mapping->a_ops->readpage(NULL, page);
	wait_on_page(page);
	page_cache_release(page);
	if (err < 0)
		goto fail;
	mark_inode_dirty(inode);
	return 0;
fail_map:
	UnlockPage(page);
	page_cache_release(page);
fail:
	return err;
}

static inline void link_dev_buffers(struct page * page, struct buffer_head *head)
{
	struct buffer_head *bh, *tail;

	bh = head;
	do {
		tail = bh;
		bh = bh->b_this_page;
	} while (bh);
	tail->b_this_page = head;
	page->buffers = head;
	page_cache_get(page);
}

/*
 * Create the page-cache page that contains the requested block
 */
static struct page * grow_dev_page(struct block_device *bdev, unsigned long index, int size)
{
	struct page * page;
	struct buffer_head *bh;

	page = find_or_create_page(bdev->bd_inode->i_mapping, index, GFP_NOFS);
	if (IS_ERR(page))
		return NULL;

	if (!PageLocked(page))
		BUG();

	bh = page->buffers;
	if (bh) {
		if (bh->b_size == size)
			return page;
		if (!try_to_free_buffers(page, GFP_NOFS))
			goto failed;
	}

	bh = create_buffers(page, size, 0);
	if (!bh)
		goto failed;
	link_dev_buffers(page, bh);
	return page;

failed:
	UnlockPage(page);
	page_cache_release(page);
	return NULL;
}

static void hash_page_buffers(struct page *page, kdev_t dev, int block, int size)
{
	struct buffer_head *head = page->buffers;
	struct buffer_head *bh = head;
	unsigned int uptodate;

	uptodate = 1 << BH_Mapped;
	if (Page_Uptodate(page))
		uptodate |= 1 << BH_Uptodate;

	write_lock(&hash_table_lock);
	do {
		if (!(bh->b_state & (1 << BH_Mapped))) {
			init_buffer(bh, NULL, NULL);
			bh->b_dev = dev;
			bh->b_blocknr = block;
			bh->b_state = uptodate;
		}

		/* Insert the buffer into the hash lists if necessary */
		if (!bh->b_pprev)
			__insert_into_hash_list(bh);

		block++;
		bh = bh->b_this_page;
	} while (bh != head);
	write_unlock(&hash_table_lock);
}

/*
 * Try to increase the number of buffers available: the size argument
 * is used to determine what kind of buffers we want.
 */
static int grow_buffers(kdev_t dev, unsigned long block, int size)
{
	struct page * page;
	struct block_device *bdev;
	unsigned long index;
	int sizebits;

	if ((size & 511) || (size > PAGE_SIZE)) {
		printk(KERN_ERR "VFS: grow_buffers: size = %d\n",size);
		return 0;
	}
	sizebits = -1;
	do {
		sizebits++;
	} while ((size << sizebits) < PAGE_SIZE);

	index = block >> sizebits;
	block = index << sizebits;

	bdev = bdget(kdev_t_to_nr(dev));
	if (!bdev) {
		printk("No block device for %s\n", kdevname(dev));
		BUG();
	}

	/* Create a page with the proper size buffers.. */
	page = grow_dev_page(bdev, index, size);

	/* This is "wrong" - talk to Al Viro */
	atomic_dec(&bdev->bd_count);
	if (!page)
		return 0;

	/* Hash in the buffers on the hash list */
	hash_page_buffers(page, dev, block, size);
	UnlockPage(page);
	page_cache_release(page);

	/* We hashed up this page, so increment buffermem */
	atomic_inc(&buffermem_pages);
	return 1;
}

static int sync_page_buffers(struct buffer_head *bh, unsigned int gfp_mask)
{
	struct buffer_head * p = bh;
	int tryagain = 1;

	do {
		if (buffer_dirty(p) || buffer_locked(p)) {
			if (test_and_set_bit(BH_Wait_IO, &p->b_state)) {
				if (buffer_dirty(p)) {
					ll_rw_block(WRITE, 1, &p);
					tryagain = 0;
				} else if (buffer_locked(p)) {
					if (gfp_mask & __GFP_WAITBUF) {
						wait_on_buffer(p);
						tryagain = 1;
					} else
						tryagain = 0;
				}
			} else
				tryagain = 0;
		}
		p = p->b_this_page;
	} while (p != bh);

	return tryagain;
}

/*
 * Can the buffer be thrown out?
 */
#define BUFFER_BUSY_BITS	((1<<BH_Dirty) | (1<<BH_Lock))
#define buffer_busy(bh)		(atomic_read(&(bh)->b_count) | ((bh)->b_state & BUFFER_BUSY_BITS))

/*
 * try_to_free_buffers() checks if all the buffers on this particular page
 * are unused, and free's the page if so.
 *
 * Wake up bdflush() if this fails - if we're running low on memory due
 * to dirty buffers, we need to flush them out as quickly as possible.
 *
 * NOTE: There are quite a number of ways that threads of control can
 *       obtain a reference to a buffer head within a page.  So we must
 *	 lock out all of these paths to cleanly toss the page.
 */
int try_to_free_buffers(struct page * page, unsigned int gfp_mask)
{
	struct buffer_head * tmp, * bh = page->buffers;

cleaned_buffers_try_again:
	spin_lock(&lru_list_lock);
	write_lock(&hash_table_lock);
	tmp = bh;
	do {
		if (buffer_busy(tmp))
			goto busy_buffer_page;
		tmp = tmp->b_this_page;
	} while (tmp != bh);

	spin_lock(&unused_list_lock);
	tmp = bh;

	/* if this buffer was hashed, this page counts as buffermem */
	if (bh->b_pprev)
		atomic_dec(&buffermem_pages);
	do {
		struct buffer_head * p = tmp;
		tmp = tmp->b_this_page;

		if (p->b_dev == B_FREE) BUG();

		remove_inode_queue(p);
		__remove_from_queues(p);
		__put_unused_buffer_head(p);
	} while (tmp != bh);
	spin_unlock(&unused_list_lock);

	/* Wake up anyone waiting for buffer heads */
	wake_up(&buffer_wait);

	/* And free the page */
	page->buffers = NULL;
	page_cache_release(page);
	write_unlock(&hash_table_lock);
	spin_unlock(&lru_list_lock);
	return 1;

busy_buffer_page:
	/* Uhhuh, start writeback so that we don't end up with all dirty pages */
	write_unlock(&hash_table_lock);
	spin_unlock(&lru_list_lock);
	if (gfp_mask & __GFP_IO) {
		if ((gfp_mask & __GFP_HIGHIO) || !PageHighMem(page)) {
			if (sync_page_buffers(bh, gfp_mask)) {
				/* no IO or waiting next time */
				gfp_mask = 0;
				goto cleaned_buffers_try_again;
			}
		}
	}
	if (balance_dirty_state() >= 0)
		wakeup_bdflush();
	return 0;
}

/* ================== Debugging =================== */

void show_buffers(void)
{
#ifdef CONFIG_SMP
	struct buffer_head * bh;
	int found = 0, locked = 0, dirty = 0, used = 0, lastused = 0;
	int nlist;
	static char *buf_types[NR_LIST] = { "CLEAN", "LOCKED", "DIRTY", };
#endif

	printk("Buffer memory:   %6dkB\n",
			atomic_read(&buffermem_pages) << (PAGE_SHIFT-10));

#ifdef CONFIG_SMP /* trylock does nothing on UP and so we could deadlock */
	if (!spin_trylock(&lru_list_lock))
		return;
	for(nlist = 0; nlist < NR_LIST; nlist++) {
		found = locked = dirty = used = lastused = 0;
		bh = lru_list[nlist];
		if(!bh) continue;

		do {
			found++;
			if (buffer_locked(bh))
				locked++;
			if (buffer_dirty(bh))
				dirty++;
			if (atomic_read(&bh->b_count))
				used++, lastused = found;
			bh = bh->b_next_free;
		} while (bh != lru_list[nlist]);
		{
			int tmp = nr_buffers_type[nlist];
			if (found != tmp)
				printk("%9s: BUG -> found %d, reported %d\n",
				       buf_types[nlist], found, tmp);
		}
		printk("%9s: %d buffers, %lu kbyte, %d used (last=%d), "
		       "%d locked, %d dirty\n",
		       buf_types[nlist], found, size_buffers_type[nlist]>>10,
		       used, lastused, locked, dirty);
	}
	spin_unlock(&lru_list_lock);
#endif
}

/* ===================== Init ======================= */

/*
 * allocate the hash table and init the free list
 * Use gfp() for the hash table to decrease TLB misses, use
 * SLAB cache for buffer heads.
 */
void __init buffer_init(unsigned long mempages)
{
	int order, i;
	unsigned int nr_hash;

	/* The buffer cache hash table is less important these days,
	 * trim it a bit.
	 */
	mempages >>= 14;

	mempages *= sizeof(struct buffer_head *);

	for (order = 0; (1 << order) < mempages; order++)
		;

	/* try to allocate something until we get it or we're asking
	   for something that is really too small */

	do {
		unsigned long tmp;

		nr_hash = (PAGE_SIZE << order) / sizeof(struct buffer_head *);
		bh_hash_mask = (nr_hash - 1);

		tmp = nr_hash;
		bh_hash_shift = 0;
		while((tmp >>= 1UL) != 0UL)
			bh_hash_shift++;

		hash_table = (struct buffer_head **)
		    __get_free_pages(GFP_ATOMIC, order);
	} while (hash_table == NULL && --order > 0);
	printk("Buffer-cache hash table entries: %d (order: %d, %ld bytes)\n",
	       nr_hash, order, (PAGE_SIZE << order));

	if (!hash_table)
		panic("Failed to allocate buffer hash table\n");

	/* Setup hash chains. */
	for(i = 0; i < nr_hash; i++)
		hash_table[i] = NULL;

	/* Setup lru lists. */
	for(i = 0; i < NR_LIST; i++)
		lru_list[i] = NULL;

}


/* ====================== bdflush support =================== */

/* This is a simple kernel daemon, whose job it is to provide a dynamic
 * response to dirty buffers.  Once this process is activated, we write back
 * a limited number of buffers to the disks and then go back to sleep again.
 */

DECLARE_WAIT_QUEUE_HEAD(bdflush_wait);

void wakeup_bdflush(void)
{
	wake_up_interruptible(&bdflush_wait);
}

/* 
 * Here we attempt to write back old buffers.  We also try to flush inodes 
 * and supers as well, since this function is essentially "update", and 
 * otherwise there would be no way of ensuring that these quantities ever 
 * get written back.  Ideally, we would have a timestamp on the inodes
 * and superblocks so that we could write back only the old ones as well
 */

static int sync_old_buffers(void)
{
	lock_kernel();
	sync_unlocked_inodes();
	sync_supers(0);
	unlock_kernel();

	for (;;) {
		struct buffer_head *bh;

		spin_lock(&lru_list_lock);
		bh = lru_list[BUF_DIRTY];
		if (!bh || time_before(jiffies, bh->b_flushtime))
			break;
		if (write_some_buffers(NODEV))
			continue;
		return 0;
	}
	spin_unlock(&lru_list_lock);
	return 0;
}

int block_sync_page(struct page *page)
{
	run_task_queue(&tq_disk);
	return 0;
}

/* This is the interface to bdflush.  As we get more sophisticated, we can
 * pass tuning parameters to this "process", to adjust how it behaves. 
 * We would want to verify each parameter, however, to make sure that it 
 * is reasonable. */

asmlinkage long sys_bdflush(int func, long data)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (func == 1) {
		/* do_exit directly and let kupdate to do its work alone. */
		do_exit(0);
#if 0 /* left here as it's the only example of lazy-mm-stuff used from
	 a syscall that doesn't care about the current mm context. */
		int error;
		struct mm_struct *user_mm;

		/*
		 * bdflush will spend all of it's time in kernel-space,
		 * without touching user-space, so we can switch it into
		 * 'lazy TLB mode' to reduce the cost of context-switches
		 * to and from bdflush.
		 */
		user_mm = start_lazy_tlb();
		error = sync_old_buffers();
		end_lazy_tlb(user_mm);
		return error;
#endif
	}

	/* Basically func 1 means read param 1, 2 means write param 1, etc */
	if (func >= 2) {
		int i = (func-2) >> 1;
		if (i >= 0 && i < N_PARAM) {
			if ((func & 1) == 0)
				return put_user(bdf_prm.data[i], (int*)data);

			if (data >= bdflush_min[i] && data <= bdflush_max[i]) {
				bdf_prm.data[i] = data;
				return 0;
			}
		}
		return -EINVAL;
	}

	/* Having func 0 used to launch the actual bdflush and then never
	 * return (unless explicitly killed). We return zero here to 
	 * remain semi-compatible with present update(8) programs.
	 */
	return 0;
}

/*
 * This is the actual bdflush daemon itself. It used to be started from
 * the syscall above, but now we launch it ourselves internally with
 * kernel_thread(...)  directly after the first thread in init/main.c
 */
int bdflush(void *startup)
{
	struct task_struct *tsk = current;

	/*
	 *	We have a bare-bones task_struct, and really should fill
	 *	in a few more things so "top" and /proc/2/{exe,root,cwd}
	 *	display semi-sane things. Not real crucial though...  
	 */

	tsk->session = 1;
	tsk->pgrp = 1;
	strcpy(tsk->comm, "bdflush");

	/* avoid getting signals */
	spin_lock_irq(&tsk->sigmask_lock);
	flush_signals(tsk);
	sigfillset(&tsk->blocked);
	recalc_sigpending(tsk);
	spin_unlock_irq(&tsk->sigmask_lock);

	complete((struct completion *)startup);

	for (;;) {
		CHECK_EMERGENCY_SYNC

		spin_lock(&lru_list_lock);
		if (!write_some_buffers(NODEV) || balance_dirty_state() < 0) {
			wait_for_some_buffers(NODEV);
			interruptible_sleep_on(&bdflush_wait);
		}
	}
}

/*
 * This is the kernel update daemon. It was used to live in userspace
 * but since it's need to run safely we want it unkillable by mistake.
 * You don't need to change your userspace configuration since
 * the userspace `update` will do_exit(0) at the first sys_bdflush().
 */
int kupdate(void *startup)
{
	struct task_struct * tsk = current;
	int interval;

	tsk->session = 1;
	tsk->pgrp = 1;
	strcpy(tsk->comm, "kupdated");

	/* sigstop and sigcont will stop and wakeup kupdate */
	spin_lock_irq(&tsk->sigmask_lock);
	sigfillset(&tsk->blocked);
	siginitsetinv(&current->blocked, sigmask(SIGCONT) | sigmask(SIGSTOP));
	recalc_sigpending(tsk);
	spin_unlock_irq(&tsk->sigmask_lock);

	complete((struct completion *)startup);

	for (;;) {
		wait_for_some_buffers(NODEV);

		/* update interval */
		interval = bdf_prm.b_un.interval;
		if (interval) {
			tsk->state = TASK_INTERRUPTIBLE;
			schedule_timeout(interval);
		} else {
		stop_kupdate:
			tsk->state = TASK_STOPPED;
			schedule(); /* wait for SIGCONT */
		}
		/* check for sigstop */
		if (signal_pending(tsk)) {
			int stopped = 0;
			spin_lock_irq(&tsk->sigmask_lock);
			if (sigismember(&tsk->pending.signal, SIGSTOP)) {
				sigdelset(&tsk->pending.signal, SIGSTOP);
				stopped = 1;
			}
			recalc_sigpending(tsk);
			spin_unlock_irq(&tsk->sigmask_lock);
			if (stopped)
				goto stop_kupdate;
		}
#ifdef DEBUG
		printk(KERN_DEBUG "kupdate() activated...\n");
#endif
		sync_old_buffers();
	}
}

static int __init bdflush_init(void)
{
	static struct completion startup __initdata = COMPLETION_INITIALIZER(startup);

	kernel_thread(bdflush, &startup, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	wait_for_completion(&startup);
	kernel_thread(kupdate, &startup, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	wait_for_completion(&startup);
	return 0;
}

module_init(bdflush_init)

