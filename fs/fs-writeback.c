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
		inode->i_state |= flags;

		/*
		 * If the inode is locked, just update its dirty state. 
		 * The unlocker will place the inode on the appropriate
		 * superblock list, based upon its state.
		 */
		if (inode->i_state & I_LOCK)
			goto same_list;

		/*
		 * Only add valid (hashed) inode to the superblock's
		 * dirty list.  Add blockdev inodes as well.
		 */
		if (list_empty(&inode->i_hash) && !S_ISBLK(inode->i_mode))
			goto same_list;
		if (inode->i_mapping->dirtied_when == 0)
			inode->i_mapping->dirtied_when = jiffies;
		list_del(&inode->i_list);
		list_add(&inode->i_list, &sb->s_dirty);
	}
same_list:
	spin_unlock(&inode_lock);
}

static inline void write_inode(struct inode *inode, int sync)
{
	if (inode->i_sb->s_op && inode->i_sb->s_op->write_inode &&
			!is_bad_inode(inode))
		inode->i_sb->s_op->write_inode(inode, sync);
}

/*
 * Write a single inode's dirty pages and inode data out to disk.
 * If `sync' is set, wait on the writeout.
 * If `nr_to_write' is not NULL, subtract the number of written pages
 * from *nr_to_write.
 *
 * Normally it is not legal for a single process to lock more than one
 * page at a time, due to ab/ba deadlock problems.  But writeback_mapping()
 * does want to lock a large number of pages, without immediately submitting
 * I/O against them (starting I/O is a "deferred unlock_page").
 *
 * However it *is* legal to lock multiple pages, if this is only ever performed
 * by a single process.  We provide that exclusion via locking in the
 * filesystem's ->writeback_mapping a_op. This ensures that only a single
 * process is locking multiple pages against this inode.  And as I/O is
 * submitted against all those locked pages, there is no deadlock.
 *
 * Called under inode_lock.
 */
static void __sync_single_inode(struct inode *inode, int wait, int *nr_to_write)
{
	unsigned dirty;
	struct address_space *mapping = inode->i_mapping;

	list_del(&inode->i_list);
	list_add(&inode->i_list, &inode->i_sb->s_locked_inodes);

	if (inode->i_state & I_LOCK)
		BUG();

	/* Set I_LOCK, reset I_DIRTY */
	dirty = inode->i_state & I_DIRTY;
	inode->i_state |= I_LOCK;
	inode->i_state &= ~I_DIRTY;
	spin_unlock(&inode_lock);

	if (wait)
		filemap_fdatawait(mapping);

	if (mapping->a_ops->writeback_mapping)
		mapping->a_ops->writeback_mapping(mapping, nr_to_write);
	else
		generic_writeback_mapping(mapping, NULL);

	/* Don't write the inode if only I_DIRTY_PAGES was set */
	if (dirty & (I_DIRTY_SYNC | I_DIRTY_DATASYNC))
		write_inode(inode, wait);

	if (wait)
		filemap_fdatawait(mapping);

	/*
	 * For non-blocking writeout (wait == 0), we still
	 * count the inode as being clean.
	 */
	spin_lock(&inode_lock);

	/*
	 * Did we write back all the pages?
	 */
	if (nr_to_write && *nr_to_write == 0) {
		/*
		 * Maybe not
		 */
		if (!list_empty(&mapping->dirty_pages))	/* No lock needed */
			inode->i_state |= I_DIRTY_PAGES;
	}

	inode->i_state &= ~I_LOCK;
	if (!(inode->i_state & I_FREEING)) {
		struct list_head *to;
		if (inode->i_state & I_DIRTY)
			to = &inode->i_sb->s_dirty;
		else if (atomic_read(&inode->i_count))
			to = &inode_in_use;
		else
			to = &inode_unused;
		list_del(&inode->i_list);
		list_add(&inode->i_list, to);
	}
	wake_up(&inode->i_wait);
}

/*
 * Write out an inode's dirty pages.  Called under inode_lock.
 */
static void
__writeback_single_inode(struct inode *inode, int sync, int *nr_to_write)
{
	while (inode->i_state & I_LOCK) {
		__iget(inode);
		spin_unlock(&inode_lock);
		__wait_on_inode(inode);
		iput(inode);
		spin_lock(&inode_lock);
	}
	__sync_single_inode(inode, sync, nr_to_write);
}

void writeback_single_inode(struct inode *inode, int sync, int *nr_to_write)
{
	spin_lock(&inode_lock);
	__writeback_single_inode(inode, sync, nr_to_write);
	spin_unlock(&inode_lock);
}

