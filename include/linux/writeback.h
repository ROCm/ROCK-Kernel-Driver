/*
 * include/linux/writeback.h.
 *
 * These declarations are private to fs/ and mm/.
 * Declarations which are exported to filesystems do not
 * get placed here.
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

struct backing_dev_info;

extern spinlock_t inode_lock;
extern struct list_head inode_in_use;
extern struct list_head inode_unused;

/*
 * Yes, writeback.h requires sched.h
 * No, sched.h is not included from here.
 */
static inline int current_is_pdflush(void)
{
	return current->flags & PF_FLUSHER;
}

/*
 * fs/fs-writeback.c
 */
enum writeback_sync_modes {
	WB_SYNC_NONE =  0,	/* Don't wait on anything */
	WB_SYNC_LAST =  1,	/* Wait on the last-written mapping */
	WB_SYNC_ALL =   2,	/* Wait on every mapping */
	WB_SYNC_HOLD =  3,	/* Hold the inode on sb_dirty for sys_sync() */
};

void writeback_unlocked_inodes(int *nr_to_write,
			       enum writeback_sync_modes sync_mode,
			       unsigned long *older_than_this);
void wake_up_inode(struct inode *inode);
void __wait_on_inode(struct inode * inode);
void sync_inodes_sb(struct super_block *, int wait);
void sync_inodes(int wait);
void writeback_backing_dev(struct backing_dev_info *bdi, int *nr_to_write,
			enum writeback_sync_modes sync_mode,
			unsigned long *older_than_this);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	if (inode->i_state & I_LOCK)
		__wait_on_inode(inode);
}

/*
 * mm/page-writeback.c
 */
/* These 5 are exported to sysctl. */
extern int dirty_background_ratio;
extern int dirty_async_ratio;
extern int dirty_sync_ratio;
extern int dirty_writeback_centisecs;
extern int dirty_expire_centisecs;


void balance_dirty_pages(struct address_space *mapping);
void balance_dirty_pages_ratelimited(struct address_space *mapping);
int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0);
int do_writepages(struct address_space *mapping, int *nr_to_write);

/* pdflush.c */
extern int nr_pdflush_threads;	/* Global so it can be exported to sysctl
				   read-only. */


#endif		/* WRITEBACK_H */
