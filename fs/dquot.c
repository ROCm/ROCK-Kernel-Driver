/*
 * Implementation of the diskquota system for the LINUX operating
 * system. QUOTA is implemented using the BSD system call interface as
 * the means of communication with the user level. Currently only the
 * ext2 filesystem has support for disk quotas. Other filesystems may
 * be added in the future. This file contains the generic routines
 * called by the different filesystems on allocation of an inode or
 * block. These routines take care of the administration needed to
 * have a consistent diskquota tracking system. The ideas of both
 * user and group quotas are based on the Melbourne quota system as
 * used on BSD derived systems. The internal implementation is 
 * based on one of the several variants of the LINUX inode-subsystem
 * with added complexity of the diskquota system.
 * 
 * Version: $Id: dquot.c,v 6.3 1996/11/17 18:35:34 mvw Exp mvw $
 * 
 * Author:	Marco van Wieringen <mvw@planets.elm.net>
 *
 * Fixes:   Dmitry Gorodchanin <pgmdsg@ibi.com>, 11 Feb 96
 *
 *		Revised list management to avoid races
 *		-- Bill Hawes, <whawes@star.net>, 9/98
 *
 *		Fixed races in dquot_transfer(), dqget() and dquot_alloc_...().
 *		As the consequence the locking was moved from dquot_decr_...(),
 *		dquot_incr_...() to calling functions.
 *		invalidate_dquots() now writes modified dquots.
 *		Serialized quota_off() and quota_on() for mount point.
 *		Fixed a few bugs in grow_dquots().
 *		Fixed deadlock in write_dquot() - we no longer account quotas on
 *		quota files
 *		remove_dquot_ref() moved to inode.c - it now traverses through inodes
 *		add_dquot_ref() restarts after blocking
 *		Added check for bogus uid and fixed check for group in quotactl.
 *		Jan Kara, <jack@suse.cz>, sponsored by SuSE CR, 10-11/99
 *
 *		Used struct list_head instead of own list struct
 *		Invalidation of dquots with dq_count > 0 no longer possible
 *		Improved free_dquots list management
 *		Quota and i_blocks are now updated in one place to avoid races
 *		Warnings are now delayed so we won't block in critical section
 *		Write updated not to require dquot lock
 *		Jan Kara, <jack@suse.cz>, 9/2000
 *
 *		Added dynamic quota structure allocation
 *		Jan Kara <jack@suse.cz> 12/2000
 *
 * (C) Copyright 1994 - 1997 Marco van Wieringen 
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/uaccess.h>

#define __DQUOT_VERSION__	"dquot_6.4.0"

int nr_dquots, nr_free_dquots;

static char *quotatypes[] = INITQFNAMES;

static inline struct quota_mount_options *sb_dqopt(struct super_block *sb)
{
	return &sb->s_dquot;
}

/*
 * Dquot List Management:
 * The quota code uses three lists for dquot management: the inuse_list,
 * free_dquots, and dquot_hash[] array. A single dquot structure may be
 * on all three lists, depending on its current state.
 *
 * All dquots are placed to the end of inuse_list when first created, and this
 * list is used for the sync and invalidate operations, which must look
 * at every dquot.
 *
 * Unused dquots (dq_count == 0) are added to the free_dquots list when
 * freed, and this list is searched whenever we need an available dquot.
 * Dquots are removed from the list as soon as they are used again, and
 * nr_free_dquots gives the number of dquots on the list. When dquot is
 * invalidated it's completely released from memory.
 *
 * Dquots with a specific identity (device, type and id) are placed on
 * one of the dquot_hash[] hash chains. The provides an efficient search
 * mechanism to locate a specific dquot.
 */

/*
 * Note that any operation which operates on dquot data (ie. dq_dqb) mustn't
 * block while it's updating/reading it. Otherwise races would occur.
 *
 * Locked dquots might not be referenced in inodes - operations like
 * add_dquot_space() does dqduplicate() and would complain. Currently
 * dquot it locked only once in its existence - when it's being read
 * to memory on first dqget() and at that time it can't be referenced
 * from inode. Write operations on dquots don't hold dquot lock as they
 * copy data to internal buffers before writing anyway and copying as well
 * as any data update should be atomic. Also nobody can change used
 * entries in dquot structure as this is done only when quota is destroyed
 * and invalidate_dquots() waits for dquot to have dq_count == 0.
 */

static LIST_HEAD(inuse_list);
static LIST_HEAD(free_dquots);
static struct list_head dquot_hash[NR_DQHASH];

static struct dqstats dqstats;

static void dqput(struct dquot *);
static struct dquot *dqduplicate(struct dquot *);

static inline char is_enabled(struct quota_mount_options *dqopt, short type)
{
	switch (type) {
		case USRQUOTA:
			return((dqopt->flags & DQUOT_USR_ENABLED) != 0);
		case GRPQUOTA:
			return((dqopt->flags & DQUOT_GRP_ENABLED) != 0);
	}
	return(0);
}

static inline char sb_has_quota_enabled(struct super_block *sb, short type)
{
	return is_enabled(sb_dqopt(sb), type);
}

static inline int const hashfn(kdev_t dev, unsigned int id, short type)
{
	return((HASHDEV(dev) ^ id) * (MAXQUOTAS - type)) % NR_DQHASH;
}

static inline void insert_dquot_hash(struct dquot *dquot)
{
	struct list_head *head = dquot_hash + hashfn(dquot->dq_dev, dquot->dq_id, dquot->dq_type);
	list_add(&dquot->dq_hash, head);
}

static inline void remove_dquot_hash(struct dquot *dquot)
{
	list_del(&dquot->dq_hash);
	INIT_LIST_HEAD(&dquot->dq_hash);
}

