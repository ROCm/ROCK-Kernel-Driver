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
 *		Invalidation of referenced dquots is no longer possible
 *		Improved free_dquots list management
 *		Quota and i_blocks are now updated in one place to avoid races
 *		Warnings are now delayed so we won't block in critical section
 *		Write updated not to require dquot lock
 *		Jan Kara, <jack@suse.cz>, 9/2000
 *
 *		Added dynamic quota structure allocation
 *		Jan Kara <jack@suse.cz> 12/2000
 *
 *		Rewritten quota interface. Implemented new quota format and
 *		formats registering.
 *		Jan Kara, <jack@suse.cz>, 2001,2002
 *
 *		New SMP locking.
 *		Jan Kara, <jack@suse.cz>, 10/2002
 *
 * (C) Copyright 1994 - 1997 Marco van Wieringen 
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/kmod.h>

#include <asm/uaccess.h>

#define __DQUOT_PARANOIA

/*
 * There are two quota SMP locks. dq_list_lock protects all lists with quotas
 * and quota formats and also dqstats structure containing statistics about the
 * lists. dq_data_lock protects data from dq_dqb and also mem_dqinfo structures
 * and also guards consistency of dquot->dq_dqb with inode->i_blocks, i_bytes.
 * i_blocks and i_bytes updates itself are guarded by i_lock acquired directly
 * in inode_add_bytes() and inode_sub_bytes().
 *
 * The spinlock ordering is hence: dq_data_lock > dq_list_lock > i_lock
 *
 * Note that some things (eg. sb pointer, type, id) doesn't change during
 * the life of the dquot structure and so needn't to be protected by a lock
 *
 * Any operation working on dquots via inode pointers must hold dqptr_sem.  If
 * operation is just reading pointers from inode (or not using them at all) the
 * read lock is enough. If pointers are altered function must hold write lock.
 * If operation is holding reference to dquot in other way (e.g. quotactl ops)
 * it must be guarded by dqonoff_sem.
 * This locking assures that:
 *   a) update/access to dquot pointers in inode is serialized
 *   b) everyone is guarded against invalidate_dquots()
 *
 * Each dquot has its dq_lock semaphore. Locked dquots might not be referenced
 * from inodes (dquot_alloc_space() and such don't check the dq_lock).
 * Currently dquot is locked only when it is being read to memory on the first
 * dqget(). Write operations on dquots don't hold dq_lock as they copy data
 * under dq_data_lock spinlock to internal buffers before writing.
 *
 * Lock ordering (including journal_lock) is following:
 *  dqonoff_sem > journal_lock > dqptr_sem > dquot->dq_lock > dqio_sem
 */
spinlock_t dq_list_lock = SPIN_LOCK_UNLOCKED;
spinlock_t dq_data_lock = SPIN_LOCK_UNLOCKED;

static char *quotatypes[] = INITQFNAMES;
static struct quota_format_type *quota_formats;	/* List of registered formats */
static struct quota_module_name module_names[] = INIT_QUOTA_MODULE_NAMES;

int register_quota_format(struct quota_format_type *fmt)
{
	spin_lock(&dq_list_lock);
	fmt->qf_next = quota_formats;
	quota_formats = fmt;
	spin_unlock(&dq_list_lock);
	return 0;
}

void unregister_quota_format(struct quota_format_type *fmt)
{
	struct quota_format_type **actqf;

	spin_lock(&dq_list_lock);
	for (actqf = &quota_formats; *actqf && *actqf != fmt; actqf = &(*actqf)->qf_next);
	if (*actqf)
		*actqf = (*actqf)->qf_next;
	spin_unlock(&dq_list_lock);
}

static struct quota_format_type *find_quota_format(int id)
{
	struct quota_format_type *actqf;

	spin_lock(&dq_list_lock);
	for (actqf = quota_formats; actqf && actqf->qf_fmt_id != id; actqf = actqf->qf_next);
	if (!actqf || !try_module_get(actqf->qf_owner)) {
		int qm;

		spin_unlock(&dq_list_lock);
		
		for (qm = 0; module_names[qm].qm_fmt_id && module_names[qm].qm_fmt_id != id; qm++);
		if (!module_names[qm].qm_fmt_id || request_module(module_names[qm].qm_mod_name))
			return NULL;

		spin_lock(&dq_list_lock);
		for (actqf = quota_formats; actqf && actqf->qf_fmt_id != id; actqf = actqf->qf_next);
		if (actqf && !try_module_get(actqf->qf_owner))
			actqf = NULL;
	}
	spin_unlock(&dq_list_lock);
	return actqf;
}

