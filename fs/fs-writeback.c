/*
 * fs/fs-writeback.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains all the functions related to writing back and waiting
 * upon dirty inodes against superblocks, and writing back dirty
 * pages against inodes.  ie: data writeback.  Writeout of the
 * inode itself is not handled here.
 *
 * 10Apr2002	akpm@zip.com.au
 *		Split out of fs/inode.c
 *		Additions for address_space-based writeback
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>

extern struct super_block *blockdev_superblock;

/**
 *	__mark_inode_dirty -	internal function
 *	@inode: inode to mark
 *	@flags: what kind of dirty (i.e. I_DIRTY_SYNC)
 *	Mark an inode as dirty. Callers should use mark_inode_dirty or
 *  	mark_inode_dirty_sync.
 *
 * Put the inode on the super block's dirty list.
 *
 * CAREFUL! We mark it dirty unconditionally, but move it onto the
 * dirty list only if it is hashed or if it refers to a blockdev.
 * If it was not hashed, it will never be added to the dirty list
 * even if it is later hashed, as it will have been marked dirty already.
 *
 * In short, make sure you hash any inodes _before_ you start marking
 * them dirty.
 *
 * This function *must* be atomic for the I_DIRTY_PAGES case -
 * set_page_dirty() is called under spinlock in several places.
 */
void __mark_inode_dirty(struct inode *inode, int flags)
{
	struct super_block *sb = inode->i_sb;

	if (!sb)
		return;		/* swapper_space */

	/*
	 * Don't do this for I_DIRTY_PAGES - that doesn't actually
	 * dirty the inode itself
	 */
	if (flags & (I_DIRTY_SYNC | I_DIRTY_DATASYNC)) {
		if (sb->s_op && sb->s_op->dirty_inode)
			sb->s_op->dirty_inode(inode);
	}

	/* avoid the locking if we can */
	if ((inode->i_state & flags) == flags)
		return;

	spin_lock(&inode_lock);
	if ((inode->i_state & flags) != flags) {
		const int was_dirty = inode->i_state & I_DIRTY;
		struct address_space *mapping = inode->i_mapping;

		inode->i_state |= flags;

		if (!was_dirty)
			mapping->dirtied_when = jiffies;

		/*
		 * If the inode is locked, just update its dirty state. 
		 * The unlocker will place the inode on the appropriate
		 * superblock list, based upon its state.
		 */
		if (inode->i_state & I_LOCK)
			goto out;

		/*
		 * Only add valid (hashed) inode to the superblock's
		 * dirty list.  Add blockdev inodes as well.
		 */
		if (list_empty(&inode->i_hash) && !S_ISBLK(inode->i_mode))
			goto out;

		/*
		 * If the inode was already on s_dirty, don't reposition
		 * it (that would break s_dirty time-ordering).
		 */
		if (!was_dirty)
			list_move(&inode->i_list, &sb->s_dirty);
	}
out:
	spin_unlock(&inode_lock);
}

static void write_inode(struct inode *inode, int sync)
{
	if (inode->i_sb->s_op && inode->i_sb->s_op->write_inode &&
			!is_bad_inode(inode))
		inode->i_sb->s_op->write_inode(inode, sync);
}

/*
 * Write a single inode's dirty pages and inode data out to disk.
 * If `sync' is set, wait on the writeout.
 * Subtract the number of written pages from nr_to_write.
 *
 * Normally it is not legal for a single process to lock more than one
 * page at a time, due to ab/ba deadlock problems.  But writepages()
 * does want to lock a large number of pages, without immediately submitting
 * I/O against them (starting I/O is a "deferred unlock_page").
 *
 * However it *is* legal to lock multiple pages, if this is only ever performed
 * by a single process.  We provide that exclusion via locking in the
 * filesystem's ->writepages a_op. This ensures that only a single
 * process is locking multiple pages against this inode.  And as I/O is
 * submitted against all those locked pages, there is no deadlock.
 *
 * Called under inode_lock.
 */
static void
__sync_single_inode(struct inode *inode, int wait,
			struct writeback_control *wbc)
{
	unsigned dirty;
	unsigned long orig_dirtied_when;
	struct address_space *mapping = inode->i_mapping;
	struct super_block *sb = inode->i_sb;

	BUG_ON(inode->i_state & I_LOCK);

