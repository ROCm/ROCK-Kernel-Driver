#ifndef _AFFS_FS_H
#define _AFFS_FS_H
/*
 * The affs filesystem constants/structures
 */

#include <linux/types.h>

#define AFFS_SUPER_MAGIC 0xadff

/* Get the filesystem block size given an inode. */
#define AFFS_I2BSIZE(inode) ((inode)->i_sb->s_blocksize)

/* Get the filesystem hash table size given an inode. */
#define AFFS_I2HSIZE(inode) ((inode)->i_sb->u.affs_sb.s_hashsize)

/* Get the block number bits given an inode */
#define AFFS_I2BITS(inode) ((inode)->i_sb->s_blocksize_bits)

/* Get the fs type given an inode */
#define AFFS_I2FSTYPE(inode) ((inode)->i_sb->u.affs_sb.s_flags & SF_INTL)

struct DateStamp
{
  u32 ds_Days;
  u32 ds_Minute;
  u32 ds_Tick;
};

/* --- Prototypes -----------------------------------------------------------------------------	*/

/* amigaffs.c */

extern int	affs_get_key_entry(int bsize, void *data, int entry_pos);
extern int	affs_get_file_name(int bsize, void *fh_data, unsigned char **name);
extern u32	affs_checksum_block(int bsize, void *data, s32 *ptype, s32 *stype);
extern void	affs_fix_checksum(int bsize, void *data, int cspos);
extern void	secs_to_datestamp(time_t secs, struct DateStamp *ds);
extern int	prot_to_mode(unsigned int prot);
extern u32	mode_to_prot(int mode);
extern int	affs_insert_hash(unsigned long dir_ino, struct buffer_head *header,
			struct inode *inode);
extern int	affs_remove_hash(struct buffer_head *bh, struct inode *inode);
extern int	affs_remove_link(struct buffer_head *bh, struct inode *inode);
extern int	affs_remove_header(struct buffer_head *bh, struct inode *inode);
extern void	affs_error(struct super_block *sb, const char *function, const char *fmt, ...);
extern void	affs_warning(struct super_block *sb, const char *function, const char *fmt, ...);
extern int	affs_check_name(const unsigned char *name, int len);
extern int	affs_copy_name(unsigned char *bstr, const unsigned char *name);

/* bitmap. c */

extern int	affs_count_free_blocks(struct super_block *s);
extern int	affs_count_free_bits(int blocksize, const char *data);
extern void	affs_free_block(struct super_block *sb, s32 block);
extern s32	affs_new_header(struct inode *inode);
extern s32	affs_new_data(struct inode *inode);
extern void	affs_make_zones(struct super_block *sb);

/* namei.c */

extern int	affs_hash_name(const unsigned char *name, int len, int intl, int hashsize);
extern struct dentry *affs_lookup(struct inode *dir, struct dentry *dentry);
extern int	affs_unlink(struct inode *dir, struct dentry *dentry);
extern int	affs_create(struct inode *dir, struct dentry *dentry, int mode);
extern int	affs_mkdir(struct inode *dir, struct dentry *dentry, int mode);
extern int	affs_rmdir(struct inode *dir, struct dentry *dentry);
extern int	affs_link(struct dentry *olddentry, struct inode *dir,
			  struct dentry *dentry);
extern int	affs_symlink(struct inode *dir, struct dentry *dentry,
			     const char *symname);
extern int	affs_rename(struct inode *old_dir, struct dentry *old_dentry,
			    struct inode *new_dir, struct dentry *new_dentry);

/* inode.c */

extern struct buffer_head	*affs_bread(kdev_t dev, int block, int size);
extern void			 affs_brelse(struct buffer_head *buf);
extern unsigned long		 affs_parent_ino(struct inode *dir);
extern struct inode		*affs_new_inode(const struct inode *dir);
extern int			 affs_notify_change(struct dentry *dentry, struct iattr *attr);
extern int			 affs_add_entry(struct inode *dir, struct inode *link,
					  struct inode *inode, struct dentry *dentry, s32 type);
extern void			 affs_put_inode(struct inode *inode);
extern void			 affs_delete_inode(struct inode *inode);
extern void			 affs_read_inode(struct inode *inode);
extern void			 affs_write_inode(struct inode *inode, int);

/* super.c */

extern int			 affs_fs(void);

/* file.c */

void		affs_free_prealloc(struct inode *inode);
extern void	affs_truncate(struct inode *);

/* dir.c */

extern void   affs_dir_truncate(struct inode *);

/* jump tables */

extern struct inode_operations	 affs_file_inode_operations;
extern struct inode_operations	 affs_dir_inode_operations;
extern struct inode_operations   affs_symlink_inode_operations;
extern struct file_operations	 affs_file_operations;
extern struct file_operations	 affs_file_operations_ofs;
extern struct file_operations	 affs_dir_operations;
extern struct address_space_operations	 affs_symlink_aops;
extern struct address_space_operations	 affs_aops;

extern struct dentry_operations	 affs_dentry_operations;
extern struct dentry_operations	 affs_dentry_operations_intl;

extern int affs_bmap(struct inode *, int);
#endif