static void put_quota_format(struct quota_format_type *fmt)
{
	module_put(fmt->qf_owner);
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
 * Unused dquots (dq_count == 0) are added to the free_dquots list when freed,
 * and this list is searched whenever we need an available dquot.  Dquots are
 * removed from the list as soon as they are used again, and
 * dqstats.free_dquots gives the number of dquots on the list. When
 * dquot is invalidated it's completely released from memory.
 *
 * Dquots with a specific identity (device, type and id) are placed on
 * one of the dquot_hash[] hash chains. The provides an efficient search
 * mechanism to locate a specific dquot.
 */

static LIST_HEAD(inuse_list);
static LIST_HEAD(free_dquots);
static struct list_head dquot_hash[NR_DQHASH];

struct dqstats dqstats;

static void dqput(struct dquot *dquot);

static inline int const hashfn(struct super_block *sb, unsigned int id, int type)
{
	return((((unsigned long)sb>>L1_CACHE_SHIFT) ^ id) * (MAXQUOTAS - type)) % NR_DQHASH;
}

/*
 * Following list functions expect dq_list_lock to be held
 */
static inline void insert_dquot_hash(struct dquot *dquot)
{
	struct list_head *head = dquot_hash + hashfn(dquot->dq_sb, dquot->dq_id, dquot->dq_type);
	list_add(&dquot->dq_hash, head);
}

static inline void remove_dquot_hash(struct dquot *dquot)
{
	list_del_init(&dquot->dq_hash);
}

static inline struct dquot *find_dquot(unsigned int hashent, struct super_block *sb, unsigned int id, int type)
{
	struct list_head *head;
	struct dquot *dquot;

	for (head = dquot_hash[hashent].next; head != dquot_hash+hashent; head = head->next) {
		dquot = list_entry(head, struct dquot, dq_hash);
		if (dquot->dq_sb == sb && dquot->dq_id == id && dquot->dq_type == type)
			return dquot;
	}
	return NODQUOT;
}

/* Add a dquot to the tail of the free list */
static inline void put_dquot_last(struct dquot *dquot)
{
	list_add(&dquot->dq_free, free_dquots.prev);
	dqstats.free_dquots++;
}

static inline void remove_free_dquot(struct dquot *dquot)
{
	if (list_empty(&dquot->dq_free))
		return;
	list_del_init(&dquot->dq_free);
	dqstats.free_dquots--;
}

static inline void put_inuse(struct dquot *dquot)
{
	/* We add to the back of inuse list so we don't have to restart
	 * when traversing this list and we block */
	list_add(&dquot->dq_inuse, inuse_list.prev);
	dqstats.allocated_dquots++;
}

static inline void remove_inuse(struct dquot *dquot)
{
	dqstats.allocated_dquots--;
	list_del(&dquot->dq_inuse);
}

static void wait_on_dquot(struct dquot *dquot)
{
	down(&dquot->dq_lock);
	up(&dquot->dq_lock);
}

static int read_dqblk(struct dquot *dquot)
{
	int ret;
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	down(&dquot->dq_lock);
	down(&dqopt->dqio_sem);
	ret = dqopt->ops[dquot->dq_type]->read_dqblk(dquot);
	up(&dqopt->dqio_sem);
	up(&dquot->dq_lock);
	return ret;
}

static int commit_dqblk(struct dquot *dquot)
{
	int ret;
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	down(&dqopt->dqio_sem);
	ret = dqopt->ops[dquot->dq_type]->commit_dqblk(dquot);
	up(&dqopt->dqio_sem);
	return ret;
}

/* Invalidate all dquots on the list. Note that this function is called after
 * quota is disabled so no new quota might be created. Because we hold
 * dqonoff_sem and pointers were already removed from inodes we actually know
 * that no quota for this sb+type should be held. */
static void invalidate_dquots(struct super_block *sb, int type)
{
	struct dquot *dquot;
	struct list_head *head;

	spin_lock(&dq_list_lock);
	for (head = inuse_list.next; head != &inuse_list;) {
		dquot = list_entry(head, struct dquot, dq_inuse);
		head = head->next;
		if (dquot->dq_sb != sb)
			continue;
		if (dquot->dq_type != type)
			continue;
#ifdef __DQUOT_PARANOIA
		if (atomic_read(&dquot->dq_count))
			BUG();
#endif
		/* Quota now has no users and it has been written on last dqput() */
		remove_dquot_hash(dquot);
		remove_free_dquot(dquot);
		remove_inuse(dquot);
		kmem_cache_free(dquot_cachep, dquot);
	}
	spin_unlock(&dq_list_lock);
}

static int vfs_quota_sync(struct super_block *sb, int type)
{
	struct list_head *head;
	struct dquot *dquot;
	struct quota_info *dqopt = sb_dqopt(sb);
	int cnt;

	down(&dqopt->dqonoff_sem);
restart:
	/* At this point any dirty dquot will definitely be written so we can clear
	   dirty flag from info */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if ((cnt == type || type == -1) && sb_has_quota_enabled(sb, cnt))
			clear_bit(DQF_ANY_DQUOT_DIRTY_B, &dqopt->info[cnt].dqi_flags);
	spin_lock(&dq_list_lock);
	list_for_each(head, &inuse_list) {
		dquot = list_entry(head, struct dquot, dq_inuse);
		if (sb && dquot->dq_sb != sb)
			continue;
                if (type != -1 && dquot->dq_type != type)
			continue;
		if (!dquot->dq_sb)	/* Invalidated? */
			continue;
		if (!dquot_dirty(dquot))
			continue;
		atomic_inc(&dquot->dq_count);
		dqstats.lookups++;
		spin_unlock(&dq_list_lock);
		sb->dq_op->write_dquot(dquot);
		dqput(dquot);
		goto restart;
	}
	spin_unlock(&dq_list_lock);

	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if ((cnt == type || type == -1) && sb_has_quota_enabled(sb, cnt) && info_dirty(&dqopt->info[cnt])) {
			down(&dqopt->dqio_sem);
			dqopt->ops[cnt]->write_file_info(sb, cnt);
			up(&dqopt->dqio_sem);
		}
	spin_lock(&dq_list_lock);
	dqstats.syncs++;
	spin_unlock(&dq_list_lock);
	up(&dqopt->dqonoff_sem);

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

/*
 * This is called from kswapd when we think we need some
 * more memory
 */

static int shrink_dqcache_memory(int nr, unsigned int gfp_mask)
{
	int ret;

	spin_lock(&dq_list_lock);
	if (nr)
		prune_dqcache(nr);
	ret = dqstats.allocated_dquots;
	spin_unlock(&dq_list_lock);
	return ret;
}

/*
 * Put reference to dquot
 * NOTE: If you change this function please check whether dqput_blocks() works right...
 * MUST be called with either dqptr_sem or dqonoff_sem held
 */
static void dqput(struct dquot *dquot)
{
	if (!dquot)
		return;
#ifdef __DQUOT_PARANOIA
	if (!atomic_read(&dquot->dq_count)) {
		printk("VFS: dqput: trying to free free dquot\n");
		printk("VFS: device %s, dquot of %s %d\n",
			dquot->dq_sb->s_id,
			quotatypes[dquot->dq_type],
			dquot->dq_id);
		BUG();
	}
#endif
	
	spin_lock(&dq_list_lock);
	dqstats.drops++;
	spin_unlock(&dq_list_lock);
we_slept:
	spin_lock(&dq_list_lock);
	if (atomic_read(&dquot->dq_count) > 1) {
		/* We have more than one user... nothing to do */
		atomic_dec(&dquot->dq_count);
		spin_unlock(&dq_list_lock);
		return;
	}
	if (dquot_dirty(dquot)) {
		spin_unlock(&dq_list_lock);
		dquot->dq_sb->dq_op->write_dquot(dquot);
		goto we_slept;
	}
	atomic_dec(&dquot->dq_count);
#ifdef __DQUOT_PARANOIA
	/* sanity check */
	if (!list_empty(&dquot->dq_free))
		BUG();
#endif
	put_dquot_last(dquot);
	spin_unlock(&dq_list_lock);
}

static struct dquot *get_empty_dquot(struct super_block *sb, int type)
{
	struct dquot *dquot;

	dquot = kmem_cache_alloc(dquot_cachep, SLAB_KERNEL);
	if(!dquot)
		return NODQUOT;

	memset((caddr_t)dquot, 0, sizeof(struct dquot));
	sema_init(&dquot->dq_lock, 1);
	INIT_LIST_HEAD(&dquot->dq_free);
	INIT_LIST_HEAD(&dquot->dq_inuse);
	INIT_LIST_HEAD(&dquot->dq_hash);
	dquot->dq_sb = sb;
	dquot->dq_type = type;
	atomic_set(&dquot->dq_count, 1);

	return dquot;
}

/*
 * Get reference to dquot
 * MUST be called with either dqptr_sem or dqonoff_sem held
 */
static struct dquot *dqget(struct super_block *sb, unsigned int id, int type)
{
	unsigned int hashent = hashfn(sb, id, type);
	struct dquot *dquot, *empty = NODQUOT;

        if (!sb_has_quota_enabled(sb, type))
		return NODQUOT;
we_slept:
	spin_lock(&dq_list_lock);
	if ((dquot = find_dquot(hashent, sb, id, type)) == NODQUOT) {
		if (empty == NODQUOT) {
			spin_unlock(&dq_list_lock);
			if ((empty = get_empty_dquot(sb, type)) == NODQUOT)
				schedule();	/* Try to wait for a moment... */
			goto we_slept;
		}
		dquot = empty;
		dquot->dq_id = id;
		/* all dquots go on the inuse_list */
		put_inuse(dquot);
		/* hash it first so it can be found */
		insert_dquot_hash(dquot);
		dqstats.lookups++;
		spin_unlock(&dq_list_lock);
		read_dqblk(dquot);
	} else {
		if (!atomic_read(&dquot->dq_count))
			remove_free_dquot(dquot);
		atomic_inc(&dquot->dq_count);
		dqstats.cache_hits++;
		dqstats.lookups++;
		spin_unlock(&dq_list_lock);
		wait_on_dquot(dquot);
		if (empty)
			kmem_cache_free(dquot_cachep, empty);
	}

#ifdef __DQUOT_PARANOIA
	if (!dquot->dq_sb)	/* Has somebody invalidated entry under us? */
		BUG();
#endif

	return dquot;
}

static int dqinit_needed(struct inode *inode, int type)
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

/* This routine is guarded by dqonoff_sem semaphore */
static void add_dquot_ref(struct super_block *sb, int type)
{
	struct list_head *p;

restart:
	file_list_lock();
	list_for_each(p, &sb->s_files) {
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
	if (atomic_read(&dquot->dq_count) <= 1 && dquot_dirty(dquot))
		return 1;
	return 0;
}

/* Remove references to dquots from inode - add dquot to list for freeing if needed */
/* We can't race with anybody because we hold dqptr_sem for writing... */
int remove_inode_dquot_ref(struct inode *inode, int type, struct list_head *tofree_head)
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
#ifdef __DQUOT_PARANOIA
			if (atomic_read(&dquot->dq_count) != 1)
				printk(KERN_WARNING "VFS: Adding dquot with dq_count %d to dispose list.\n", atomic_read(&dquot->dq_count));
#endif
			spin_lock(&dq_list_lock);
			list_add(&dquot->dq_free, tofree_head);	/* As dquot must have currently users it can't be on the free list... */
			spin_unlock(&dq_list_lock);
			return 1;
		}
		else
			dqput(dquot);   /* We have guaranteed we won't block */
	}
	return 0;
}