	/* Set I_LOCK, reset I_DIRTY */
	dirty = inode->i_state & I_DIRTY;
	inode->i_state |= I_LOCK;
	inode->i_state &= ~I_DIRTY;
	orig_dirtied_when = mapping->dirtied_when;
	mapping->dirtied_when = 0;	/* assume it's whole-file writeback */
	spin_unlock(&inode_lock);

	do_writepages(mapping, wbc);

	/* Don't write the inode if only I_DIRTY_PAGES was set */
	if (dirty & (I_DIRTY_SYNC | I_DIRTY_DATASYNC))
		write_inode(inode, wait);

	if (wait)
		filemap_fdatawait(mapping);

	spin_lock(&inode_lock);

	inode->i_state &= ~I_LOCK;
	if (!(inode->i_state & I_FREEING)) {
		list_del(&inode->i_list);
		if (inode->i_state & I_DIRTY) {		/* Redirtied */
			list_add(&inode->i_list, &sb->s_dirty);
		} else {
			if (!list_empty(&mapping->dirty_pages) ||
					!list_empty(&mapping->io_pages)) {
			 	/* Not a whole-file writeback */
				mapping->dirtied_when = orig_dirtied_when;
				inode->i_state |= I_DIRTY_PAGES;
				list_add_tail(&inode->i_list, &sb->s_dirty);
			} else if (atomic_read(&inode->i_count)) {
				list_add(&inode->i_list, &inode_in_use);
			} else {
				list_add(&inode->i_list, &inode_unused);
			}
		}
	}
	wake_up_inode(inode);
}

/*
 * Write out an inode's dirty pages.  Called under inode_lock.
 */
static void
__writeback_single_inode(struct inode *inode, int sync,
			struct writeback_control *wbc)
{
	if (current_is_pdflush() && (inode->i_state & I_LOCK))
		return;

	while (inode->i_state & I_LOCK) {
		__iget(inode);
		spin_unlock(&inode_lock);
		__wait_on_inode(inode);
		iput(inode);
		spin_lock(&inode_lock);
	}
	__sync_single_inode(inode, sync, wbc);
}

/*
 * Write out a superblock's list of dirty inodes.  A wait will be performed
 * upon no inodes, all inodes or the final one, depending upon sync_mode.
 *
 * If older_than_this is non-NULL, then only write out mappings which
 * had their first dirtying at a time earlier than *older_than_this.
 *
 * If we're a pdlfush thread, then implement pdflush collision avoidance
 * against the entire list.
 *
 * WB_SYNC_HOLD is a hack for sys_sync(): reattach the inode to sb->s_dirty so
 * that it can be located for waiting on in __writeback_single_inode().
 *
 * Called under inode_lock.
 *
 * If `bdi' is non-zero then we're being asked to writeback a specific queue.
 * This function assumes that the blockdev superblock's inodes are backed by
 * a variety of queues, so all inodes are searched.  For other superblocks,
 * assume that all inodes are backed by the same queue.
 *
 * FIXME: this linear search could get expensive with many fileystems.  But
 * how to fix?  We need to go from an address_space to all inodes which share
 * a queue with that address_space.
 *
 * The inodes to be written are parked on sb->s_io.  They are moved back onto
 * sb->s_dirty as they are selected for writing.  This way, none can be missed
 * on the writer throttling path, and we get decent balancing between many
 * thrlttled threads: we don't want them all piling up on __wait_on_inode.
 */