static inline struct dquot *find_dquot(unsigned int hashent, kdev_t dev, unsigned int id, short type)
{
	struct list_head *head;
	struct dquot *dquot;

	for (head = dquot_hash[hashent].next; head != dquot_hash+hashent; head = head->next) {
		dquot = list_entry(head, struct dquot, dq_hash);
		if (dquot->dq_dev == dev && dquot->dq_id == id && dquot->dq_type == type)
			return dquot;
	}
	return NODQUOT;
}

/* Add a dquot to the head of the free list */
static inline void put_dquot_head(struct dquot *dquot)
{
	list_add(&dquot->dq_free, &free_dquots);
	nr_free_dquots++;
}

/* Add a dquot to the tail of the free list */
static inline void put_dquot_last(struct dquot *dquot)
{
	list_add(&dquot->dq_free, free_dquots.prev);
	nr_free_dquots++;
}

/* Move dquot to the head of free list (it must be already on it) */
static inline void move_dquot_head(struct dquot *dquot)
{
	list_del(&dquot->dq_free);
	list_add(&dquot->dq_free, &free_dquots);
}

static inline void remove_free_dquot(struct dquot *dquot)
{
	if (list_empty(&dquot->dq_free))
		return;
	list_del(&dquot->dq_free);
	INIT_LIST_HEAD(&dquot->dq_free);
	nr_free_dquots--;
}

static inline void put_inuse(struct dquot *dquot)
{
	/* We add to the back of inuse list so we don't have to restart
	 * when traversing this list and we block */
	list_add(&dquot->dq_inuse, inuse_list.prev);
	nr_dquots++;
}

static inline void remove_inuse(struct dquot *dquot)
{
	nr_dquots--;
	list_del(&dquot->dq_inuse);
}

static void __wait_on_dquot(struct dquot *dquot)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&dquot->dq_wait_lock, &wait);
repeat:
	set_current_state(TASK_UNINTERRUPTIBLE);
	if (dquot->dq_flags & DQ_LOCKED) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&dquot->dq_wait_lock, &wait);
	current->state = TASK_RUNNING;
}

static inline void wait_on_dquot(struct dquot *dquot)
{
	if (dquot->dq_flags & DQ_LOCKED)
		__wait_on_dquot(dquot);
}

static inline void lock_dquot(struct dquot *dquot)
{
	wait_on_dquot(dquot);
	dquot->dq_flags |= DQ_LOCKED;
}

static inline void unlock_dquot(struct dquot *dquot)
{
	dquot->dq_flags &= ~DQ_LOCKED;
	wake_up(&dquot->dq_wait_lock);
}

static void __wait_dquot_unused(struct dquot *dquot)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&dquot->dq_wait_free, &wait);
repeat:
	set_current_state(TASK_UNINTERRUPTIBLE);
	if (dquot->dq_count) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&dquot->dq_wait_free, &wait);
	current->state = TASK_RUNNING;
}

/*
 *	We don't have to be afraid of deadlocks as we never have quotas on quota files...
 */
static void write_dquot(struct dquot *dquot)
{
	short type = dquot->dq_type;
	struct file *filp;
	mm_segment_t fs;
	loff_t offset;
	ssize_t ret;
	struct semaphore *sem = &dquot->dq_sb->s_dquot.dqio_sem;
	struct dqblk dqbuf;

	down(sem);
	filp = dquot->dq_sb->s_dquot.files[type];
	offset = dqoff(dquot->dq_id);
	fs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 * Note: clear the DQ_MOD flag unconditionally,
	 * so we don't loop forever on failure.
	 */
	memcpy(&dqbuf, &dquot->dq_dqb, sizeof(struct dqblk));
	dquot->dq_flags &= ~DQ_MOD;
	ret = 0;
	if (filp)
		ret = filp->f_op->write(filp, (char *)&dqbuf, 
					sizeof(struct dqblk), &offset);
	if (ret != sizeof(struct dqblk))
		printk(KERN_WARNING "VFS: dquota write failed on dev %s\n",
			kdevname(dquot->dq_dev));

	set_fs(fs);
	up(sem);
	dqstats.writes++;
}

static void read_dquot(struct dquot *dquot)
{
	short type = dquot->dq_type;
	struct file *filp;
	mm_segment_t fs;
	loff_t offset;

	filp = dquot->dq_sb->s_dquot.files[type];
	if (filp == (struct file *)NULL)
		return;

	lock_dquot(dquot);
	if (!dquot->dq_sb)	/* Invalidated quota? */
		goto out_lock;
	/* Now we are sure filp is valid - the dquot isn't invalidated */
	down(&dquot->dq_sb->s_dquot.dqio_sem);
	offset = dqoff(dquot->dq_id);
	fs = get_fs();
	set_fs(KERNEL_DS);
	filp->f_op->read(filp, (char *)&dquot->dq_dqb, sizeof(struct dqblk), &offset);
	up(&dquot->dq_sb->s_dquot.dqio_sem);
	set_fs(fs);

	if (dquot->dq_bhardlimit == 0 && dquot->dq_bsoftlimit == 0 &&
	    dquot->dq_ihardlimit == 0 && dquot->dq_isoftlimit == 0)
		dquot->dq_flags |= DQ_FAKE;
	dqstats.reads++;
out_lock:
	unlock_dquot(dquot);
}

/* Invalidate all dquots on the list, wait for all users. Note that this function is called
 * after quota is disabled so no new quota might be created. As we only insert to the end of
 * inuse list, we don't have to restart searching... */
static void invalidate_dquots(struct super_block *sb, short type)
{
	struct dquot *dquot;
	struct list_head *head;

restart:
	for (head = inuse_list.next; head != &inuse_list; head = head->next) {
		dquot = list_entry(head, struct dquot, dq_inuse);
		if (dquot->dq_sb != sb)
			continue;
		if (dquot->dq_type != type)
			continue;
		dquot->dq_flags |= DQ_INVAL;
		if (dquot->dq_count)
			/*
			 *  Wait for any users of quota. As we have already cleared the flags in
			 *  superblock and cleared all pointers from inodes we are assured
			 *  that there will be no new users of this quota.
			 */
			__wait_dquot_unused(dquot);
		/* Quota now have no users and it has been written on last dqput() */
		remove_dquot_hash(dquot);
		remove_free_dquot(dquot);
		remove_inuse(dquot);
		kmem_cache_free(dquot_cachep, dquot);
		goto restart;
	}
}

