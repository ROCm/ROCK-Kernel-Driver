/*
 * Definitions for diskquota-operations. When diskquota is configured these
 * macros expand to the right source-code.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: quotaops.h,v 1.2 1998/01/15 16:22:26 ecd Exp $
 *
 */
#ifndef _LINUX_QUOTAOPS_
#define _LINUX_QUOTAOPS_

#include <linux/config.h>
#include <linux/smp_lock.h>

#if defined(CONFIG_QUOTA)

#include <linux/fs.h>

/*
 * declaration of quota_function calls in kernel.
 */
extern void dquot_initialize(struct inode *inode, short type);
extern void dquot_drop(struct inode *inode);
extern int  quota_off(struct super_block *sb, short type);
extern int  sync_dquots(kdev_t dev, short type);

extern int  dquot_alloc_block(struct inode *inode, unsigned long number, char prealloc);
extern int  dquot_alloc_inode(const struct inode *inode, unsigned long number);

extern void dquot_free_block(struct inode *inode, unsigned long number);
extern void dquot_free_inode(const struct inode *inode, unsigned long number);

extern int  dquot_transfer(struct inode *inode, struct iattr *iattr);

/*
 * Operations supported for diskquotas.
 */
#define sb_any_quota_enabled(sb) ((sb)->s_dquot.flags & (DQUOT_USR_ENABLED | DQUOT_GRP_ENABLED))

static __inline__ void DQUOT_INIT(struct inode *inode)
{
	if (!inode->i_sb)
		BUG();
	lock_kernel();
	if (sb_any_quota_enabled(inode->i_sb) && !IS_NOQUOTA(inode))
		inode->i_sb->dq_op->initialize(inode, -1);
	unlock_kernel();
}

static __inline__ void DQUOT_DROP(struct inode *inode)
{
	lock_kernel();
	if (IS_QUOTAINIT(inode)) {
		if (!inode->i_sb)
			BUG();
		inode->i_sb->dq_op->drop(inode);	/* Ops must be set when there's any quota... */
	}
	unlock_kernel();
}

static __inline__ int DQUOT_PREALLOC_BLOCK_NODIRTY(struct inode *inode, int nr)
{
	lock_kernel();
	if (sb_any_quota_enabled(inode->i_sb)) {
		/* Number of used blocks is updated in alloc_block() */
		if (inode->i_sb->dq_op->alloc_block(inode, fs_to_dq_blocks(nr, inode->i_sb->s_blocksize), 1) == NO_QUOTA) {
			unlock_kernel();
			return 1;
		}
	}
	else
		inode->i_blocks += nr << (inode->i_sb->s_blocksize_bits - 9);
	unlock_kernel();
	return 0;
}

static __inline__ int DQUOT_PREALLOC_BLOCK(struct inode *inode, int nr)
{
	int ret;
        if (!(ret =  DQUOT_PREALLOC_BLOCK_NODIRTY(inode, nr)))
		mark_inode_dirty(inode);
	return ret;
}

static __inline__ int DQUOT_ALLOC_BLOCK_NODIRTY(struct inode *inode, int nr)
{
	lock_kernel();
	if (sb_any_quota_enabled(inode->i_sb)) {
		/* Number of used blocks is updated in alloc_block() */
		if (inode->i_sb->dq_op->alloc_block(inode, fs_to_dq_blocks(nr, inode->i_sb->s_blocksize), 0) == NO_QUOTA) {
			unlock_kernel();
			return 1;
		}
	}
	else
		inode->i_blocks += nr << (inode->i_sb->s_blocksize_bits - 9);
	unlock_kernel();
	return 0;
}

static __inline__ int DQUOT_ALLOC_BLOCK(struct inode *inode, int nr)
{
	int ret;
	if (!(ret = DQUOT_ALLOC_BLOCK_NODIRTY(inode, nr)))
		mark_inode_dirty(inode);
	return ret;
}

static __inline__ int DQUOT_ALLOC_INODE(struct inode *inode)
{
	lock_kernel();
	if (sb_any_quota_enabled(inode->i_sb)) {
		DQUOT_INIT(inode);
		if (inode->i_sb->dq_op->alloc_inode(inode, 1) == NO_QUOTA) {
			unlock_kernel();
			return 1;
		}
	}
	unlock_kernel();
	return 0;
}

static __inline__ void DQUOT_FREE_BLOCK_NODIRTY(struct inode *inode, int nr)
{
	lock_kernel();
	if (sb_any_quota_enabled(inode->i_sb))
		inode->i_sb->dq_op->free_block(inode, fs_to_dq_blocks(nr, inode->i_sb->s_blocksize));
	else
		inode->i_blocks -= nr << (inode->i_sb->s_blocksize_bits - 9);
	unlock_kernel();
}

static __inline__ void DQUOT_FREE_BLOCK(struct inode *inode, int nr)
{
	DQUOT_FREE_BLOCK_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
}

static __inline__ void DQUOT_FREE_INODE(struct inode *inode)
{
	lock_kernel();
	if (sb_any_quota_enabled(inode->i_sb))
		inode->i_sb->dq_op->free_inode(inode, 1);
	unlock_kernel();
}

static __inline__ int DQUOT_TRANSFER(struct inode *inode, struct iattr *iattr)
{
	lock_kernel();
	if (sb_any_quota_enabled(inode->i_sb) && !IS_NOQUOTA(inode)) {
		DQUOT_INIT(inode);
		if (inode->i_sb->dq_op->transfer(inode, iattr) == NO_QUOTA) {
			unlock_kernel();
			return 1;
		}
	}
	unlock_kernel();
	return 0;
}

#define DQUOT_SYNC(dev)	sync_dquots(dev, -1)
#define DQUOT_OFF(sb)	quota_off(sb, -1)

#else

/*
 * NO-OP when quota not configured.
 */
#define DQUOT_INIT(inode)			do { } while(0)
#define DQUOT_DROP(inode)			do { } while(0)
#define DQUOT_ALLOC_INODE(inode)		(0)
#define DQUOT_FREE_INODE(inode)			do { } while(0)
#define DQUOT_SYNC(dev)				do { } while(0)
#define DQUOT_OFF(sb)				do { } while(0)
#define DQUOT_TRANSFER(inode, iattr)		(0)
extern __inline__ int DQUOT_PREALLOC_BLOCK_NODIRTY(struct inode *inode, int nr)
{
	lock_kernel();
	inode->i_blocks += nr << (inode->i_sb->s_blocksize_bits - 9);
	unlock_kernel();
	return 0;
}

extern __inline__ int DQUOT_PREALLOC_BLOCK(struct inode *inode, int nr)
{
	DQUOT_PREALLOC_BLOCK_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

extern __inline__ int DQUOT_ALLOC_BLOCK_NODIRTY(struct inode *inode, int nr)
{
	lock_kernel();
	inode->i_blocks += nr << (inode->i_sb->s_blocksize_bits - 9);
	unlock_kernel();
	return 0;
}

extern __inline__ int DQUOT_ALLOC_BLOCK(struct inode *inode, int nr)
{
	DQUOT_ALLOC_BLOCK_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

extern __inline__ void DQUOT_FREE_BLOCK_NODIRTY(struct inode *inode, int nr)
{
	lock_kernel();
	inode->i_blocks -= nr << (inode->i_sb->s_blocksize_bits - 9);
	unlock_kernel();
}

extern __inline__ void DQUOT_FREE_BLOCK(struct inode *inode, int nr)
{
	DQUOT_FREE_BLOCK_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
}	

#endif /* CONFIG_QUOTA */
#endif /* _LINUX_QUOTAOPS_ */