static void
sync_sb_inodes(struct super_block *sb, struct writeback_control *wbc)
{
	struct list_head *tmp;
	struct list_head *head;
	const unsigned long start = jiffies;	/* livelock avoidance */

	list_splice_init(&sb->s_dirty, &sb->s_io);
	head = &sb->s_io;
	while ((tmp = head->prev) != head) {
		struct inode *inode = list_entry(tmp, struct inode, i_list);
		struct address_space *mapping = inode->i_mapping;
		struct backing_dev_info *bdi;
		int really_sync;

		if (wbc->bdi && mapping->backing_dev_info != wbc->bdi) {
			if (sb != blockdev_superblock)
				break;		/* inappropriate superblock */
			list_move(&inode->i_list, &sb->s_dirty);
			continue;		/* not this blockdev */
		}

		/* Was this inode dirtied after sync_sb_inodes was called? */
		if (time_after(mapping->dirtied_when, start))
			break;

		if (wbc->older_than_this && time_after(mapping->dirtied_when,
						*wbc->older_than_this))
			goto out;

		bdi = mapping->backing_dev_info;
		if (current_is_pdflush() && !writeback_acquire(bdi))
			break;

		really_sync = (wbc->sync_mode == WB_SYNC_ALL);
		if ((wbc->sync_mode == WB_SYNC_LAST) && (head->prev == head))
			really_sync = 1;

		BUG_ON(inode->i_state & I_FREEING);
		__iget(inode);
		list_move(&inode->i_list, &sb->s_dirty);
		__writeback_single_inode(inode, really_sync, wbc);
		if (wbc->sync_mode == WB_SYNC_HOLD) {
			mapping->dirtied_when = jiffies;
			list_move(&inode->i_list, &sb->s_dirty);
		}
		if (current_is_pdflush())
			writeback_release(bdi);
		spin_unlock(&inode_lock);
		iput(inode);
		spin_lock(&inode_lock);
		if (wbc->nr_to_write <= 0)
			break;
	}
out:
	/*
	 * Leave any unwritten inodes on s_io.
	 */
	return;
}

/*
 * Start writeback of dirty pagecache data against all unlocked inodes.
 *
 * Note:
 * We don't need to grab a reference to superblock here. If it has non-empty
 * ->s_dirty it's hadn't been killed yet and kill_super() won't proceed
 * past sync_inodes_sb() until both the ->s_dirty and ->s_io lists are
 * empty. Since __sync_single_inode() regains inode_lock before it finally moves
 * inode from superblock lists we are OK.
 *
 * If `older_than_this' is non-zero then only flush inodes which have a
 * flushtime older than *older_than_this.
 *
 * If `bdi' is non-zero then we will scan the first inode against each
 * superblock until we find the matching ones.  One group will be the dirty
 * inodes against a filesystem.  Then when we hit the dummy blockdev superblock,
 * sync_sb_inodes will seekout the blockdev which matches `bdi'.  Maybe not
 * super-efficient but we're about to do a ton of I/O...
 */
void
writeback_inodes(struct writeback_control *wbc)
{
	struct super_block *sb;

	spin_lock(&inode_lock);
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		if (!list_empty(&sb->s_dirty) || !list_empty(&sb->s_io)) {
			spin_unlock(&sb_lock);
			sync_sb_inodes(sb, wbc);
			spin_lock(&sb_lock);
		}
		if (wbc->nr_to_write <= 0)
			break;
	}
	spin_unlock(&sb_lock);
	spin_unlock(&inode_lock);
}

/*
 * writeback and wait upon the filesystem's dirty inodes.  The caller will
 * do this in two passes - one to write, and one to wait.  WB_SYNC_HOLD is
 * used to park the written inodes on sb->s_dirty for the wait pass.
 *
 * A finite limit is set on the number of pages which will be written.
 * To prevent infinite livelock of sys_sync().
 */
void sync_inodes_sb(struct super_block *sb, int wait)
{
	struct page_state ps;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= wait ? WB_SYNC_ALL : WB_SYNC_HOLD,
		.older_than_this = NULL,
		.nr_to_write	= 0,
	};

	get_page_state(&ps);
	wbc.nr_to_write = ps.nr_dirty + ps.nr_dirty / 4;
	spin_lock(&inode_lock);
	sync_sb_inodes(sb, &wbc);
	spin_unlock(&inode_lock);
}

/*
 * Rather lame livelock avoidance.
 */
static void set_sb_syncing(int val)
{
	struct super_block *sb;
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		sb->s_syncing = val;
	}
	spin_unlock(&sb_lock);
}

/*
 * Find a superblock with inodes that need to be synced
 */
static struct super_block *get_super_to_sync(void)
{
	struct super_block *sb;
restart:
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		if (sb->s_syncing)
			continue;
		sb->s_syncing = 1;
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (!sb->s_root) {
			drop_super(sb);
			goto restart;
		}
		return sb;
	}
	spin_unlock(&sb_lock);
	return NULL;
}