int sync_dquots(kdev_t dev, short type)
{
	struct list_head *head;
	struct dquot *dquot;

	lock_kernel();
restart:
	for (head = inuse_list.next; head != &inuse_list; head = head->next) {
		dquot = list_entry(head, struct dquot, dq_inuse);
		if (dev && dquot->dq_dev != dev)
			continue;
                if (type != -1 && dquot->dq_type != type)
			continue;
		if (!dquot->dq_sb)	/* Invalidated? */
			continue;
		if (!(dquot->dq_flags & (DQ_MOD | DQ_LOCKED)))
			continue;
		/* Raise use count so quota won't be invalidated. We can't use dqduplicate() as it does too many tests */
		dquot->dq_count++;
		if (dquot->dq_flags & DQ_LOCKED)
			wait_on_dquot(dquot);
		if (dquot->dq_flags & DQ_MOD)
			write_dquot(dquot);
		dqput(dquot);
		goto restart;
	}
	dqstats.syncs++;
	unlock_kernel();
	return 0;
}

/* Free unused dquots from cache */
static void prune_dqcache(int count)
{
	struct list_head *head;
	struct dquot *dquot;

	head = free_dquots.prev;
	while (head != &free_dquots && count) {
		dquot = list_entry(head, struct dquot, dq_free);
		remove_dquot_hash(dquot);
		remove_free_dquot(dquot);
		remove_inuse(dquot);
		kmem_cache_free(dquot_cachep, dquot);
		count--;
		head = free_dquots.prev;
	}
}

int shrink_dqcache_memory(int priority, unsigned int gfp_mask)
{
	lock_kernel();
	prune_dqcache(nr_free_dquots / (priority + 1));
	unlock_kernel();
	kmem_cache_shrink(dquot_cachep);
	return 0;
}

/* NOTE: If you change this function please check whether dqput_blocks() works right... */
static void dqput(struct dquot *dquot)
{
	if (!dquot)
		return;
	if (!dquot->dq_count) {
		printk("VFS: dqput: trying to free free dquot\n");
		printk("VFS: device %s, dquot of %s %d\n",
			kdevname(dquot->dq_dev), quotatypes[dquot->dq_type],
			dquot->dq_id);
		return;
	}

	dqstats.drops++;
we_slept:
	if (dquot->dq_count > 1) {
		/* We have more than one user... We can simply decrement use count */
		dquot->dq_count--;
		return;
	}
	if (dquot->dq_flags & DQ_MOD) {
		write_dquot(dquot);
		goto we_slept;
	}

	/* sanity check */
	if (!list_empty(&dquot->dq_free)) {
		printk(KERN_ERR "dqput: dquot already on free list??\n");
		dquot->dq_count--;	/* J.K. Just decrementing use count seems safer... */
		return;
	}
	dquot->dq_count--;
	/* If dquot is going to be invalidated invalidate_dquots() is going to free it so */
	if (!(dquot->dq_flags & DQ_INVAL))
		put_dquot_last(dquot);	/* Place at end of LRU free queue */
	wake_up(&dquot->dq_wait_free);
}

static struct dquot *get_empty_dquot(void)
{
	struct dquot *dquot;

	dquot = kmem_cache_alloc(dquot_cachep, SLAB_KERNEL);
	if(!dquot)
		return NODQUOT;

	memset((caddr_t)dquot, 0, sizeof(struct dquot));
	init_waitqueue_head(&dquot->dq_wait_free);
	init_waitqueue_head(&dquot->dq_wait_lock);
	INIT_LIST_HEAD(&dquot->dq_free);
	INIT_LIST_HEAD(&dquot->dq_inuse);
	INIT_LIST_HEAD(&dquot->dq_hash);
	dquot->dq_count = 1;
	/* all dquots go on the inuse_list */
	put_inuse(dquot);

	return dquot;
}

static struct dquot *dqget(struct super_block *sb, unsigned int id, short type)
{
	unsigned int hashent = hashfn(sb->s_dev, id, type);
	struct dquot *dquot, *empty = NODQUOT;
	struct quota_mount_options *dqopt = sb_dqopt(sb);

we_slept:
        if (!is_enabled(dqopt, type)) {
		if (empty)
			dqput(empty);
                return NODQUOT;
	}

	if ((dquot = find_dquot(hashent, sb->s_dev, id, type)) == NODQUOT) {
		if (empty == NODQUOT) {
			if ((empty = get_empty_dquot()) == NODQUOT)
				schedule();	/* Try to wait for a moment... */
			goto we_slept;
		}
		dquot = empty;
        	dquot->dq_id = id;
        	dquot->dq_type = type;
        	dquot->dq_dev = sb->s_dev;
        	dquot->dq_sb = sb;
		/* hash it first so it can be found */
		insert_dquot_hash(dquot);
        	read_dquot(dquot);
	} else {
		if (!dquot->dq_count++)
			remove_free_dquot(dquot);
		dqstats.cache_hits++;
		wait_on_dquot(dquot);
		if (empty)
			dqput(empty);
	}

	if (!dquot->dq_sb) {	/* Has somebody invalidated entry under us? */
		printk(KERN_ERR "VFS: dqget(): Quota invalidated in dqget()!\n");
		dqput(dquot);
		return NODQUOT;
	}
	dquot->dq_referenced++;
	dqstats.lookups++;

	return dquot;
}