/* Free list of dquots - called from inode.c */
/* dquots are removed from inodes, no new references can be got so we are the only ones holding reference */
static void put_dquot_list(struct list_head *tofree_head)
{
	struct list_head *act_head;
	struct dquot *dquot;

	act_head = tofree_head->next;
	/* So now we have dquots on the list... Just free them */
	while (act_head != tofree_head) {
		dquot = list_entry(act_head, struct dquot, dq_free);
		act_head = act_head->next;
		list_del_init(&dquot->dq_free);	/* Remove dquot from the list so we won't have problems... */
		dqput(dquot);
	}
}

/* Function in inode.c - remove pointers to dquots in icache */
extern void remove_dquot_ref(struct super_block *, int, struct list_head *);

/* Gather all references from inodes and drop them */
static void drop_dquot_ref(struct super_block *sb, int type)
{
	LIST_HEAD(tofree_head);

	down_write(&sb_dqopt(sb)->dqptr_sem);
	remove_dquot_ref(sb, type, &tofree_head);
	up_write(&sb_dqopt(sb)->dqptr_sem);
	put_dquot_list(&tofree_head);
}

static inline void dquot_incr_inodes(struct dquot *dquot, unsigned long number)
{
	dquot->dq_dqb.dqb_curinodes += number;
	mark_dquot_dirty(dquot);
}