/**
 * sync_inodes
 *
 * sync_inodes() goes through each super block's dirty inode list, writes the
 * inodes out, waits on the writeout and puts the inodes back on the normal
 * list.
 *
 * This is for sys_sync().  fsync_dev() uses the same algorithm.  The subtle
 * part of the sync functions is that the blockdev "superblock" is processed
 * last.  This is because the write_inode() function of a typical fs will
 * perform no I/O, but will mark buffers in the blockdev mapping as dirty.
 * What we want to do is to perform all that dirtying first, and then write
 * back all those inode blocks via the blockdev mapping in one sweep.  So the
 * additional (somewhat redundant) sync_blockdev() calls here are to make
 * sure that really happens.  Because if we call sync_inodes_sb(wait=1) with
 * outstanding dirty inodes, the writeback goes block-at-a-time within the
 * filesystem's write_inode().  This is extremely slow.
 */
void sync_inodes(int wait)
{
	struct super_block *sb;

	set_sb_syncing(0);
	while ((sb = get_super_to_sync()) != NULL) {
		sync_inodes_sb(sb, 0);
		sync_blockdev(sb->s_bdev);
		drop_super(sb);
	}
	if (wait) {
		set_sb_syncing(0);
		while ((sb = get_super_to_sync()) != NULL) {
			sync_inodes_sb(sb, 1);
			sync_blockdev(sb->s_bdev);
			drop_super(sb);
		}
	}
}

/**
 *	write_inode_now	-	write an inode to disk
 *	@inode: inode to write to disk
 *	@sync: whether the write should be synchronous or not
 *
 *	This function commits an inode to disk immediately if it is
 *	dirty. This is primarily needed by knfsd.
 */
 
void write_inode_now(struct inode *inode, int sync)
{
	struct writeback_control wbc = {
		.nr_to_write = LONG_MAX,
	};

	spin_lock(&inode_lock);
	__writeback_single_inode(inode, sync, &wbc);
	spin_unlock(&inode_lock);
	if (sync)
		wait_on_inode(inode);
}

/**
 * generic_osync_inode - flush all dirty data for a given inode to disk
 * @inode: inode to write
 * @what:  what to write and wait upon
 *
 * This can be called by file_write functions for files which have the
 * O_SYNC flag set, to flush dirty writes to disk.
 *
 * @what is a bitmask, specifying which part of the inode's data should be
 * written and waited upon:
 *
 *    OSYNC_DATA:     i_mapping's dirty data
 *    OSYNC_METADATA: the buffers at i_mapping->private_list
 *    OSYNC_INODE:    the inode itself
 */

int generic_osync_inode(struct inode *inode, int what)
{
	int err = 0;
	int need_write_inode_now = 0;
	int err2;

	if (what & OSYNC_DATA)
		err = filemap_fdatawrite(inode->i_mapping);
	if (what & (OSYNC_METADATA|OSYNC_DATA)) {
		err2 = sync_mapping_buffers(inode->i_mapping);
		if (!err)
			err = err2;
	}
	if (what & OSYNC_DATA) {
		err2 = filemap_fdatawait(inode->i_mapping);
		if (!err)
			err = err2;
	}

	spin_lock(&inode_lock);
	if ((inode->i_state & I_DIRTY) &&
	    ((what & OSYNC_INODE) || (inode->i_state & I_DIRTY_DATASYNC)))
		need_write_inode_now = 1;
	spin_unlock(&inode_lock);

	if (need_write_inode_now)
		write_inode_now(inode, 1);
	else
		wait_on_inode(inode);

	return err;
}

/**
 * writeback_acquire: attempt to get exclusive writeback access to a device
 * @bdi: the device's backing_dev_info structure
 *
 * It is a waste of resources to have more than one pdflush thread blocked on
 * a single request queue.  Exclusion at the request_queue level is obtained
 * via a flag in the request_queue's backing_dev_info.state.
 *
 * Non-request_queue-backed address_spaces will share default_backing_dev_info,
 * unless they implement their own.  Which is somewhat inefficient, as this
 * may prevent concurrent writeback against multiple devices.
 */
int writeback_acquire(struct backing_dev_info *bdi)
{
	return !test_and_set_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_in_progress: determine whether there is writeback in progress
 *                        against a backing device.
 * @bdi: the device's backing_dev_info structure.
 */
int writeback_in_progress(struct backing_dev_info *bdi)
{
	return test_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_release: relinquish exclusive writeback access against a device.
 * @bdi: the device's backing_dev_info structure
 */
void writeback_release(struct backing_dev_info *bdi)
{
	BUG_ON(!writeback_in_progress(bdi));
	clear_bit(BDI_pdflush, &bdi->state);
}
