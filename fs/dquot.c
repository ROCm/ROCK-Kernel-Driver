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
 *		Fixed a few bugs in grow_dquots.
 *		Fixed deadlock in write_dquot() - we no longer account quotas on
 *		quota files
 *		remove_dquot_ref() moved to inode.c - it now traverses through inodes
 *		add_dquot_ref() restarts after blocking
 *		Added check for bogus uid and fixed check for group in quotactl.
 *		Jan Kara, <jack@suse.cz>, sponsored by SuSE CR, 10-11/99
 *
 * (C) Copyright 1994 - 1997 Marco van Wieringen 
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <linux/types.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/malloc.h>
#include <linux/mount.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#define __DQUOT_VERSION__	"dquot_6.4.0"

int nr_dquots, nr_free_dquots;
int max_dquots = NR_DQUOTS;

static char quotamessage[MAX_QUOTA_MESSAGE];
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
 * All dquots are placed on the inuse_list when first created, and this
 * list is used for the sync and invalidate operations, which must look
 * at every dquot.
 *
 * Unused dquots (dq_count == 0) are added to the free_dquots list when
 * freed, and this list is searched whenever we need an available dquot.
 * Dquots are removed from the list as soon as they are used again, and
 * nr_free_dquots gives the number of dquots on the list.
 *
 * Dquots with a specific identity (device, type and id) are placed on
 * one of the dquot_hash[] hash chains. The provides an efficient search
 * mechanism to lcoate a specific dquot.
 */

static struct dquot *inuse_list;
static LIST_HEAD(free_dquots);
static struct dquot *dquot_hash[NR_DQHASH];
static int dquot_updating[NR_DQHASH];

static struct dqstats dqstats;
static DECLARE_WAIT_QUEUE_HEAD(dquot_wait);
static DECLARE_WAIT_QUEUE_HEAD(update_wait);

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
	struct dquot **htable;

	htable = &dquot_hash[hashfn(dquot->dq_dev, dquot->dq_id, dquot->dq_type)];
	if ((dquot->dq_hash_next = *htable) != NULL)
		(*htable)->dq_hash_pprev = &dquot->dq_hash_next;
	*htable = dquot;
	dquot->dq_hash_pprev = htable;
}

static inline void hash_dquot(struct dquot *dquot)
{
	insert_dquot_hash(dquot);
}

static inline void unhash_dquot(struct dquot *dquot)
{
	if (dquot->dq_hash_pprev) {
		if (dquot->dq_hash_next)
			dquot->dq_hash_next->dq_hash_pprev = dquot->dq_hash_pprev;
		*(dquot->dq_hash_pprev) = dquot->dq_hash_next;
		dquot->dq_hash_pprev = NULL;
	}
}

