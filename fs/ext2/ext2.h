#include <linux/fs.h>
#include <linux/ext2_fs.h>

/*
 * second extended file system inode data in memory
 */
struct ext2_inode_info {
	__u32	i_data[15];
	__u32	i_flags;
	__u32	i_faddr;
	__u8	i_frag_no;
	__u8	i_frag_size;
	__u32	i_file_acl;
	__u32	i_dir_acl;
	__u32	i_dtime;
	__u32	i_block_group;
	__u32	i_next_alloc_block;
	__u32	i_next_alloc_goal;
	__u32	i_prealloc_block;
	__u32	i_prealloc_count;
	__u32	i_dir_start_lookup;
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

static inline struct ext2_inode_info *EXT2_I(struct inode *inode)
{
	return container_of(inode, struct ext2_inode_info, vfs_inode);
}

/* balloc.c */
extern int ext2_bg_has_super(struct super_block *sb, int group);
extern unsigned long ext2_bg_num_gdb(struct super_block *sb, int group);
extern int ext2_new_block (struct inode *, unsigned long,
			   __u32 *, __u32 *, int *);
extern void ext2_free_blocks (struct inode *, unsigned long,
			      unsigned long);
extern unsigned long ext2_count_free_blocks (struct super_block *);
extern void ext2_check_blocks_bitmap (struct super_block *);
extern struct ext2_group_desc * ext2_get_group_desc(struct super_block * sb,
						    unsigned int block_group,
						    struct buffer_head ** bh);

/* dir.c */
extern int ext2_add_link (struct dentry *, struct inode *);
extern ino_t ext2_inode_by_name(struct inode *, struct dentry *);
extern int ext2_make_empty(struct inode *, struct inode *);
extern struct ext2_dir_entry_2 * ext2_find_entry (struct inode *,struct dentry *, struct page **);
extern int ext2_delete_entry (struct ext2_dir_entry_2 *, struct page *);
extern int ext2_empty_dir (struct inode *);
extern struct ext2_dir_entry_2 * ext2_dotdot (struct inode *, struct page **);
extern void ext2_set_link(struct inode *, struct ext2_dir_entry_2 *, struct page *, struct inode *);

/* fsync.c */
extern int ext2_sync_file (struct file *, struct dentry *, int);

/* ialloc.c */
extern struct inode * ext2_new_inode (struct inode *, int);
extern void ext2_free_inode (struct inode *);
extern unsigned long ext2_count_free_inodes (struct super_block *);
extern void ext2_check_inodes_bitmap (struct super_block *);
extern unsigned long ext2_count_free (struct buffer_head *, unsigned);

/* inode.c */
extern void ext2_read_inode (struct inode *);
extern void ext2_write_inode (struct inode *, int);
extern void ext2_put_inode (struct inode *);
extern void ext2_delete_inode (struct inode *);
extern int ext2_sync_inode (struct inode *);
extern void ext2_discard_prealloc (struct inode *);
extern void ext2_truncate (struct inode *);

/* ioctl.c */
extern int ext2_ioctl (struct inode *, struct file *, unsigned int,
		       unsigned long);

/* super.c */
extern void ext2_error (struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern NORET_TYPE void ext2_panic (struct super_block *, const char *,
				   const char *, ...)
	__attribute__ ((NORET_AND format (printf, 3, 4)));
extern void ext2_warning (struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern void ext2_update_dynamic_rev (struct super_block *sb);
extern void ext2_write_super (struct super_block *);

/*
 * Inodes and files operations
 */

/* dir.c */
extern struct file_operations ext2_dir_operations;

/* file.c */
extern struct inode_operations ext2_file_inode_operations;
extern struct file_operations ext2_file_operations;

/* inode.c */
extern struct address_space_operations ext2_aops;

/* namei.c */
extern struct inode_operations ext2_dir_inode_operations;

/* symlink.c */
extern struct inode_operations ext2_fast_symlink_inode_operations;