static struct dquot *dqduplicate(struct dquot *dquot)
{
	if (dquot == NODQUOT)
		return NODQUOT;
	dquot->dq_count++;
	if (!dquot->dq_sb) {
		printk(KERN_ERR "VFS: dqduplicate(): Invalidated quota to be duplicated!\n");
		dquot->dq_count--;
		return NODQUOT;
	}
	if (dquot->dq_flags & DQ_LOCKED)
		printk(KERN_ERR "VFS: dqduplicate(): Locked quota to be duplicated!\n");
	dquot->dq_referenced++;
	dqstats.lookups++;
	return dquot;
}

static int dqinit_needed(struct inode *inode, short type)
{
	int cnt;

	if (IS_NOQUOTA(inode))
		return 0;
	if (type != -1)
		return inode->i_dquot[type] == NODQUOT;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (inode->i_dquot[cnt] == NODQUOT)
			return 1;
	return 0;
}

static void add_dquot_ref(struct super_block *sb, short type)
{
	struct list_head *p;

	if (!sb->dq_op)
		return;	/* nothing to do */

restart:
	file_list_lock();
	for (p = sb->s_files.next; p != &sb->s_files; p = p->next) {
		struct file *filp = list_entry(p, struct file, f_list);
		struct inode *inode = filp->f_dentry->d_inode;
		if (filp->f_mode & FMODE_WRITE && dqinit_needed(inode, type)) {
			struct vfsmount *mnt = mntget(filp->f_vfsmnt);
			struct dentry *dentry = dget(filp->f_dentry);
			file_list_unlock();
			sb->dq_op->initialize(inode, type);
			dput(dentry);
			mntput(mnt);
			/* As we may have blocked we had better restart... */
			goto restart;
		}
	}
	file_list_unlock();
}

/* Return 0 if dqput() won't block (note that 1 doesn't necessarily mean blocking) */
static inline int dqput_blocks(struct dquot *dquot)
{
	if (dquot->dq_count == 1)
		return 1;
	return 0;
}

/* Remove references to dquots from inode - add dquot to list for freeing if needed */
int remove_inode_dquot_ref(struct inode *inode, short type, struct list_head *tofree_head)
{
	struct dquot *dquot = inode->i_dquot[type];
	int cnt;

	inode->i_dquot[type] = NODQUOT;
	/* any other quota in use? */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] != NODQUOT)
			goto put_it;
	}
	inode->i_flags &= ~S_QUOTA;
put_it:
	if (dquot != NODQUOT) {
		if (dqput_blocks(dquot)) {
			if (dquot->dq_count != 1)
				printk(KERN_WARNING "VFS: Adding dquot with dq_count %d to dispose list.\n", dquot->dq_count);
			list_add(&dquot->dq_free, tofree_head);	/* As dquot must have currently users it can't be on the free list... */
			return 1;
		}
		else
			dqput(dquot);   /* We have guaranteed we won't block */
	}
	return 0;
}

/* Free list of dquots - called from inode.c */
void put_dquot_list(struct list_head *tofree_head)
{
	struct list_head *act_head;
	struct dquot *dquot;

	lock_kernel();
	act_head = tofree_head->next;
	/* So now we have dquots on the list... Just free them */
	while (act_head != tofree_head) {
		dquot = list_entry(act_head, struct dquot, dq_free);
		act_head = act_head->next;
		list_del(&dquot->dq_free);	/* Remove dquot from the list so we won't have problems... */
		INIT_LIST_HEAD(&dquot->dq_free);
		dqput(dquot);
	}
	unlock_kernel();
}

static inline void dquot_incr_inodes(struct dquot *dquot, unsigned long number)
{
	dquot->dq_curinodes += number;
	dquot->dq_flags |= DQ_MOD;
}

static inline void dquot_incr_blocks(struct dquot *dquot, unsigned long number)
{
	dquot->dq_curblocks += number;
	dquot->dq_flags |= DQ_MOD;
}

static inline void dquot_decr_inodes(struct dquot *dquot, unsigned long number)
{
	if (dquot->dq_curinodes > number)
		dquot->dq_curinodes -= number;
	else
		dquot->dq_curinodes = 0;
	if (dquot->dq_curinodes < dquot->dq_isoftlimit)
		dquot->dq_itime = (time_t) 0;
	dquot->dq_flags &= ~DQ_INODES;
	dquot->dq_flags |= DQ_MOD;
}

static inline void dquot_decr_blocks(struct dquot *dquot, unsigned long number)
{
	if (dquot->dq_curblocks > number)
		dquot->dq_curblocks -= number;
	else
		dquot->dq_curblocks = 0;
	if (dquot->dq_curblocks < dquot->dq_bsoftlimit)
		dquot->dq_btime = (time_t) 0;
	dquot->dq_flags &= ~DQ_BLKS;
	dquot->dq_flags |= DQ_MOD;
}

static inline int need_print_warning(struct dquot *dquot, int flag)
{
	switch (dquot->dq_type) {
		case USRQUOTA:
			return current->fsuid == dquot->dq_id && !(dquot->dq_flags & flag);
		case GRPQUOTA:
			return in_group_p(dquot->dq_id) && !(dquot->dq_flags & flag);
	}
	return 0;
}

/* Values of warnings */
#define NOWARN 0
#define IHARDWARN 1
#define ISOFTLONGWARN 2
#define ISOFTWARN 3
#define BHARDWARN 4
#define BSOFTLONGWARN 5
#define BSOFTWARN 6

/* Print warning to user which exceeded quota */
static void print_warning(struct dquot *dquot, const char warntype)
{
	char *msg = NULL;
	int flag = (warntype == BHARDWARN || warntype == BSOFTLONGWARN) ? DQ_BLKS :
	  ((warntype == IHARDWARN || warntype == ISOFTLONGWARN) ? DQ_INODES : 0);

	if (!need_print_warning(dquot, flag))
		return;
	dquot->dq_flags |= flag;
	tty_write_message(current->tty, (char *)bdevname(dquot->dq_sb->s_dev));
	if (warntype == ISOFTWARN || warntype == BSOFTWARN)
		tty_write_message(current->tty, ": warning, ");
	else
		tty_write_message(current->tty, ": write failed, ");
	tty_write_message(current->tty, quotatypes[dquot->dq_type]);
	switch (warntype) {
		case IHARDWARN:
			msg = " file limit reached.\n";
			break;
		case ISOFTLONGWARN:
			msg = " file quota exceeded too long.\n";
			break;
		case ISOFTWARN:
			msg = " file quota exceeded.\n";
			break;
		case BHARDWARN:
			msg = " block limit reached.\n";
			break;
		case BSOFTLONGWARN:
			msg = " block quota exceeded too long.\n";
			break;
		case BSOFTWARN:
			msg = " block quota exceeded.\n";
			break;
	}
	tty_write_message(current->tty, msg);
}