static inline void dquot_incr_space(struct dquot *dquot, qsize_t number)
{
	dquot->dq_dqb.dqb_curspace += number;
	mark_dquot_dirty(dquot);
}

static inline void dquot_decr_inodes(struct dquot *dquot, unsigned long number)
{
	if (dquot->dq_dqb.dqb_curinodes > number)
		dquot->dq_dqb.dqb_curinodes -= number;
	else
		dquot->dq_dqb.dqb_curinodes = 0;
	if (dquot->dq_dqb.dqb_curinodes < dquot->dq_dqb.dqb_isoftlimit)
		dquot->dq_dqb.dqb_itime = (time_t) 0;
	clear_bit(DQ_INODES_B, &dquot->dq_flags);
	mark_dquot_dirty(dquot);
}

static inline void dquot_decr_space(struct dquot *dquot, qsize_t number)
{
	if (dquot->dq_dqb.dqb_curspace > number)
		dquot->dq_dqb.dqb_curspace -= number;
	else
		dquot->dq_dqb.dqb_curspace = 0;
	if (toqb(dquot->dq_dqb.dqb_curspace) < dquot->dq_dqb.dqb_bsoftlimit)
		dquot->dq_dqb.dqb_btime = (time_t) 0;
	clear_bit(DQ_BLKS_B, &dquot->dq_flags);
	mark_dquot_dirty(dquot);
}

static inline int need_print_warning(struct dquot *dquot)
{
	switch (dquot->dq_type) {
		case USRQUOTA:
			return current->fsuid == dquot->dq_id;
		case GRPQUOTA:
			return in_group_p(dquot->dq_id);
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
	int flag = (warntype == BHARDWARN || warntype == BSOFTLONGWARN) ? DQ_BLKS_B :
	  ((warntype == IHARDWARN || warntype == ISOFTLONGWARN) ? DQ_INODES_B : 0);

	if (!need_print_warning(dquot) || (flag && test_and_set_bit(flag, &dquot->dq_flags)))
		return;
	tty_write_message(current->signal->tty, dquot->dq_sb->s_id);
	if (warntype == ISOFTWARN || warntype == BSOFTWARN)
		tty_write_message(current->signal->tty, ": warning, ");
	else
		tty_write_message(current->signal->tty, ": write failed, ");
	tty_write_message(current->signal->tty, quotatypes[dquot->dq_type]);
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
	tty_write_message(current->signal->tty, msg);
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
	struct mem_dqinfo *info = &sb_dqopt(dquot->dq_sb)->info[dquot->dq_type];

	return capable(CAP_SYS_RESOURCE) &&
	    (info->dqi_format->qf_fmt_id != QFMT_VFS_OLD || !(info->dqi_flags & V1_DQF_RSQUASH));
}

/* needs dq_data_lock */
static int check_idq(struct dquot *dquot, ulong inodes, char *warntype)
{
	*warntype = NOWARN;
	if (inodes <= 0 || test_bit(DQ_FAKE_B, &dquot->dq_flags))
		return QUOTA_OK;

	if (dquot->dq_dqb.dqb_ihardlimit &&
	   (dquot->dq_dqb.dqb_curinodes + inodes) > dquot->dq_dqb.dqb_ihardlimit &&
            !ignore_hardlimit(dquot)) {
		*warntype = IHARDWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_dqb.dqb_isoftlimit &&
	   (dquot->dq_dqb.dqb_curinodes + inodes) > dquot->dq_dqb.dqb_isoftlimit &&
	    dquot->dq_dqb.dqb_itime && get_seconds() >= dquot->dq_dqb.dqb_itime &&
            !ignore_hardlimit(dquot)) {
		*warntype = ISOFTLONGWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_dqb.dqb_isoftlimit &&
	   (dquot->dq_dqb.dqb_curinodes + inodes) > dquot->dq_dqb.dqb_isoftlimit &&
	    dquot->dq_dqb.dqb_itime == 0) {
		*warntype = ISOFTWARN;
		dquot->dq_dqb.dqb_itime = get_seconds() + sb_dqopt(dquot->dq_sb)->info[dquot->dq_type].dqi_igrace;
	}

	return QUOTA_OK;
}

/* needs dq_data_lock */
static int check_bdq(struct dquot *dquot, qsize_t space, int prealloc, char *warntype)
{
	*warntype = 0;
	if (space <= 0 || test_bit(DQ_FAKE_B, &dquot->dq_flags))
		return QUOTA_OK;

	if (dquot->dq_dqb.dqb_bhardlimit &&
	   toqb(dquot->dq_dqb.dqb_curspace + space) > dquot->dq_dqb.dqb_bhardlimit &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			*warntype = BHARDWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_dqb.dqb_bsoftlimit &&
	   toqb(dquot->dq_dqb.dqb_curspace + space) > dquot->dq_dqb.dqb_bsoftlimit &&
	    dquot->dq_dqb.dqb_btime && get_seconds() >= dquot->dq_dqb.dqb_btime &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			*warntype = BSOFTLONGWARN;
		return NO_QUOTA;
	}

	if (dquot->dq_dqb.dqb_bsoftlimit &&
	   toqb(dquot->dq_dqb.dqb_curspace + space) > dquot->dq_dqb.dqb_bsoftlimit &&
	    dquot->dq_dqb.dqb_btime == 0) {
		if (!prealloc) {
			*warntype = BSOFTWARN;
			dquot->dq_dqb.dqb_btime = get_seconds() + sb_dqopt(dquot->dq_sb)->info[dquot->dq_type].dqi_bgrace;
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
 * Externally referenced functions through dquot_operations in inode.
 *
 * Note: this is a blocking operation.
 */
void dquot_initialize(struct inode *inode, int type)
{
	unsigned int id = 0;
	int cnt;

	/* Solve deadlock when we recurse when holding dqptr_sem... */
	if (IS_NOQUOTA(inode))
		return;
	down_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	/* Having dqptr_sem we know NOQUOTA flags can't be altered... */
	if (IS_NOQUOTA(inode)) {
		up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
		return;
	}
	/* Build list of quotas to initialize... */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
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
			inode->i_dquot[cnt] = dqget(inode->i_sb, id, cnt);
			if (inode->i_dquot[cnt])
				inode->i_flags |= S_QUOTA;
		}
	}
	up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
}

/*
 * 	Release all quotas referenced by inode
 *	Transaction must be started at an entry
 */
void dquot_drop(struct inode *inode)
{
	int cnt;

	down_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	inode->i_flags &= ~S_QUOTA;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] != NODQUOT) {
			dqput(inode->i_dquot[cnt]);
			inode->i_dquot[cnt] = NODQUOT;
		}
	}
	up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
}