static inline struct dquot *find_dquot(unsigned int hashent, kdev_t dev, unsigned int id, short type)
{
	struct dquot *dquot;

	for (dquot = dquot_hash[hashent]; dquot; dquot = dquot->dq_hash_next)
		if (dquot->dq_dev == dev && dquot->dq_id == id && dquot->dq_type == type)
			break;
	return dquot;
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

static inline void remove_free_dquot(struct dquot *dquot)
{
	/* sanity check */
	if (list_empty(&dquot->dq_free)) {
		printk("remove_free_dquot: dquot not on the free list??\n");
		return;		/* J.K. Just don't do anything */
	}
	list_del(&dquot->dq_free);
	INIT_LIST_HEAD(&dquot->dq_free);
	nr_free_dquots--;
}

static inline void put_inuse(struct dquot *dquot)
{
	if ((dquot->dq_next = inuse_list) != NULL)
		inuse_list->dq_pprev = &dquot->dq_next;
	inuse_list = dquot;
	dquot->dq_pprev = &inuse_list;
}

#if 0	/* currently not needed */
static inline void remove_inuse(struct dquot *dquot)
{
	if (dquot->dq_pprev) {
		if (dquot->dq_next)
			dquot->dq_next->dq_pprev = dquot->dq_pprev;
		*dquot->dq_pprev = dquot->dq_next;
		dquot->dq_pprev = NULL;
	}
}
#endif

static void __wait_on_dquot(struct dquot *dquot)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&dquot->dq_wait, &wait);
repeat:
	set_current_state(TASK_UNINTERRUPTIBLE);
	if (dquot->dq_flags & DQ_LOCKED) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&dquot->dq_wait, &wait);
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
	wake_up(&dquot->dq_wait);
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

	lock_dquot(dquot);
	if (!dquot->dq_sb) {	/* Invalidated quota? */
		unlock_dquot(dquot);
		return;
	}
	down(sem);
	filp = dquot->dq_sb->s_dquot.files[type];
	offset = dqoff(dquot->dq_id);
	fs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 * Note: clear the DQ_MOD flag unconditionally,
	 * so we don't loop forever on failure.
	 */
	dquot->dq_flags &= ~DQ_MOD;
	ret = 0;
	if (filp)
		ret = filp->f_op->write(filp, (char *)&dquot->dq_dqb, 
					sizeof(struct dqblk), &offset);
	if (ret != sizeof(struct dqblk))
		printk(KERN_WARNING "VFS: dquota write failed on dev %s\n",
			kdevname(dquot->dq_dev));

	set_fs(fs);
	up(sem);
	unlock_dquot(dquot);

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

/*
 * Unhash and selectively clear the dquot structure,
 * but preserve the use count, list pointers, and
 * wait queue.
 */
void clear_dquot(struct dquot *dquot)
{
	/* unhash it first */
        unhash_dquot(dquot);
        dquot->dq_sb = NULL;
        dquot->dq_flags = 0;
        dquot->dq_referenced = 0;
        memset(&dquot->dq_dqb, 0, sizeof(struct dqblk));
}

void invalidate_dquots(kdev_t dev, short type)
{
	struct dquot *dquot, *next;
	int need_restart;

restart:
	next = inuse_list;	/* Here it is better. Otherwise the restart doesn't have any sense ;-) */
	need_restart = 0;
	while ((dquot = next) != NULL) {
		next = dquot->dq_next;
		if (dquot->dq_dev != dev)
			continue;
		if (dquot->dq_type != type)
			continue;
		if (!dquot->dq_sb)	/* Already invalidated entry? */
			continue;
		if (dquot->dq_flags & DQ_LOCKED) {
			__wait_on_dquot(dquot);

			/* Set the flag for another pass. */
			need_restart = 1;
			/*
			 * Make sure it's still the same dquot.
			 */
			if (dquot->dq_dev != dev)
				continue;
			if (dquot->dq_type != type)
				continue;
			if (!dquot->dq_sb)
				continue;
		}
		/*
		 *  Because inodes needn't to be the only holders of dquot
		 *  the quota needn't to be written to disk. So we write it
		 *  ourselves before discarding the data just for sure...
		 */
		if (dquot->dq_flags & DQ_MOD && dquot->dq_sb)
		{
			write_dquot(dquot);
			need_restart = 1;	/* We slept on IO */
		}
		clear_dquot(dquot);
	}
	/*
	 * If anything blocked, restart the operation
	 * to ensure we don't miss any dquots.
	 */ 
	if (need_restart)
		goto restart;
}

