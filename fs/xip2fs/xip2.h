/*
 *  linux/fs/xip2fs/xip2.h      , Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include <linux/fs.h>
#include <linux/xip2_fs.h>

/*
 * second extended file system inode data in memory
 */
struct xip2_inode_info {
	__u32	i_data[15];
	__u32	i_flags;
	__u32	i_faddr;
	__u8	i_frag_no;
	__u8	i_frag_size;
	__u16	i_state;
	__u32	i_file_acl;
	__u32	i_dir_acl;
	__u32	i_dtime;

	/*
	 * i_block_group is the number of the block group which contains
	 * this file's inode.  Constant across the lifetime of the inode,
	 * it is ued for making block allocation decisions - we try to
	 * place a file's data blocks near its inode block, and new inodes
	 * near to their parent directory's inode.
	 */
	__u32	i_block_group;

	/*
	 * i_next_alloc_block is the logical (file-relative) number of the
	 * most-recently-allocated block in this file.  Yes, it is misnamed.
	 * We use this for detecting linearly ascending allocation requests.
	 */
	__u32	i_next_alloc_block;

	/*
	 * i_next_alloc_goal is the *physical* companion to i_next_alloc_block.
	 * it the the physical block number of the block which was most-recently
	 * allocated to this file.  This give us the goal (target) for the next
	 * allocation when we detect linearly ascending requests.
	 */
	__u32	i_next_alloc_goal;
	__u32	i_prealloc_block;
	__u32	i_prealloc_count;
	__u32	i_dir_start_lookup;
#ifdef CONFIG_XIP2_FS_XATTR
	/*
	 * Extended attributes can be read independently of the main file
	 * data. Taking i_sem even when reading would cause contention
	 * between readers of EAs and writers of regular file data, so
	 * instead we synchronize on xattr_sem when reading or changing
	 * EAs.
	 */
	struct rw_semaphore xattr_sem;
#endif
#ifdef CONFIG_XIP2_FS_POSIX_ACL
	struct posix_acl	*i_acl;
	struct posix_acl	*i_default_acl;
#endif
	rwlock_t i_meta_lock;
	struct inode	vfs_inode;
};

/*
 * Function prototypes
 */

/*
 * Ok, these declarations are also in <linux/kernel.h> but none of the
 * ext2 source programs needs to include it so they are duplicated here.
 */

static inline struct xip2_inode_info *XIP2_I(struct inode *inode)
{
	return container_of(inode, struct xip2_inode_info, vfs_inode);
}

/* balloc.c */
extern int xip2_bg_has_super(struct super_block *sb, int group);
extern unsigned long xip2_bg_num_gdb(struct super_block *sb, int group);
extern unsigned long xip2_count_free_blocks (struct super_block *);
extern unsigned long xip2_count_dirs (struct super_block *);
extern void xip2_check_blocks_bitmap (struct super_block *);
extern struct ext2_group_desc * xip2_get_group_desc(struct super_block * sb,
						    unsigned int block_group,
						    void ** bh);

/* dir.c */
extern ino_t xip2_inode_by_name(struct inode *, struct dentry *);

/* ialloc.c */
extern unsigned long xip2_count_free_inodes (struct super_block *);
extern void xip2_check_inodes_bitmap (struct super_block *);
extern unsigned long xip2_count_free (void *, unsigned);

/* inode.c */
extern void xip2_read_inode (struct inode *);
extern void xip2_set_inode_flags(struct inode *inode);
extern int  xip2_get_block (struct inode *inode, unsigned long iblock, 
			    sector_t *blockno_result, int create);

/* ioctl.c */
extern int xip2_ioctl (struct inode *, struct file *, unsigned int,
		       unsigned long);

/* super.c */
extern void xip2_error (struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern NORET_TYPE void xip2_panic (struct super_block *, const char *,
				   const char *, ...)
	__attribute__ ((NORET_AND format (printf, 3, 4)));
extern void xip2_warning (struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern void xip2_update_dynamic_rev (struct super_block *sb);
void *xip2_sb_bread (struct super_block *sb, sector_t block);

/*
 * Inodes and files operations
 */

/* dir.c */
extern struct file_operations xip2_dir_operations;

/* file.c */
extern struct inode_operations xip2_file_inode_operations;
extern struct file_operations xip2_file_operations;

/* inode.c */
extern struct address_space_operations xip2_aops;
extern struct address_space_operations xip2_nobh_aops;

/* namei.c */
extern struct inode_operations xip2_dir_inode_operations;
extern struct inode_operations xip2_special_inode_operations;

/* symlink.c */
extern struct inode_operations xip2_fast_symlink_inode_operations;
extern struct inode_operations xip2_symlink_inode_operations;