static inline void flush_warnings(struct dquot **dquots, char *warntype)
{
	int i;

	for (i = 0; i < MAXQUOTAS; i++)
		if (dquots[i] != NODQUOT && warntype[i] != NOWARN)
			print_warning(dquots[i], warntype[i]);
}

static inline char ignore_hardlimit(struct dquot *dquot)
{
	return capable(CAP_SYS_RESOURCE) && !dquot->dq_sb->s_dquot.rsquash[dquot->dq_type];
}

static int check_idq(struct dquot *dquot, ulong inodes, char *warntype)
{
	*warntype = NOWARN;
	if (inodes <= 0 || dquot->dq_flags & DQ_FAKE)
		return QUOTA_OK;

	if (dquot->dq_ihardlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_ihardlimit &&
            !ignore_hardlimit(dquot)) {
		*warntype = IHARDWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime && CURRENT_TIME >= dquot->dq_itime &&
            !ignore_hardlimit(dquot)) {
		*warntype = ISOFTLONGWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime == 0) {
		*warntype = ISOFTWARN;
		dquot->dq_itime = CURRENT_TIME + dquot->dq_sb->s_dquot.inode_expire[dquot->dq_type];
	}

	return QUOTA_OK;
}

static int check_bdq(struct dquot *dquot, ulong blocks, char prealloc, char *warntype)
{
	*warntype = 0;
	if (blocks <= 0 || dquot->dq_flags & DQ_FAKE)
		return QUOTA_OK;

	if (dquot->dq_bhardlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bhardlimit &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			*warntype = BHARDWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime && CURRENT_TIME >= dquot->dq_btime &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			*warntype = BSOFTLONGWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime == 0) {
		if (!prealloc) {
			*warntype = BSOFTWARN;
			dquot->dq_btime = CURRENT_TIME + dquot->dq_sb->s_dquot.block_expire[dquot->dq_type];
		}
		else
			/*
			 * We don't allow preallocation to exceed softlimit so exceeding will
			 * be always printed
			 */
			return NO_QUOTA;
	}

	return QUOTA_OK;
}

/*
 * Initialize a dquot-struct with new quota info. This is used by the
 * system call interface functions.
 */ 
static int set_dqblk(struct super_block *sb, int id, short type, int flags, struct dqblk *dqblk)
{
	struct dquot *dquot;
	int error = -EFAULT;
	struct dqblk dq_dqblk;

	if (copy_from_user(&dq_dqblk, dqblk, sizeof(struct dqblk)))
		return error;

	if (sb && (dquot = dqget(sb, id, type)) != NODQUOT) {
		/* We can't block while changing quota structure... */
		if (id > 0 && ((flags & SET_QUOTA) || (flags & SET_QLIMIT))) {
			dquot->dq_bhardlimit = dq_dqblk.dqb_bhardlimit;
			dquot->dq_bsoftlimit = dq_dqblk.dqb_bsoftlimit;
			dquot->dq_ihardlimit = dq_dqblk.dqb_ihardlimit;
			dquot->dq_isoftlimit = dq_dqblk.dqb_isoftlimit;
		}

		if ((flags & SET_QUOTA) || (flags & SET_USE)) {
			if (dquot->dq_isoftlimit &&
			    dquot->dq_curinodes < dquot->dq_isoftlimit &&
			    dq_dqblk.dqb_curinodes >= dquot->dq_isoftlimit)
				dquot->dq_itime = CURRENT_TIME + dquot->dq_sb->s_dquot.inode_expire[type];
			dquot->dq_curinodes = dq_dqblk.dqb_curinodes;
			if (dquot->dq_curinodes < dquot->dq_isoftlimit)
				dquot->dq_flags &= ~DQ_INODES;
			if (dquot->dq_bsoftlimit &&
			    dquot->dq_curblocks < dquot->dq_bsoftlimit &&
			    dq_dqblk.dqb_curblocks >= dquot->dq_bsoftlimit)
				dquot->dq_btime = CURRENT_TIME + dquot->dq_sb->s_dquot.block_expire[type];
			dquot->dq_curblocks = dq_dqblk.dqb_curblocks;
			if (dquot->dq_curblocks < dquot->dq_bsoftlimit)
				dquot->dq_flags &= ~DQ_BLKS;
		}

		if (id == 0) {
			dquot->dq_sb->s_dquot.block_expire[type] = dquot->dq_btime = dq_dqblk.dqb_btime;
			dquot->dq_sb->s_dquot.inode_expire[type] = dquot->dq_itime = dq_dqblk.dqb_itime;
		}

		if (dq_dqblk.dqb_bhardlimit == 0 && dq_dqblk.dqb_bsoftlimit == 0 &&
		    dq_dqblk.dqb_ihardlimit == 0 && dq_dqblk.dqb_isoftlimit == 0)
			dquot->dq_flags |= DQ_FAKE;
		else
			dquot->dq_flags &= ~DQ_FAKE;

		dquot->dq_flags |= DQ_MOD;
		dqput(dquot);
	}
	return 0;
}