/*
 * Write out a list of inodes' pages, and the inode itself.
 *
 * If `sync' is true, wait on writeout of the last mapping
 * which we write.
 *
 * If older_than_this is non-NULL, then only write out mappings which
 * had their first dirtying at a time earlier than *older_than_this.
 *
 * Called under inode_lock.
 *
 * FIXME: putting all the inodes on a local list could introduce a
 * race with umount.  Bump the superblock refcount?
 */
static void __sync_list(struct list_head *head, int sync_mode,
		int *nr_to_write, unsigned long *older_than_this)
{
	struct list_head * tmp;
	LIST_HEAD(hold);	/* Unready inodes go here */

	while ((tmp = head->prev) != head) {
		struct inode *inode = list_entry(tmp, struct inode, i_list);
		struct address_space *mapping = inode->i_mapping;
		int really_sync;

		if (older_than_this && *older_than_this) {
			if (time_after(mapping->dirtied_when,
						*older_than_this)) {
				list_del(&inode->i_list);
				list_add(&inode->i_list, &hold);
				continue;
			}
		}
		really_sync = (sync_mode == WB_SYNC_ALL);
		if ((sync_mode == WB_SYNC_LAST) && (head->prev == head))
			really_sync = 1;
		__writeback_single_inode(inode, really_sync, nr_to_write);
		if (nr_to_write && *nr_to_write == 0)
			break;
	}
	/*
	 * Put the not-ready inodes back
	 */
	if (!list_empty(&hold))
		list_splice(&hold, head);
}

/*
 * Start writeback of dirty pagecache data against all unlocked inodes.
 *
 * Note:
 * We don't need to grab a reference to superblock here. If it has non-empty
 * ->s_dirty it's hadn't been killed yet and kill_super() won't proceed
 * past sync_inodes_sb() until both ->s_dirty and ->s_locked_inodes are
 * empty. Since __sync_single_inode() regains inode_lock before it finally moves
 * inode from superblock lists we are OK.
 *
 * If `older_than_this' is non-zero then only flush inodes which have a
 * flushtime older than *older_than_this.  Unless *older_than_this is
 * zero.  In which case we flush everything, like the old (dumb) wakeup_bdflush.
 */
void writeback_unlocked_inodes(int *nr_to_write, int sync_mode,
				unsigned long *older_than_this)
{
	struct super_block * sb;
	static unsigned short writeback_gen;

	spin_lock(&inode_lock);
	spin_lock(&sb_lock);

	/*
	 * We could get into livelock here if someone is dirtying
	 * inodes fast enough.  writeback_gen is used to avoid that.
	 */
	writeback_gen++;

	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		if (sb->s_writeback_gen == writeback_gen)
			continue;
		sb->s_writeback_gen = writeback_gen;

		if (current->flags & PF_FLUSHER) {
			if (sb->s_flags & MS_FLUSHING) {
				/*
				 * There's no point in two pdflush threads
				 * flushing the same device.  But for other
				 * callers, we want to perform the flush
				 * because the fdatasync is how we implement
				 * writer throttling.
				 */
				continue;
			}
			sb->s_flags |= MS_FLUSHING;
		}

		if (!list_empty(&sb->s_dirty)) {
			spin_unlock(&sb_lock);
			__sync_list(&sb->s_dirty, sync_mode,
					nr_to_write, older_than_this);
			spin_lock(&sb_lock);
		}
		if (current->flags & PF_FLUSHER)
			sb->s_flags &= ~MS_FLUSHING;
		if (nr_to_write && *nr_to_write == 0)
			break;
	}
	spin_unlock(&sb_lock);
	spin_unlock(&inode_lock);
}

/*
 * Called under inode_lock
 */
static int __try_to_writeback_unused_list(struct list_head *head, int nr_inodes)
{
	struct list_head *tmp = head;
	struct inode *inode;

	while (nr_inodes && (tmp = tmp->prev) != head) {
		inode = list_entry(tmp, struct inode, i_list);

		if (!atomic_read(&inode->i_count)) {
			__sync_single_inode(inode, 0, NULL);
			nr_inodes--;

			/* 
			 * __sync_single_inode moved the inode to another list,
			 * so we have to start looking from the list head.
			 */
			tmp = head;
		}
	}

	return nr_inodes;
}

static void __wait_on_locked(struct list_head *head)
{
	struct list_head * tmp;
	while ((tmp = head->prev) != head) {
		struct inode *inode = list_entry(tmp, struct inode, i_list);
		__iget(inode);
		spin_unlock(&inode_lock);
		__wait_on_inode(inode);
		iput(inode);
		spin_lock(&inode_lock);
	}
}