int sync_dquots(kdev_t dev, short type)
{
	struct dquot *dquot, *next, *ddquot;
	int need_restart;

restart:
	next = inuse_list;
	need_restart = 0;
	while ((dquot = next) != NULL) {
		next = dquot->dq_next;
		if (dev && dquot->dq_dev != dev)
			continue;
                if (type != -1 && dquot->dq_type != type)
			continue;
		if (!dquot->dq_sb)	/* Invalidated? */
			continue;
		if (!(dquot->dq_flags & (DQ_LOCKED | DQ_MOD)))
			continue;

		if ((ddquot = dqduplicate(dquot)) == NODQUOT)
			continue;
		if (ddquot->dq_flags & DQ_MOD)
			write_dquot(ddquot);
		dqput(ddquot);
		/* Set the flag for another pass. */
		need_restart = 1;
	}
	/*
	 * If anything blocked, restart the operation
	 * to ensure we don't miss any dquots.
	 */ 
	if (need_restart)
		goto restart;

	dqstats.syncs++;
	return(0);
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

	/*
	 * If the dq_sb pointer isn't initialized this entry needs no
	 * checking and doesn't need to be written. It's just an empty
	 * dquot that is put back on to the freelist.
	 */
	if (dquot->dq_sb)
		dqstats.drops++;
we_slept:
	if (dquot->dq_count > 1) {
		/* We have more than one user... We can simply decrement use count */
		dquot->dq_count--;
		return;
	}
	if (dquot->dq_flags & DQ_LOCKED) {
		printk(KERN_ERR "VFS: Locked quota to be put on the free list.\n");
		dquot->dq_flags &= ~DQ_LOCKED;
	}
	if (dquot->dq_sb && dquot->dq_flags & DQ_MOD) {
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
	dquot->dq_flags &= ~DQ_MOD;	/* Modified flag has no sense on free list */
	/* Place at end of LRU free queue */
	put_dquot_last(dquot);
	wake_up(&dquot_wait);
}

static int grow_dquots(void)
{
	struct dquot *dquot;
	int cnt = 0;

	while (cnt < 32) {
		dquot = kmem_cache_alloc(dquot_cachep, SLAB_KERNEL);
		if(!dquot)
			return cnt;

		nr_dquots++;
		memset((caddr_t)dquot, 0, sizeof(struct dquot));
		init_waitqueue_head(&dquot->dq_wait);
		/* all dquots go on the inuse_list */
		put_inuse(dquot);
		put_dquot_head(dquot);
		cnt++;
	}
	return cnt;
}

static struct dquot *find_best_candidate_weighted(void)
{
	struct list_head *tmp = &free_dquots;
	struct dquot *dquot, *best = NULL;
	unsigned long myscore, bestscore = ~0U;
	int limit = (nr_free_dquots > 128) ? nr_free_dquots >> 2 : 32;

	while ((tmp = tmp->next) != &free_dquots && --limit) {
		dquot = list_entry(tmp, struct dquot, dq_free);
		/* This should never happen... */
		if (dquot->dq_flags & (DQ_LOCKED | DQ_MOD))
			continue;
		myscore = dquot->dq_referenced;
		if (myscore < bestscore) {
			bestscore = myscore;
			best = dquot;
		}
	}
	return best;
}

static inline struct dquot *find_best_free(void)
{
	struct list_head *tmp = &free_dquots;
	struct dquot *dquot;
	int limit = (nr_free_dquots > 1024) ? nr_free_dquots >> 5 : 32;

	while ((tmp = tmp->next) != &free_dquots && --limit) {
		dquot = list_entry(tmp, struct dquot, dq_free);
		if (dquot->dq_referenced == 0)
			return dquot;
	}
	return NULL;
}

struct dquot *get_empty_dquot(void)
{
	struct dquot *dquot;
	int shrink = 8;	/* Number of times we should try to shrink dcache and icache */

repeat:
	dquot = find_best_free();
	if (!dquot)
		goto pressure;
got_it:
	/* Sanity checks */
	if (dquot->dq_flags & DQ_LOCKED)
		printk(KERN_ERR "VFS: Locked dquot on the free list\n");
	if (dquot->dq_count != 0)
		printk(KERN_ERR "VFS: free dquot count=%d\n", dquot->dq_count);

	remove_free_dquot(dquot);
	dquot->dq_count = 1;
	/* unhash and selectively clear the structure */
	clear_dquot(dquot);
	return dquot;

pressure:
	if (nr_dquots < max_dquots)
		if (grow_dquots())
			goto repeat;

	dquot = find_best_candidate_weighted();
	if (dquot)
		goto got_it;
	/*
	 * Try pruning the dcache to free up some dquots ...
	 */
	if (shrink) {
		printk(KERN_DEBUG "get_empty_dquot: pruning dcache and icache\n");
		prune_dcache(128);
		prune_icache(128);
		shrink--;
		goto repeat;
	}

	printk("VFS: No free dquots, contact mvw@planets.elm.net\n");
	sleep_on(&dquot_wait);
	goto repeat;
}

static struct dquot *dqget(struct super_block *sb, unsigned int id, short type)
{
	unsigned int hashent = hashfn(sb->s_dev, id, type);
	struct dquot *dquot, *empty = NULL;
	struct quota_mount_options *dqopt = sb_dqopt(sb);

        if (!is_enabled(dqopt, type))
                return(NODQUOT);

we_slept:
	if ((dquot = find_dquot(hashent, sb->s_dev, id, type)) == NULL) {
		if (empty == NULL) {
			dquot_updating[hashent]++;
			empty = get_empty_dquot();
			if (!--dquot_updating[hashent])
				wake_up(&update_wait);
			goto we_slept;
		}
		dquot = empty;
        	dquot->dq_id = id;
        	dquot->dq_type = type;
        	dquot->dq_dev = sb->s_dev;
        	dquot->dq_sb = sb;
		/* hash it first so it can be found */
		hash_dquot(dquot);
        	read_dquot(dquot);
	} else {
		if (!dquot->dq_count++) {
			remove_free_dquot(dquot);
		} else
			dqstats.cache_hits++;
		wait_on_dquot(dquot);
		if (empty)
			dqput(empty);
	}

	while (dquot_updating[hashent])
		sleep_on(&update_wait);

	if (!dquot->dq_sb) {	/* Has somebody invalidated entry under us? */
		/*
		 *  Do it as if the quota was invalidated before we started
		 */
		dqput(dquot);
		return NODQUOT;
	}
	dquot->dq_referenced++;
	dqstats.lookups++;

	return dquot;
}

static struct dquot *dqduplicate(struct dquot *dquot)
{
	if (dquot == NODQUOT || !dquot->dq_sb)
		return NODQUOT;
	dquot->dq_count++;
	wait_on_dquot(dquot);
	if (!dquot->dq_sb) {
		dquot->dq_count--;
		return NODQUOT;
	}
	dquot->dq_referenced++;
	dqstats.lookups++;
	return dquot;
}

/* Check whether this inode is quota file */
static inline int is_quotafile(struct inode *inode)
{
	int cnt;
	struct quota_mount_options *dqopt = sb_dqopt(inode->i_sb);
	struct file **files;

	if (!dqopt)
		return 0;
	files = dqopt->files;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (files[cnt] && files[cnt]->f_dentry->d_inode == inode)
			return 1;
	return 0;
}

static int dqinit_needed(struct inode *inode, short type)
{
	int cnt;

        if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
                return 0;
	if (is_quotafile(inode))
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
	struct inode *inode;

	if (!sb->dq_op)
		return;	/* nothing to do */

restart:
	file_list_lock();
	for (p = sb->s_files.next; p != &sb->s_files; p = p->next) {
		struct file *filp = list_entry(p, struct file, f_list);
		if (!filp->f_dentry)
			continue;
		inode = filp->f_dentry->d_inode;
		if (filp->f_mode & FMODE_WRITE && dqinit_needed(inode, type)) {
			file_list_unlock();
			sb->dq_op->initialize(inode, type);
			inode->i_flags |= S_QUOTA;
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
		} else {
			dqput(dquot);   /* We have guaranteed we won't block */
		}
	}
	return 0;
}

/* Free list of dquots - called from inode.c */
void put_dquot_list(struct list_head *tofree_head)
{
	struct list_head *act_head = tofree_head->next;
	struct dquot *dquot;

	/* So now we have dquots on the list... Just free them */
	while (act_head != tofree_head) {
		dquot = list_entry(act_head, struct dquot, dq_free);
		act_head = act_head->next;
		list_del(&dquot->dq_free);	/* Remove dquot from the list so we won't have problems... */
		INIT_LIST_HEAD(&dquot->dq_free);
		dqput(dquot);
	}
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

static void print_warning(struct dquot *dquot, int flag, const char *fmtstr)
{
	if (!need_print_warning(dquot, flag))
		return;
	sprintf(quotamessage, fmtstr,
		bdevname(dquot->dq_sb->s_dev), quotatypes[dquot->dq_type]);
	tty_write_message(current->tty, quotamessage);
	dquot->dq_flags |= flag;
}

static inline char ignore_hardlimit(struct dquot *dquot)
{
	return capable(CAP_SYS_RESOURCE) && !dquot->dq_sb->s_dquot.rsquash[dquot->dq_type];
}

static int check_idq(struct dquot *dquot, u_long inodes)
{
	if (inodes <= 0 || dquot->dq_flags & DQ_FAKE)
		return QUOTA_OK;

	if (dquot->dq_ihardlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_ihardlimit &&
            !ignore_hardlimit(dquot)) {
		print_warning(dquot, DQ_INODES, "%s: write failed, %s file limit reached\n");
		return NO_QUOTA;
	}

	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime && CURRENT_TIME >= dquot->dq_itime &&
            !ignore_hardlimit(dquot)) {
		print_warning(dquot, DQ_INODES, "%s: warning, %s file quota exceeded too long.\n");
		return NO_QUOTA;
	}

	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime == 0) {
		print_warning(dquot, 0, "%s: warning, %s file quota exceeded\n");
		dquot->dq_itime = CURRENT_TIME + dquot->dq_sb->s_dquot.inode_expire[dquot->dq_type];
	}

	return QUOTA_OK;
}