static int get_quota(struct super_block *sb, int id, short type, struct dqblk *dqblk)
{
	struct dquot *dquot;
	struct dqblk data;
	int error = -ESRCH;

	if (!sb || !sb_has_quota_enabled(sb, type))
		goto out;
	dquot = dqget(sb, id, type);
	if (dquot == NODQUOT)
		goto out;

	memcpy(&data, &dquot->dq_dqb, sizeof(struct dqblk));        /* We copy data to preserve them from changing */
	dqput(dquot);
	error = -EFAULT;
	if (dqblk && !copy_to_user(dqblk, &data, sizeof(struct dqblk)))
		error = 0;
out:
	return error;
}

static int get_stats(caddr_t addr)
{
	int error = -EFAULT;
	struct dqstats stats;

	dqstats.allocated_dquots = nr_dquots;
	dqstats.free_dquots = nr_free_dquots;

	/* make a copy, in case we page-fault in user space */
	memcpy(&stats, &dqstats, sizeof(struct dqstats));
	if (!copy_to_user(addr, &stats, sizeof(struct dqstats)))
		error = 0;
	return error;
}

static int quota_root_squash(struct super_block *sb, short type, int *addr)
{
	int new_value, error;

	if (!sb)
		return(-ENODEV);

	error = -EFAULT;
	if (!copy_from_user(&new_value, addr, sizeof(int))) {
		sb_dqopt(sb)->rsquash[type] = new_value;
		error = 0;
	}
	return error;
}

#if 0	/* We are not going to support filesystems without i_blocks... */
/*
 * This is a simple algorithm that calculates the size of a file in blocks.
 * This is only used on filesystems that do not have an i_blocks count.
 */
static u_long isize_to_blocks(loff_t isize, size_t blksize_bits)
{
	u_long blocks;
	u_long indirect;

	if (!blksize_bits)
		blksize_bits = BLOCK_SIZE_BITS;
	blocks = (isize >> blksize_bits) + ((isize & ~((1 << blksize_bits)-1)) ? 1 : 0);
	if (blocks > 10) {
		indirect = ((blocks - 11) >> 8) + 1; /* single indirect blocks */
		if (blocks > (10 + 256)) {
			indirect += ((blocks - 267) >> 16) + 1; /* double indirect blocks */
			if (blocks > (10 + 256 + (256 << 8)))
				indirect++; /* triple indirect blocks */
		}
		blocks += indirect;
	}
	return blocks;
}
#endif

/*
 * Externally referenced functions through dquot_operations in inode.
 *
 * Note: this is a blocking operation.
 */
void dquot_initialize(struct inode *inode, short type)
{
	struct dquot *dquot[MAXQUOTAS];
	unsigned int id = 0;
	short cnt;

	if (IS_NOQUOTA(inode))
		return;
	/* Build list of quotas to initialize... We can block here */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot[cnt] = NODQUOT;
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_enabled(inode->i_sb, cnt))
			continue;
		if (inode->i_dquot[cnt] == NODQUOT) {
			switch (cnt) {
				case USRQUOTA:
					id = inode->i_uid;
					break;
				case GRPQUOTA:
					id = inode->i_gid;
					break;
			}
			dquot[cnt] = dqget(inode->i_sb, id, cnt);
		}
	}
	/* NOBLOCK START: Here we shouldn't block */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquot[cnt] == NODQUOT || !sb_has_quota_enabled(inode->i_sb, cnt) || inode->i_dquot[cnt] != NODQUOT)
			continue;
		inode->i_dquot[cnt] = dquot[cnt];
		dquot[cnt] = NODQUOT;
		inode->i_flags |= S_QUOTA;
	}
	/* NOBLOCK END */
	/* Put quotas which we didn't use */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (dquot[cnt] != NODQUOT)
			dqput(dquot[cnt]);
}

/*
 * Release all quota for the specified inode.
 *
 * Note: this is a blocking operation.
 */
void dquot_drop(struct inode *inode)
{
	struct dquot *dquot;
	short cnt;

	inode->i_flags &= ~S_QUOTA;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot = inode->i_dquot[cnt];
		inode->i_dquot[cnt] = NODQUOT;
		dqput(dquot);
	}
}

/*
 * This operation can block, but only after everything is updated
 */
int dquot_alloc_block(struct inode *inode, unsigned long number, char warn)
{
	int cnt, ret = NO_QUOTA;
	struct dquot *dquot[MAXQUOTAS];
	char warntype[MAXQUOTAS];

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot[cnt] = NODQUOT;
		warntype[cnt] = NOWARN;
	}
	/* NOBLOCK Start */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot[cnt] = dqduplicate(inode->i_dquot[cnt]);
		if (dquot[cnt] == NODQUOT)
			continue;
		if (check_bdq(dquot[cnt], number, warn, warntype+cnt) == NO_QUOTA)
			goto warn_put_all;
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_blocks(dquot[cnt], number);
	}
	inode->i_blocks += number << (BLOCK_SIZE_BITS - 9);
	/* NOBLOCK End */
	ret = QUOTA_OK;
warn_put_all:
	flush_warnings(dquot, warntype);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (dquot[cnt] != NODQUOT)
			dqput(dquot[cnt]);
	return ret;
}

/*
 * This operation can block, but only after everything is updated
 */
int dquot_alloc_inode(const struct inode *inode, unsigned long number)
{
	int cnt, ret = NO_QUOTA;
	struct dquot *dquot[MAXQUOTAS];
	char warntype[MAXQUOTAS];

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot[cnt] = NODQUOT;
		warntype[cnt] = NOWARN;
	}
	/* NOBLOCK Start */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot[cnt] = dqduplicate(inode -> i_dquot[cnt]);
		if (dquot[cnt] == NODQUOT)
			continue;
		if (check_idq(dquot[cnt], number, warntype+cnt) == NO_QUOTA)
			goto warn_put_all;
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_inodes(dquot[cnt], number);
	}
	/* NOBLOCK End */
	ret = QUOTA_OK;
warn_put_all:
	flush_warnings(dquot, warntype);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (dquot[cnt] != NODQUOT)
			dqput(dquot[cnt]);
	return ret;
}