/*
 * This operation can block, but only after everything is updated
 */
int dquot_alloc_space(struct inode *inode, qsize_t number, int warn)
{
	int cnt, ret = NO_QUOTA;
	char warntype[MAXQUOTAS];

	/* Solve deadlock when we recurse when holding dqptr_sem... */
	if (IS_NOQUOTA(inode)) {
		inode_add_bytes(inode, number);
		return QUOTA_OK;
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		warntype[cnt] = NOWARN;

	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	spin_lock(&dq_data_lock);
	/* Now recheck reliably when holding dqptr_sem */
	if (IS_NOQUOTA(inode))
		goto add_bytes;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		if (check_bdq(inode->i_dquot[cnt], number, warn, warntype+cnt) == NO_QUOTA)
			goto warn_put_all;
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_space(inode->i_dquot[cnt], number);
	}
add_bytes:
	inode_add_bytes(inode, number);
	ret = QUOTA_OK;
warn_put_all:
	spin_unlock(&dq_data_lock);
	flush_warnings(inode->i_dquot, warntype);
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	return ret;
}

/*
 * This operation can block, but only after everything is updated
 */
int dquot_alloc_inode(const struct inode *inode, unsigned long number)
{
	int cnt, ret = NO_QUOTA;
	char warntype[MAXQUOTAS];

	/* Solve deadlock when we recurse when holding dqptr_sem... */
	if (IS_NOQUOTA(inode))
		return QUOTA_OK;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		warntype[cnt] = NOWARN;
	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	/* Now recheck reliably when holding dqptr_sem */
	if (IS_NOQUOTA(inode)) {
		up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
		return QUOTA_OK;
	}
	spin_lock(&dq_data_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		if (check_idq(inode->i_dquot[cnt], number, warntype+cnt) == NO_QUOTA)
			goto warn_put_all;
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_inodes(inode->i_dquot[cnt], number);
	}
	ret = QUOTA_OK;
warn_put_all:
	spin_unlock(&dq_data_lock);
	flush_warnings((struct dquot **)inode->i_dquot, warntype);
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	return ret;
}

/*
 * This is a non-blocking operation.
 */
void dquot_free_space(struct inode *inode, qsize_t number)
{
	unsigned int cnt;

	/* Solve deadlock when we recurse when holding dqptr_sem... */
	if (IS_NOQUOTA(inode)) {
		inode_sub_bytes(inode, number);
		return;
	}
	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	spin_lock(&dq_data_lock);
	/* Now recheck reliably when holding dqptr_sem */
	if (IS_NOQUOTA(inode))
		goto sub_bytes;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_decr_space(inode->i_dquot[cnt], number);
	}
sub_bytes:
	inode_sub_bytes(inode, number);
	spin_unlock(&dq_data_lock);
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
}

/*
 * This is a non-blocking operation.
 */
void dquot_free_inode(const struct inode *inode, unsigned long number)
{
	unsigned int cnt;

	/* Solve deadlock when we recurse when holding dqptr_sem... */
	if (IS_NOQUOTA(inode))
		return;
	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	/* Now recheck reliably when holding dqptr_sem */
	if (IS_NOQUOTA(inode)) {
		up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
		return;
	}
	spin_lock(&dq_data_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_decr_inodes(inode->i_dquot[cnt], number);
	}
	spin_unlock(&dq_data_lock);
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
}

/*
 * Transfer the number of inode and blocks from one diskquota to an other.
 *
 * This operation can block, but only after everything is updated
 */