static int check_bdq(struct dquot *dquot, u_long blocks, char prealloc)
{
	if (blocks <= 0 || dquot->dq_flags & DQ_FAKE)
		return QUOTA_OK;

	if (dquot->dq_bhardlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bhardlimit &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			print_warning(dquot, DQ_BLKS, "%s: write failed, %s disk limit reached.\n");
		return NO_QUOTA;
	}

	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime && CURRENT_TIME >= dquot->dq_btime &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			print_warning(dquot, DQ_BLKS, "%s: write failed, %s disk quota exceeded too long.\n");
		return NO_QUOTA;
	}

	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime == 0) {
		if (!prealloc) {
			print_warning(dquot, 0, "%s: warning, %s disk quota exceeded\n");
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

	if (dqblk == (struct dqblk *)NULL)
		return error;

	if (flags & QUOTA_SYSCALL) {
		if (copy_from_user(&dq_dqblk, dqblk, sizeof(struct dqblk)))
			return(error);
	} else
		memcpy((caddr_t)&dq_dqblk, (caddr_t)dqblk, sizeof(struct dqblk));

	if (sb && (dquot = dqget(sb, id, type)) != NODQUOT) {
		lock_dquot(dquot);

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
		unlock_dquot(dquot);
		dqput(dquot);
	}
	return(0);
}

static int get_quota(struct super_block *sb, int id, short type, struct dqblk *dqblk)
{
	struct dquot *dquot;
	int error = -ESRCH;

	if (!sb || !sb_has_quota_enabled(sb, type))
		goto out;
	dquot = dqget(sb, id, type);
	if (dquot == NODQUOT)
		goto out;

	lock_dquot(dquot);	/* We must protect against invalidating the quota */
	error = -EFAULT;
	if (dqblk && !copy_to_user(dqblk, &dquot->dq_dqb, sizeof(struct dqblk)))
		error = 0;
	unlock_dquot(dquot);
	dqput(dquot);
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

/*
 * Externally referenced functions through dquot_operations in inode.
 *
 * Note: this is a blocking operation.
 */
void dquot_initialize(struct inode *inode, short type)
{
	struct dquot *dquot;
	unsigned int id = 0;
	short cnt;

	if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode) &&
            !S_ISLNK(inode->i_mode))
		return;
	lock_kernel();
	/* We don't want to have quotas on quota files - nasty deadlocks possible */
	if (is_quotafile(inode)) {
		unlock_kernel();
		return;
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
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
			dquot = dqget(inode->i_sb, id, cnt);
			if (dquot == NODQUOT)
				continue;
			if (inode->i_dquot[cnt] != NODQUOT) {
				dqput(dquot);
				continue;
			} 
			inode->i_dquot[cnt] = dquot;
			inode->i_flags |= S_QUOTA;
		}
	}
	unlock_kernel();
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

	lock_kernel();
	inode->i_flags &= ~S_QUOTA;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot = inode->i_dquot[cnt];
		inode->i_dquot[cnt] = NODQUOT;
		dqput(dquot);
	}
	unlock_kernel();
}

