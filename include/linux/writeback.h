/*
 * include/linux/writeback.h.
 *
 * These declarations are private to fs/ and mm/.
 * Declarations which are exported to filesystems do not
 * get placed here.
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

extern spinlock_t inode_lock;
extern struct list_head inode_in_use;
extern struct list_head inode_unused;

/*
 * fs/fs-writeback.c
 */
#define WB_SYNC_NONE	0	/* Don't wait on anything */
#define WB_SYNC_LAST	1	/* Wait on the last-written mapping */
#define WB_SYNC_ALL	2	/* Wait on every mapping */

void try_to_writeback_unused_inodes(unsigned long pexclusive);
void writeback_single_inode(struct inode *inode,
				int sync, int *nr_to_write);
void writeback_unlocked_inodes(int *nr_to_write, int sync_mode,
				unsigned long *older_than_this);
void writeback_inodes_sb(struct super_block *);
void __wait_on_inode(struct inode * inode);
void sync_inodes(void);

static inline void wait_on_inode(struct inode *inode)
{
	if (inode->i_state & I_LOCK)
		__wait_on_inode(inode);
}

/*
 * mm/page-writeback.c
 */
/*
 * How much data to write out at a time in various places.  This isn't
 * really very important - it's just here to prevent any thread from
 * locking an inode for too long and blocking other threads which wish
 * to write the same file for allocation throttling purposes.
 */
#define WRITEOUT_PAGES	((4096 * 1024) / PAGE_CACHE_SIZE)

void balance_dirty_pages(struct address_space *mapping);
void balance_dirty_pages_ratelimited(struct address_space *mapping);
int pdflush_flush(unsigned long nr_pages);
int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0);

#endif		/* WRITEBACK_H */