int dquot_transfer(struct inode *inode, struct iattr *iattr)
{
	qsize_t space;
	struct dquot *transfer_from[MAXQUOTAS];
	struct dquot *transfer_to[MAXQUOTAS];
	int cnt, ret = NO_QUOTA, chuid = (iattr->ia_valid & ATTR_UID) && inode->i_uid != iattr->ia_uid,
	    chgid = (iattr->ia_valid & ATTR_GID) && inode->i_gid != iattr->ia_gid;
	char warntype[MAXQUOTAS];

	/* Solve deadlock when we recurse when holding dqptr_sem... */
	if (IS_NOQUOTA(inode))
		return QUOTA_OK;
	/* Clear the arrays */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		transfer_to[cnt] = transfer_from[cnt] = NODQUOT;
		warntype[cnt] = NOWARN;
	}
	down(&sb_dqopt(inode->i_sb)->dqonoff_sem);
	down_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	/* Now recheck reliably when holding dqptr_sem */
	if (IS_NOQUOTA(inode)) {	/* File without quota accounting? */
		up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
		up(&sb_dqopt(inode->i_sb)->dqonoff_sem);
		return QUOTA_OK;
	}
	/* First build the transfer_to list - here we can block on reading of dquots... */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
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
	spin_lock(&dq_data_lock);
	space = inode_get_bytes(inode);
	/* Build the transfer_from list and check the limits */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (transfer_to[cnt] == NODQUOT)
			continue;
		transfer_from[cnt] = inode->i_dquot[cnt];
		if (check_idq(transfer_to[cnt], 1, warntype+cnt) == NO_QUOTA ||
		    check_bdq(transfer_to[cnt], space, 0, warntype+cnt) == NO_QUOTA)
			goto warn_put_all;
	}

	/*
	 * Finally perform the needed transfer from transfer_from to transfer_to
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		/*
		 * Skip changes for same uid or gid or for turned off quota-type.
		 */
		if (transfer_to[cnt] == NODQUOT)
			continue;

		dquot_decr_inodes(transfer_from[cnt], 1);
		dquot_decr_space(transfer_from[cnt], space);

		dquot_incr_inodes(transfer_to[cnt], 1);
		dquot_incr_space(transfer_to[cnt], space);

		inode->i_dquot[cnt] = transfer_to[cnt];
	}
	ret = QUOTA_OK;
warn_put_all:
	spin_unlock(&dq_data_lock);
	up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	flush_warnings(transfer_to, warntype);
	
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (ret == QUOTA_OK && transfer_from[cnt] != NODQUOT)
			dqput(transfer_from[cnt]);
		if (ret == NO_QUOTA && transfer_to[cnt] != NODQUOT)
			dqput(transfer_to[cnt]);
	}
	up(&sb_dqopt(inode->i_sb)->dqonoff_sem);
	return ret;
}

/*
 * Definitions of diskquota operations.
 */
struct dquot_operations dquot_operations = {
	.initialize	= dquot_initialize,		/* mandatory */
	.drop		= dquot_drop,			/* mandatory */
	.alloc_space	= dquot_alloc_space,
	.alloc_inode	= dquot_alloc_inode,
	.free_space	= dquot_free_space,
	.free_inode	= dquot_free_inode,
	.transfer	= dquot_transfer,
	.write_dquot	= commit_dqblk
};

/* Function used by filesystems for initializing the dquot_operations structure */
void init_dquot_operations(struct dquot_operations *fsdqops)
{
	memcpy(fsdqops, &dquot_operations, sizeof(dquot_operations));
}

static inline void set_enable_flags(struct quota_info *dqopt, int type)
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

static inline void reset_enable_flags(struct quota_info *dqopt, int type)
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

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */
int vfs_quota_off(struct super_block *sb, int type)
{
	int cnt;
	struct quota_info *dqopt = sb_dqopt(sb);

	if (!sb)
		goto out;

	/* We need to serialize quota_off() for device */
	down(&dqopt->dqonoff_sem);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_enabled(sb, cnt))
			continue;
		reset_enable_flags(dqopt, cnt);

		/* Note: these are blocking operations */
		drop_dquot_ref(sb, cnt);
		invalidate_dquots(sb, cnt);
		/*
		 * Now all dquots should be invalidated, all writes done so we should be only
		 * users of the info. No locks needed.
		 */
		if (info_dirty(&dqopt->info[cnt])) {
			down(&dqopt->dqio_sem);
			dqopt->ops[cnt]->write_file_info(sb, cnt);
			up(&dqopt->dqio_sem);
		}
		if (dqopt->ops[cnt]->free_file_info)
			dqopt->ops[cnt]->free_file_info(sb, cnt);
		put_quota_format(dqopt->info[cnt].dqi_format);

		fput(dqopt->files[cnt]);
		dqopt->files[cnt] = (struct file *)NULL;
		dqopt->info[cnt].dqi_flags = 0;
		dqopt->info[cnt].dqi_igrace = 0;
		dqopt->info[cnt].dqi_bgrace = 0;
		dqopt->ops[cnt] = NULL;
	}
	up(&dqopt->dqonoff_sem);
out:
	return 0;
}