/*
 * Note: this is a blocking operation.
 */
int dquot_alloc_block(const struct inode *inode, unsigned long number, char warn)
{
	int cnt;
	struct dquot *dquot[MAXQUOTAS];

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot[cnt] = dqduplicate(inode->i_dquot[cnt]);
		if (dquot[cnt] == NODQUOT)
			continue;
		lock_dquot(dquot[cnt]);
		if (check_bdq(dquot[cnt], number, warn))
			goto put_all;
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_blocks(dquot[cnt], number);
		unlock_dquot(dquot[cnt]);
		dqput(dquot[cnt]);
	}

	return QUOTA_OK;
put_all:
	for (; cnt >= 0; cnt--) {
		if (dquot[cnt] == NODQUOT)
			continue;
		unlock_dquot(dquot[cnt]);
		dqput(dquot[cnt]);
	}
	return NO_QUOTA;
}

/*
 * Note: this is a blocking operation.
 */
int dquot_alloc_inode(const struct inode *inode, unsigned long number)
{
	int cnt;
	struct dquot *dquot[MAXQUOTAS];

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot[cnt] = dqduplicate(inode -> i_dquot[cnt]);
		if (dquot[cnt] == NODQUOT)
			continue;
		lock_dquot(dquot[cnt]);
		if (check_idq(dquot[cnt], number))
			goto put_all;
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_inodes(dquot[cnt], number);
		unlock_dquot(dquot[cnt]);
		dqput(dquot[cnt]);
	}

	return QUOTA_OK;
