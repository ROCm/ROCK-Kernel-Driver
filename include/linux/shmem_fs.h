#ifndef __SHMEM_FS_H
#define __SHMEM_FS_H

#include <linux/swap.h>

/* inode in-kernel data */

#define SHMEM_NR_DIRECT 16

extern atomic_t shmem_nrpages;

struct shmem_inode_info {
	spinlock_t		lock;
	struct semaphore 	sem;
	unsigned long		next_index;
	swp_entry_t		i_direct[SHMEM_NR_DIRECT]; /* for the first blocks */
	void		      **i_indirect; /* indirect blocks */
	unsigned long		swapped;
	int			locked;     /* into memory */
	struct list_head	list;
	struct inode		vfs_inode;
};

struct shmem_sb_info {
	unsigned long max_blocks;   /* How many blocks are allowed */
	unsigned long free_blocks;  /* How many are left for allocation */
	unsigned long max_inodes;   /* How many inodes are allowed */
	unsigned long free_inodes;  /* How many are left for allocation */
	spinlock_t    stat_lock;
};

static inline struct shmem_inode_info *SHMEM_I(struct inode *inode)
{
	return container_of(inode, struct shmem_inode_info, vfs_inode);
}

#endif