/*
 * writeback and wait upon the filesystem's dirty inodes.
 * We do it in two passes - one to write, and one to wait.
 */
void sync_inodes_sb(struct super_block *sb)
{
	spin_lock(&inode_lock);
	while (!list_empty(&sb->s_dirty)||!list_empty(&sb->s_locked_inodes)) {
		__sync_list(&sb->s_dirty, WB_SYNC_NONE, NULL, NULL);
		__sync_list(&sb->s_dirty, WB_SYNC_ALL, NULL, NULL);
		__wait_on_locked(&sb->s_locked_inodes);
	}
	spin_unlock(&inode_lock);
}

/*
 * writeback the dirty inodes for this filesystem
 */
void writeback_inodes_sb(struct super_block *sb)
{
	spin_lock(&inode_lock);
	while (!list_empty(&sb->s_dirty))
		__sync_list(&sb->s_dirty, WB_SYNC_NONE, NULL, NULL);
	spin_unlock(&inode_lock);
}

/*
 * Find a superblock with inodes that need to be synced
 */

static struct super_block *get_super_to_sync(void)
{
	struct list_head *p;
restart:
	spin_lock(&inode_lock);
	spin_lock(&sb_lock);
	list_for_each(p, &super_blocks) {
		struct super_block *s = list_entry(p,struct super_block,s_list);
		if (list_empty(&s->s_dirty) && list_empty(&s->s_locked_inodes))
			continue;
		s->s_count++;
		spin_unlock(&sb_lock);
		spin_unlock(&inode_lock);
		down_read(&s->s_umount);
		if (!s->s_root) {
			drop_super(s);
			goto restart;
		}
		return s;
	}
	spin_unlock(&sb_lock);
	spin_unlock(&inode_lock);
	return NULL;
}

/**
 *	sync_inodes
 *	@dev: device to sync the inodes from.
 *
 *	sync_inodes goes through the super block's dirty list, 
 *	writes them out, waits on the writeout and puts the inodes
 *	back on the normal list.
 */

void sync_inodes(void)
{
	struct super_block * s;
	/*
	 * Search the super_blocks array for the device(s) to sync.
	 */
	while ((s = get_super_to_sync()) != NULL) {
		sync_inodes_sb(s);
		drop_super(s);
	}
}

void try_to_writeback_unused_inodes(unsigned long pexclusive)
{
	struct super_block * sb;
	int nr_inodes = inodes_stat.nr_unused;

	spin_lock(&inode_lock);
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.next);
	for (; nr_inodes && sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.next)) {
		if (list_empty(&sb->s_dirty))
			continue;
		spin_unlock(&sb_lock);
		nr_inodes = __try_to_writeback_unused_list(&sb->s_dirty, nr_inodes);
		spin_lock(&sb_lock);
	}
	spin_unlock(&sb_lock);
	spin_unlock(&inode_lock);
	clear_bit(0, (unsigned long *)pexclusive);
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
	spin_lock(&inode_lock);
	__writeback_single_inode(inode, sync, NULL);
	spin_unlock(&inode_lock);
	if (sync)
		wait_on_inode(inode);
}

/**
 * generic_osync_inode - flush all dirty data for a given inode to disk
 * @inode: inode to write
 * @datasync: if set, don't bother flushing timestamps
 *
 * This can be called by file_write functions for files which have the
 * O_SYNC flag set, to flush dirty writes to disk.  
 */

int generic_osync_inode(struct inode *inode, int what)
{
	int err = 0, err2 = 0, need_write_inode_now = 0;
	
	/* 
	 * WARNING
	 *
	 * Currently, the filesystem write path does not pass the
	 * filp down to the low-level write functions.  Therefore it
	 * is impossible for (say) __block_commit_write to know if
	 * the operation is O_SYNC or not.
	 *
	 * Ideally, O_SYNC writes would have the filesystem call
	 * ll_rw_block as it went to kick-start the writes, and we
	 * could call osync_inode_buffers() here to wait only for
	 * those IOs which have already been submitted to the device
	 * driver layer.  As it stands, if we did this we'd not write
	 * anything to disk since our writes have not been queued by
	 * this point: they are still on the dirty LRU.
	 * 
	 * So, currently we will call fsync_inode_buffers() instead,
	 * to flush _all_ dirty buffers for this inode to disk on 
	 * every O_SYNC write, not just the synchronous I/Os.  --sct
	 */

	if (what & OSYNC_DATA)
		writeback_single_inode(inode, 0, NULL);
	if (what & (OSYNC_METADATA|OSYNC_DATA))
		err = fsync_inode_buffers(inode);
	if (what & OSYNC_DATA) {
		err2 = filemap_fdatawrite(inode->i_mapping);
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