put_all:
	for (; cnt >= 0; cnt--) {
		if (dquot[cnt] == NODQUOT)
			continue;
		unlock_dquot(dquot[cnt]);
		dqput(dquot[cnt]);
	}
	return NO_QUOTA;
}

/*
 * Note: this is a blocking operation.
 */
void dquot_free_block(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;
	struct dquot *dquot;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot = inode->i_dquot[cnt];
		if (dquot == NODQUOT)
			continue;
		wait_on_dquot(dquot);
		dquot_decr_blocks(dquot, number);
	}
}

/*
 * Note: this is a blocking operation.
 */
void dquot_free_inode(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;
	struct dquot *dquot;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		dquot = inode->i_dquot[cnt];
		if (dquot == NODQUOT)
			continue;
		wait_on_dquot(dquot);
		dquot_decr_inodes(dquot, number);
	}
}

/*
 * Transfer the number of inode and blocks from one diskquota to an other.
 *
 * Note: this is a blocking operation.
 */
int dquot_transfer(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry -> d_inode;
	unsigned long blocks;
	struct dquot *transfer_from[MAXQUOTAS];
	struct dquot *transfer_to[MAXQUOTAS];
	short cnt, disc;
	int error = -EDQUOT;

	if (!inode)
		return -ENOENT;
	/* Arguably we could consider that as error, but... no fs - no quota */
	if (!inode->i_sb)
		return 0;

	lock_kernel();
	/*
	 * Build the transfer_from and transfer_to lists and check quotas to see
	 * if operation is permitted.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		transfer_from[cnt] = NODQUOT;
		transfer_to[cnt] = NODQUOT;

		if (!sb_has_quota_enabled(inode->i_sb, cnt))
			continue;

		switch (cnt) {
			case USRQUOTA:
				if (inode->i_uid == iattr->ia_uid)
					continue;
				/* We can get transfer_from from inode, can't we? */
				transfer_from[cnt] = dqget(inode->i_sb, inode->i_uid, cnt);
				transfer_to[cnt] = dqget(inode->i_sb, iattr->ia_uid, cnt);
				break;
			case GRPQUOTA:
				if (inode->i_gid == iattr->ia_gid)
					continue;
				transfer_from[cnt] = dqget(inode->i_sb, inode->i_gid, cnt);
				transfer_to[cnt] = dqget(inode->i_sb, iattr->ia_gid, cnt);
				break;
		}

		/* Something bad (eg. quotaoff) happened while we were sleeping? */
		if (transfer_from[cnt] == NODQUOT || transfer_to[cnt] == NODQUOT)
		{
			if (transfer_from[cnt] != NODQUOT) {
				dqput(transfer_from[cnt]);
				transfer_from[cnt] = NODQUOT;
			}
			if (transfer_to[cnt] != NODQUOT) {
				dqput(transfer_to[cnt]);
				transfer_to[cnt] = NODQUOT;
			}
			continue;
		}
		/*
		 *  We have to lock the quotas to prevent races...
		 */
		if (transfer_from[cnt] < transfer_to[cnt])
		{
                	lock_dquot(transfer_from[cnt]);
			lock_dquot(transfer_to[cnt]);
		}
		else
		{
			lock_dquot(transfer_to[cnt]);
			lock_dquot(transfer_from[cnt]);
		}

		/*
		 * The entries might got invalidated while locking. The second
		 * dqget() could block and so the first structure might got
		 * invalidated or locked...
		 */
		if (!transfer_to[cnt]->dq_sb || !transfer_from[cnt]->dq_sb) {
			cnt++;
			goto put_all;
		}
	}

	/*
	 * Find out if this filesystem uses i_blocks.
	 */
	if (!inode->i_sb->s_blocksize)
		blocks = isize_to_blocks(inode->i_size, BLOCK_SIZE_BITS);
	else
		blocks = (inode->i_blocks >> 1);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!transfer_to[cnt])
			continue;
		if (check_idq(transfer_to[cnt], 1) == NO_QUOTA ||
		    check_bdq(transfer_to[cnt], blocks, 0) == NO_QUOTA) {
			cnt = MAXQUOTAS;
			goto put_all;
		}
	}

	if ((error = notify_change(dentry, iattr)))
		goto put_all; 
	/*
	 * Finally perform the needed transfer from transfer_from to transfer_to,
	 * and release any pointers to dquots not needed anymore.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		/*
		 * Skip changes for same uid or gid or for non-existing quota-type.
		 */
		if (transfer_from[cnt] == NODQUOT && transfer_to[cnt] == NODQUOT)
			continue;

		dquot_decr_inodes(transfer_from[cnt], 1);
		dquot_decr_blocks(transfer_from[cnt], blocks);

		dquot_incr_inodes(transfer_to[cnt], 1);
		dquot_incr_blocks(transfer_to[cnt], blocks);

		unlock_dquot(transfer_from[cnt]);
		dqput(transfer_from[cnt]);
		if (inode->i_dquot[cnt] != NODQUOT) {
			struct dquot *temp = inode->i_dquot[cnt];
			inode->i_dquot[cnt] = transfer_to[cnt];
			unlock_dquot(transfer_to[cnt]);
			dqput(temp);
		} else {
			unlock_dquot(transfer_to[cnt]);
			dqput(transfer_to[cnt]);
		}
	}

	unlock_kernel();
	return 0;
