#ifndef _AFFS_FS_I
#define _AFFS_FS_I

#include <linux/a.out.h>
#include <linux/time.h>

#define AFFS_MAX_PREALLOC	16	/* MUST be a power of 2 */
#define AFFS_KCSIZE		73	/* Allows for 1 extension block at 512 byte-blocks */

struct key_cache {
	struct timeval	 kc_lru_time;	/* Last time this cache was used */
	s32	 kc_first;		/* First cached key */
	s32	 kc_last;		/* Last cached key */
	s32	 kc_this_key;		/* Key of extension block this data block keys are from */
	int	 kc_this_seq;		/* Sequence number of this extension block */
	s32	 kc_next_key;		/* Key of next extension block */
	s32	 kc_keys[AFFS_KCSIZE];	/* Key cache */
};

#define EC_SIZE	(PAGE_SIZE - 4 * sizeof(struct key_cache) - 4) / 4

struct ext_cache {
	struct key_cache  kc[4];	/* The 4 key caches */
	s32	 ec[EC_SIZE];		/* Keys of assorted extension blocks */
	int	 max_ext;		/* Index of last known extension block */
};

/*
 * affs fs inode data in memory
 */
struct affs_inode_info {
	unsigned long mmu_private;
	u32	 i_protect;			/* unused attribute bits */
	s32	 i_parent;			/* parent ino */
	s32	 i_original;			/* if != 0, this is the key of the original */
	s32	 i_data[AFFS_MAX_PREALLOC];	/* preallocated blocks */
	struct ext_cache *i_ec;			/* Cache gets allocated dynamically */
	int	 i_cache_users;			/* Cache cannot be freed while > 0 */
	int	 i_lastblock;			/* last allocated block */
	short	 i_pa_cnt;			/* number of preallocated blocks */
	short	 i_pa_next;			/* Index of next block in i_data[] */
	short	 i_pa_last;			/* Index of next free slot in i_data[] */
	short	 i_zone;			/* write zone */
	unsigned char i_hlink;			/* This is a fake */
	unsigned char i_pad;
};

#endif