int vfs_quota_on(struct super_block *sb, int type, int format_id, char *path)
{
	struct file *f;
	struct inode *inode;
	struct quota_info *dqopt = sb_dqopt(sb);
	struct quota_format_type *fmt = find_quota_format(format_id);
	int error, cnt;
	struct dquot *to_drop[MAXQUOTAS];
	unsigned int oldflags;

	if (!fmt)
		return -ESRCH;
	f = filp_open(path, O_RDWR, 0600);
	if (IS_ERR(f)) {
		error = PTR_ERR(f);
		goto out_fmt;
	}
	error = -EIO;
	if (!f->f_op || !f->f_op->read || !f->f_op->write)
		goto out_f;
	error = security_quota_on(f);
	if (error)
		goto out_f;
	inode = f->f_dentry->d_inode;
	error = -EACCES;
	if (!S_ISREG(inode->i_mode))
		goto out_f;

	down(&dqopt->dqonoff_sem);
	if (sb_has_quota_enabled(sb, type)) {
		error = -EBUSY;
		goto out_lock;
	}
	oldflags = inode->i_flags;
	dqopt->files[type] = f;
	error = -EINVAL;
	if (!fmt->qf_ops->check_quota_file(sb, type))
		goto out_file_init;
	/* We don't want quota and atime on quota files (deadlocks possible) */
	down_write(&dqopt->dqptr_sem);
	inode->i_flags |= S_NOQUOTA | S_NOATIME;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		to_drop[cnt] = inode->i_dquot[cnt];
		inode->i_dquot[cnt] = NODQUOT;
	}
	inode->i_flags &= ~S_QUOTA;
	up_write(&dqopt->dqptr_sem);
	/* We must put dquots outside of dqptr_sem because we may need to
	 * start transaction for write */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (to_drop[cnt])
			dqput(to_drop[cnt]);
	}

	dqopt->ops[type] = fmt->qf_ops;
	dqopt->info[type].dqi_format = fmt;
	down(&dqopt->dqio_sem);
	if ((error = dqopt->ops[type]->read_file_info(sb, type)) < 0) {
		up(&dqopt->dqio_sem);
		goto out_file_init;
	}
	up(&dqopt->dqio_sem);
	set_enable_flags(dqopt, type);

	add_dquot_ref(sb, type);
	up(&dqopt->dqonoff_sem);

	return 0;

out_file_init:
	inode->i_flags = oldflags;
	dqopt->files[type] = NULL;
out_lock:
	up_write(&dqopt->dqptr_sem);
	up(&dqopt->dqonoff_sem);
out_f:
	filp_close(f, NULL);
out_fmt:
	put_quota_format(fmt);

	return error; 
}

/* Generic routine for getting common part of quota structure */
static void do_get_dqblk(struct dquot *dquot, struct if_dqblk *di)
{
	struct mem_dqblk *dm = &dquot->dq_dqb;

	spin_lock(&dq_data_lock);
	di->dqb_bhardlimit = dm->dqb_bhardlimit;
	di->dqb_bsoftlimit = dm->dqb_bsoftlimit;
	di->dqb_curspace = dm->dqb_curspace;
	di->dqb_ihardlimit = dm->dqb_ihardlimit;
	di->dqb_isoftlimit = dm->dqb_isoftlimit;
	di->dqb_curinodes = dm->dqb_curinodes;
	di->dqb_btime = dm->dqb_btime;
	di->dqb_itime = dm->dqb_itime;
	di->dqb_valid = QIF_ALL;
	spin_unlock(&dq_data_lock);
}

int vfs_get_dqblk(struct super_block *sb, int type, qid_t id, struct if_dqblk *di)
{
	struct dquot *dquot;

	down(&sb_dqopt(sb)->dqonoff_sem);
	if (!(dquot = dqget(sb, id, type))) {
		up(&sb_dqopt(sb)->dqonoff_sem);
		return -ESRCH;
	}
	do_get_dqblk(dquot, di);
	dqput(dquot);
	up(&sb_dqopt(sb)->dqonoff_sem);
	return 0;
}

/* Generic routine for setting common part of quota structure */
static void do_set_dqblk(struct dquot *dquot, struct if_dqblk *di)
{
	struct mem_dqblk *dm = &dquot->dq_dqb;
	int check_blim = 0, check_ilim = 0;

	spin_lock(&dq_data_lock);
	if (di->dqb_valid & QIF_SPACE) {
		dm->dqb_curspace = di->dqb_curspace;
		check_blim = 1;
	}
	if (di->dqb_valid & QIF_BLIMITS) {
		dm->dqb_bsoftlimit = di->dqb_bsoftlimit;
		dm->dqb_bhardlimit = di->dqb_bhardlimit;
		check_blim = 1;
	}
	if (di->dqb_valid & QIF_INODES) {
		dm->dqb_curinodes = di->dqb_curinodes;
		check_ilim = 1;
	}
	if (di->dqb_valid & QIF_ILIMITS) {
		dm->dqb_isoftlimit = di->dqb_isoftlimit;
		dm->dqb_ihardlimit = di->dqb_ihardlimit;
		check_ilim = 1;
	}
	if (di->dqb_valid & QIF_BTIME)
		dm->dqb_btime = di->dqb_btime;
	if (di->dqb_valid & QIF_ITIME)
		dm->dqb_itime = di->dqb_itime;

	if (check_blim) {
		if (!dm->dqb_bsoftlimit || toqb(dm->dqb_curspace) < dm->dqb_bsoftlimit) {
			dm->dqb_btime = 0;
			clear_bit(DQ_BLKS_B, &dquot->dq_flags);
		}
		else if (!(di->dqb_valid & QIF_BTIME))	/* Set grace only if user hasn't provided his own... */
			dm->dqb_btime = get_seconds() + sb_dqopt(dquot->dq_sb)->info[dquot->dq_type].dqi_bgrace;
	}
	if (check_ilim) {
		if (!dm->dqb_isoftlimit || dm->dqb_curinodes < dm->dqb_isoftlimit) {
			dm->dqb_itime = 0;
			clear_bit(DQ_INODES_B, &dquot->dq_flags);
		}
		else if (!(di->dqb_valid & QIF_ITIME))	/* Set grace only if user hasn't provided his own... */
			dm->dqb_itime = get_seconds() + sb_dqopt(dquot->dq_sb)->info[dquot->dq_type].dqi_igrace;
	}
	if (dm->dqb_bhardlimit || dm->dqb_bsoftlimit || dm->dqb_ihardlimit || dm->dqb_isoftlimit)
		clear_bit(DQ_FAKE_B, &dquot->dq_flags);
	else
		set_bit(DQ_FAKE_B, &dquot->dq_flags);
	mark_dquot_dirty(dquot);
	spin_unlock(&dq_data_lock);
}