/*
 * This is a non-blocking operation.
 */
void dquot_free_block(struct inode *inode, unsigned long number)
{
	unsigned short cnt;
	struct dquot *dquot;

	/* NOBLOCK Start */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot = dqduplicate(inode->i_dquot[cnt]);
		if (dquot == NODQUOT)
			continue;
		dquot_decr_blocks(dquot, number);
		dqput(dquot);
	}
	inode->i_blocks -= number << (BLOCK_SIZE_BITS - 9);
	/* NOBLOCK End */
}

/*
 * This is a non-blocking operation.
 */
void dquot_free_inode(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;
	struct dquot *dquot;

	/* NOBLOCK Start */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot = dqduplicate(inode->i_dquot[cnt]);
		if (dquot == NODQUOT)
			continue;
		dquot_decr_inodes(dquot, number);
		dqput(dquot);
	}
	/* NOBLOCK End */
}

/*
 * Transfer the number of inode and blocks from one diskquota to an other.
 *
 * This operation can block, but only after everything is updated
 */
int dquot_transfer(struct inode *inode, struct iattr *iattr)
{
	unsigned long blocks;
	struct dquot *transfer_from[MAXQUOTAS];
	struct dquot *transfer_to[MAXQUOTAS];
	int cnt, ret = NO_QUOTA, chuid = (iattr->ia_valid & ATTR_UID) && inode->i_uid != iattr->ia_uid,
	    chgid = (iattr->ia_valid & ATTR_GID) && inode->i_gid != iattr->ia_gid;
	char warntype[MAXQUOTAS];

	/* Clear the arrays */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		transfer_to[cnt] = transfer_from[cnt] = NODQUOT;
		warntype[cnt] = NOWARN;
	}
	/* First build the transfer_to list - here we can block on reading of dquots... */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!sb_has_quota_enabled(inode->i_sb, cnt))
			continue;
		switch (cnt) {
			case USRQUOTA:
				if (!chuid)
					continue;
				transfer_to[cnt] = dqget(inode->i_sb, iattr->ia_uid, cnt);
				break;
			case GRPQUOTA:
				if (!chgid)
					continue;
				transfer_to[cnt] = dqget(inode->i_sb, iattr->ia_gid, cnt);
				break;
		}
	}
	/* NOBLOCK START: From now on we shouldn't block */
	blocks = (inode->i_blocks >> 1);
	/* Build the transfer_from list and check the limits */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		/* The second test can fail when quotaoff is in progress... */
		if (transfer_to[cnt] == NODQUOT || !sb_has_quota_enabled(inode->i_sb, cnt))
			continue;
		transfer_from[cnt] = dqduplicate(inode->i_dquot[cnt]);
		if (transfer_from[cnt] == NODQUOT)	/* Can happen on quotafiles (quota isn't initialized on them)... */
			continue;
		if (check_idq(transfer_to[cnt], 1, warntype+cnt) == NO_QUOTA ||
		    check_bdq(transfer_to[cnt], blocks, 0, warntype+cnt) == NO_QUOTA)
			goto warn_put_all;
	}

	/*
	 * Finally perform the needed transfer from transfer_from to transfer_to
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		/*
		 * Skip changes for same uid or gid or for non-existing quota-type.
		 */
		if (transfer_from[cnt] == NODQUOT || transfer_to[cnt] == NODQUOT)
			continue;

		dquot_decr_inodes(transfer_from[cnt], 1);
		dquot_decr_blocks(transfer_from[cnt], blocks);

		dquot_incr_inodes(transfer_to[cnt], 1);
		dquot_incr_blocks(transfer_to[cnt], blocks);

		if (inode->i_dquot[cnt] == NODQUOT)
			BUG();
		inode->i_dquot[cnt] = transfer_to[cnt];
		/*
		 * We've got to release transfer_from[] twice - once for dquot_transfer() and
		 * once for inode. We don't want to release transfer_to[] as it's now placed in inode
		 */
		transfer_to[cnt] = transfer_from[cnt];
	}
	/* NOBLOCK END. From now on we can block as we wish */
	ret = QUOTA_OK;
warn_put_all:
	flush_warnings(transfer_to, warntype);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (transfer_to[cnt] != NODQUOT)
			dqput(transfer_to[cnt]);
		if (transfer_from[cnt] != NODQUOT)
			dqput(transfer_from[cnt]);
	}
	return ret;
}

static int __init dquot_init(void)
{
	int i;

	for (i = 0; i < NR_DQHASH; i++)
		INIT_LIST_HEAD(dquot_hash + i);
	printk(KERN_NOTICE "VFS: Diskquotas version %s initialized\n", __DQUOT_VERSION__);
	return 0;
}
__initcall(dquot_init);

/*
 * Definitions of diskquota operations.
 */
struct dquot_operations dquot_operations = {
	dquot_initialize,		/* mandatory */
	dquot_drop,			/* mandatory */
	dquot_alloc_block,
	dquot_alloc_inode,
	dquot_free_block,
	dquot_free_inode,
	dquot_transfer
};

static inline void set_enable_flags(struct quota_mount_options *dqopt, short type)
{
	switch (type) {
		case USRQUOTA:
			dqopt->flags |= DQUOT_USR_ENABLED;
			break;
		case GRPQUOTA:
			dqopt->flags |= DQUOT_GRP_ENABLED;
			break;
	}
}

static inline void reset_enable_flags(struct quota_mount_options *dqopt, short type)
{
	switch (type) {
		case USRQUOTA:
			dqopt->flags &= ~DQUOT_USR_ENABLED;
			break;
		case GRPQUOTA:
			dqopt->flags &= ~DQUOT_GRP_ENABLED;
			break;
	}
}