put_all:
	for (disc = 0; disc < cnt; disc++) {
		/* There should be none or both pointers set but... */
		if (transfer_to[disc] != NODQUOT) {
			unlock_dquot(transfer_to[disc]);
			dqput(transfer_to[disc]);
		}
		if (transfer_from[disc] != NODQUOT) {
			unlock_dquot(transfer_from[disc]);
			dqput(transfer_from[disc]);
		}
	}
	unlock_kernel();
	return error;
}


void __init dquot_init_hash(void)
{
	printk(KERN_NOTICE "VFS: Diskquotas version %s initialized\n", __DQUOT_VERSION__);

	memset(dquot_hash, 0, sizeof(dquot_hash));
	memset((caddr_t)&dqstats, 0, sizeof(dqstats));
}

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
extern void remove_dquot_ref(kdev_t, short);

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */
int quota_off(struct super_block *sb, short type)
{
	struct file *filp;
	short cnt;
	int enabled = 0;
	struct quota_mount_options *dqopt = sb_dqopt(sb);

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
		remove_dquot_ref(sb->s_dev, cnt);
		invalidate_dquots(sb->s_dev, cnt);

		/* Wait for any pending IO - remove me as soon as invalidate is more polite */
		down(&dqopt->dqio_sem);
		filp = dqopt->files[cnt];
		dqopt->files[cnt] = (struct file *)NULL;
		dqopt->inode_expire[cnt] = 0;
		dqopt->block_expire[cnt] = 0;
		up(&dqopt->dqio_sem);
		fput(filp);
	}	

	/*
	 * Check whether any quota is still enabled,
	 * and if not clear the dq_op pointer.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		enabled |= is_enabled(dqopt, cnt);
	if (!enabled)
		sb->dq_op = NULL;
	up(&dqopt->dqoff_sem);
out:
	return(0);
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
	if (!f->f_op || (!f->f_op->read && !f->f_op->write))
		goto out_f;
	inode = f->f_dentry->d_inode;
	error = -EACCES;
	if (!S_ISREG(inode->i_mode))
		goto out_f;
	error = -EINVAL;
	if (inode->i_size == 0 || !check_quotafile_size(inode->i_size))
		goto out_f;
	dquot_drop(inode);	/* We don't want quota on quota files */

	set_enable_flags(dqopt, type);
	dqopt->files[type] = f;

	dquot = dqget(sb, 0, type);
	dqopt->inode_expire[type] = (dquot != NODQUOT) ? dquot->dq_itime : MAX_IQ_TIME;
	dqopt->block_expire[type] = (dquot != NODQUOT) ? dquot->dq_btime : MAX_DQ_TIME;
	dqput(dquot);

	sb->dq_op = &dquot_operations;
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
			     (type == GRPQUOTA && in_egroup_p(id))) &&
			    !capable(CAP_SYS_RESOURCE))
				goto out;
			break;
		default:
			if (!capable(CAP_SYS_RESOURCE))
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
		sb = get_super(dev);
	}

	ret = -EINVAL;
	switch (cmds) {
		case Q_QUOTAON:
			ret = sb ? quota_on(sb, type, (char *) addr) : -ENODEV;
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

	flags |= QUOTA_SYSCALL;

	ret = -ESRCH;
	if (sb && sb_has_quota_enabled(sb, type))
		ret = set_dqblk(sb, id, type, flags, (struct dqblk *) addr);
out:
	unlock_kernel();
	return ret;
}