int vfs_set_dqblk(struct super_block *sb, int type, qid_t id, struct if_dqblk *di)
{
	struct dquot *dquot;

	down(&sb_dqopt(sb)->dqonoff_sem);
	if (!(dquot = dqget(sb, id, type))) {
		up(&sb_dqopt(sb)->dqonoff_sem);
		return -ESRCH;
	}
	do_set_dqblk(dquot, di);
	dqput(dquot);
	up(&sb_dqopt(sb)->dqonoff_sem);
	return 0;
}

/* Generic routine for getting common part of quota file information */
int vfs_get_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii)
{
	struct mem_dqinfo *mi;
  
	down(&sb_dqopt(sb)->dqonoff_sem);
	if (!sb_has_quota_enabled(sb, type)) {
		up(&sb_dqopt(sb)->dqonoff_sem);
		return -ESRCH;
	}
	mi = sb_dqopt(sb)->info + type;
	spin_lock(&dq_data_lock);
	ii->dqi_bgrace = mi->dqi_bgrace;
	ii->dqi_igrace = mi->dqi_igrace;
	ii->dqi_flags = mi->dqi_flags & DQF_MASK;
	ii->dqi_valid = IIF_ALL;
	spin_unlock(&dq_data_lock);
	up(&sb_dqopt(sb)->dqonoff_sem);
	return 0;
}

/* Generic routine for setting common part of quota file information */
int vfs_set_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii)
{
	struct mem_dqinfo *mi;

	down(&sb_dqopt(sb)->dqonoff_sem);
	if (!sb_has_quota_enabled(sb, type)) {
		up(&sb_dqopt(sb)->dqonoff_sem);
		return -ESRCH;
	}
	mi = sb_dqopt(sb)->info + type;
	spin_lock(&dq_data_lock);
	if (ii->dqi_valid & IIF_BGRACE)
		mi->dqi_bgrace = ii->dqi_bgrace;
	if (ii->dqi_valid & IIF_IGRACE)
		mi->dqi_igrace = ii->dqi_igrace;
	if (ii->dqi_valid & IIF_FLAGS)
		mi->dqi_flags = (mi->dqi_flags & ~DQF_MASK) | (ii->dqi_flags & DQF_MASK);
	mark_info_dirty(mi);
	spin_unlock(&dq_data_lock);
	up(&sb_dqopt(sb)->dqonoff_sem);
	return 0;
}

struct quotactl_ops vfs_quotactl_ops = {
	.quota_on	= vfs_quota_on,
	.quota_off	= vfs_quota_off,
	.quota_sync	= vfs_quota_sync,
	.get_info	= vfs_get_dqinfo,
	.set_info	= vfs_set_dqinfo,
	.get_dqblk	= vfs_get_dqblk,
	.set_dqblk	= vfs_set_dqblk
};

static ctl_table fs_dqstats_table[] = {
	{
		.ctl_name	= FS_DQ_LOOKUPS,
		.procname	= "lookups",
		.data		= &dqstats.lookups,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DQ_DROPS,
		.procname	= "drops",
		.data		= &dqstats.drops,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DQ_READS,
		.procname	= "reads",
		.data		= &dqstats.reads,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DQ_WRITES,
		.procname	= "writes",
		.data		= &dqstats.writes,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DQ_CACHE_HITS,
		.procname	= "cache_hits",
		.data		= &dqstats.cache_hits,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DQ_ALLOCATED,
		.procname	= "allocated_dquots",
		.data		= &dqstats.allocated_dquots,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DQ_FREE,
		.procname	= "free_dquots",
		.data		= &dqstats.free_dquots,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DQ_SYNCS,
		.procname	= "syncs",
		.data		= &dqstats.syncs,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 },
};

static ctl_table fs_table[] = {
	{
		.ctl_name	= FS_DQSTATS,
		.procname	= "quota",
		.mode		= 0555,
		.child		= fs_dqstats_table,
	},
	{ .ctl_name = 0 },
};

static ctl_table sys_table[] = {
	{
		.ctl_name	= CTL_FS,
		.procname	= "fs",
		.mode		= 0555,
		.child		= fs_table,
	},
	{ .ctl_name = 0 },
};

/* SLAB cache for dquot structures */
kmem_cache_t *dquot_cachep;

static int __init dquot_init(void)
{
	int i;

	register_sysctl_table(sys_table, 0);
	for (i = 0; i < NR_DQHASH; i++)
		INIT_LIST_HEAD(dquot_hash + i);
	printk(KERN_NOTICE "VFS: Disk quotas %s\n", __DQUOT_VERSION__);

	dquot_cachep = kmem_cache_create("dquot", 
			sizeof(struct dquot), sizeof(unsigned long) * 4,
			SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT, NULL, NULL);
	if (!dquot_cachep)
		panic("Cannot create dquot SLAB cache");

	set_shrinker(DEFAULT_SEEKS, shrink_dqcache_memory);

	return 0;
}
module_init(dquot_init);

EXPORT_SYMBOL(register_quota_format);
EXPORT_SYMBOL(unregister_quota_format);
EXPORT_SYMBOL(dqstats);
EXPORT_SYMBOL(dq_list_lock);
EXPORT_SYMBOL(dq_data_lock);
EXPORT_SYMBOL(init_dquot_operations);