/* Function in inode.c - remove pointers to dquots in icache */
extern void remove_dquot_ref(struct super_block *, short);

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */
int quota_off(struct super_block *sb, short type)
{
	struct file *filp;
	short cnt;
	struct quota_mount_options *dqopt = sb_dqopt(sb);

	lock_kernel();
	if (!sb)
		goto out;

	/* We need to serialize quota_off() for device */
	down(&dqopt->dqoff_sem);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!is_enabled(dqopt, cnt))
			continue;
		reset_enable_flags(dqopt, cnt);

		/* Note: these are blocking operations */
		remove_dquot_ref(sb, cnt);
		invalidate_dquots(sb, cnt);

		filp = dqopt->files[cnt];
		dqopt->files[cnt] = (struct file *)NULL;
		dqopt->inode_expire[cnt] = 0;
		dqopt->block_expire[cnt] = 0;
		fput(filp);
	}	
	up(&dqopt->dqoff_sem);
out:
	unlock_kernel();
	return 0;
}

static inline int check_quotafile_size(loff_t size)
{
	ulong blocks = size >> BLOCK_SIZE_BITS;
	size_t off = size & (BLOCK_SIZE - 1);

	return !(((blocks % sizeof(struct dqblk)) * BLOCK_SIZE + off % sizeof(struct dqblk)) % sizeof(struct dqblk));
}

static int quota_on(struct super_block *sb, short type, char *path)
{
	struct file *f;
	struct inode *inode;
	struct dquot *dquot;
	struct quota_mount_options *dqopt = sb_dqopt(sb);
	char *tmp;
	int error;

	if (is_enabled(dqopt, type))
		return -EBUSY;

	down(&dqopt->dqoff_sem);
	tmp = getname(path);
	error = PTR_ERR(tmp);
	if (IS_ERR(tmp))
		goto out_lock;

	f = filp_open(tmp, O_RDWR, 0600);
	putname(tmp);

	error = PTR_ERR(f);
	if (IS_ERR(f))
		goto out_lock;
	error = -EIO;
	if (!f->f_op || !f->f_op->read || !f->f_op->write)
		goto out_f;
	inode = f->f_dentry->d_inode;
	error = -EACCES;
	if (!S_ISREG(inode->i_mode))
		goto out_f;
	error = -EINVAL;
	if (inode->i_size == 0 || !check_quotafile_size(inode->i_size))
		goto out_f;
	/* We don't want quota on quota files */
	dquot_drop(inode);
	inode->i_flags |= S_NOQUOTA;

	dqopt->files[type] = f;
	sb->dq_op = &dquot_operations;
	set_enable_flags(dqopt, type);

	dquot = dqget(sb, 0, type);
	dqopt->inode_expire[type] = (dquot != NODQUOT) ? dquot->dq_itime : MAX_IQ_TIME;
	dqopt->block_expire[type] = (dquot != NODQUOT) ? dquot->dq_btime : MAX_DQ_TIME;
	dqput(dquot);

	add_dquot_ref(sb, type);

	up(&dqopt->dqoff_sem);
	return 0;

out_f:
	filp_close(f, NULL);
out_lock:
	up(&dqopt->dqoff_sem);

	return error; 
}

/*
 * This is the system call interface. This communicates with
 * the user-level programs. Currently this only supports diskquota
 * calls. Maybe we need to add the process quotas etc. in the future,
 * but we probably should use rlimits for that.
 */
asmlinkage long sys_quotactl(int cmd, const char *special, int id, caddr_t addr)
{
	int cmds = 0, type = 0, flags = 0;
	kdev_t dev;
	struct super_block *sb = NULL;
	int ret = -EINVAL;

	lock_kernel();
	cmds = cmd >> SUBCMDSHIFT;
	type = cmd & SUBCMDMASK;

	if ((u_int) type >= MAXQUOTAS)
		goto out;
	if (id & ~0xFFFF)
		goto out;

	ret = -EPERM;
	switch (cmds) {
		case Q_SYNC:
		case Q_GETSTATS:
			break;
		case Q_GETQUOTA:
			if (((type == USRQUOTA && current->euid != id) ||
			     (type == GRPQUOTA && !in_egroup_p(id))) &&
			    !capable(CAP_SYS_ADMIN))
				goto out;
			break;
		default:
			if (!capable(CAP_SYS_ADMIN))
				goto out;
	}

	ret = -EINVAL;
	dev = NODEV;
	if (special != NULL || (cmds != Q_SYNC && cmds != Q_GETSTATS)) {
		mode_t mode;
		struct nameidata nd;

		ret = user_path_walk(special, &nd);
		if (ret)
			goto out;

		dev = nd.dentry->d_inode->i_rdev;
		mode = nd.dentry->d_inode->i_mode;
		path_release(&nd);

		ret = -ENOTBLK;
		if (!S_ISBLK(mode))
			goto out;
		ret = -ENODEV;
		sb = get_super(dev);
		if (!sb)
			goto out;
	}

	ret = -EINVAL;
	switch (cmds) {
		case Q_QUOTAON:
			ret = quota_on(sb, type, (char *) addr);
			goto out;
		case Q_QUOTAOFF:
			ret = quota_off(sb, type);
			goto out;
		case Q_GETQUOTA:
			ret = get_quota(sb, id, type, (struct dqblk *) addr);
			goto out;
		case Q_SETQUOTA:
			flags |= SET_QUOTA;
			break;
		case Q_SETUSE:
			flags |= SET_USE;
			break;
		case Q_SETQLIM:
			flags |= SET_QLIMIT;
			break;
		case Q_SYNC:
			ret = sync_dquots(dev, type);
			goto out;
		case Q_GETSTATS:
			ret = get_stats(addr);
			goto out;
		case Q_RSQUASH:
			ret = quota_root_squash(sb, type, (int *) addr);
			goto out;
		default:
			goto out;
	}

	ret = -NODEV;
	if (sb && sb_has_quota_enabled(sb, type))
		ret = set_dqblk(sb, id, type, flags, (struct dqblk *) addr);
out:
	if (sb)
		drop_super(sb);
	unlock_kernel();
	return ret;
}
