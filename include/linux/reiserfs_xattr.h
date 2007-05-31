/*
  File: linux/reiserfs_xattr.h
*/

#ifndef _LINUX_REISERFS_XATTR_H
#define _LINUX_REISERFS_XATTR_H

#include <linux/types.h>

/* Magic value in header */
#define REISERFS_XATTR_MAGIC 0x52465841	/* "RFXA" */

struct reiserfs_xattr_header {
	__le32 h_magic;		/* magic number for identification */
	__le32 h_hash;		/* hash of the value */
};

struct reiserfs_security_handle {
	char *name;
	void *value;
	size_t length;
};

#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/reiserfs_fs_i.h>
#include <linux/reiserfs_fs.h>

struct inode;
struct dentry;
struct iattr;
struct super_block;
struct nameidata;

#ifdef CONFIG_REISERFS_FS_XATTR
#define is_reiserfs_priv_object(inode) IS_PRIVATE(inode)
#define has_xattr_dir(inode) (REISERFS_I(inode)->i_flags & i_has_xattr_dir)
ssize_t reiserfs_getxattr(struct dentry *dentry, const char *name,
			  void *buffer, size_t size);
int reiserfs_setxattr(struct dentry *dentry, const char *name,
		      const void *value, size_t size, int flags);
ssize_t reiserfs_listxattr(struct dentry *dentry, char *buffer, size_t size);
int reiserfs_removexattr(struct dentry *dentry, const char *name);
int reiserfs_delete_xattrs(struct inode *inode);
int reiserfs_chown_xattrs(struct inode *inode, struct iattr *attrs);
int reiserfs_xattr_init(struct super_block *sb, int mount_flags);
int reiserfs_permission(struct inode *inode, int mask, struct nameidata *nd);

int reiserfs_xattr_get(struct inode *, const char *, void *, size_t);
int reiserfs_xattr_set(struct inode *, const char *, const void *, size_t, int);
int reiserfs_xattr_set_handle(struct reiserfs_transaction_handle *,struct inode *, const char *, const void *, size_t, int);

extern struct xattr_handler reiserfs_xattr_user_handler;
extern struct xattr_handler reiserfs_xattr_trusted_handler;
extern struct xattr_handler reiserfs_xattr_security_handler;
#ifdef CONFIG_REISERFS_FS_SECURITY
int reiserfs_security_init(struct inode *dir, struct inode *inode,
                           struct reiserfs_security_handle *sec);
int reiserfs_security_write(struct reiserfs_transaction_handle *th,
                            struct inode *inode,
			    struct reiserfs_security_handle *sec);
void reiserfs_security_free(struct reiserfs_security_handle *sec);
#endif


static inline void reiserfs_mark_inode_private(struct inode *inode)
{
	inode->i_flags |= S_PRIVATE;
}

#define xattr_size(size) ((size) + sizeof (struct reiserfs_xattr_header))
#define reiserfs_xattr_nblocks(inode, size) (reiserfs_file_data_log(inode) && _ROUND_UP(xattr_size(size), (inode)->i_sb->s_blocksize) >> (inode)->i_sb->s_blocksize_bits)

/* We may have to create up to 3 objects: xattr root, xattr dir, xattr file.
 * Let's try to be smart about it.
 * xattr root: We cache it. If it's not cached, we may need to create it.
 * xattr dir: If anything has been loaded for this inode, we can set a flag
 *            saying so.
 * xattr file: Since we don't cache xattrs, we can't tell. We always include
 *             blocks for it.
 *
 * However, since root and dir can be created between calls - YOU MUST SAVE
 * THIS VALUE.
 */
static inline size_t reiserfs_xattr_jcreate_nblocks(struct inode *inode)
{
	size_t nblocks = JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);

	if ((REISERFS_I(inode)->i_flags & i_has_xattr_dir) == 0) {
		nblocks += JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);
		if (REISERFS_SB(inode->i_sb)->xattr_root == NULL)
			nblocks += JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);
	}

	return nblocks;
}

#else

#define is_reiserfs_priv_object(inode) 0
#define reiserfs_mark_inode_private(inode) do {;} while(0)
#define reiserfs_getxattr NULL
#define reiserfs_setxattr NULL
#define reiserfs_listxattr NULL
#define reiserfs_removexattr NULL

#define reiserfs_permission NULL

static inline int reiserfs_delete_xattrs(struct inode *inode)
{
	return 0;
};
static inline int reiserfs_chown_xattrs(struct inode *inode,
					struct iattr *attrs)
{
	return 0;
};
static inline int reiserfs_xattr_init(struct super_block *sb, int mount_flags)
{
	sb->s_flags = (sb->s_flags & ~MS_POSIXACL);	/* to be sure */
	return 0;
};
static inline void reiserfs_init_xattr_rwsem(struct inode *inode)
{
}
#endif  /*  CONFIG_REISERFS_FS_XATTR  */

#ifndef CONFIG_REISERFS_FS_SECURITY
static inline int reiserfs_security_init(struct inode *dir,
                                         struct inode *inode,
                                         struct reiserfs_security_handle *sec)
{
	return 0;
}
static inline int reiserfs_security_write(struct reiserfs_transaction_handle *th,
                                          struct inode *inode,
					  struct reiserfs_security_handle *sec)
{
	return 0;
}
static inline void reiserfs_security_free(struct reiserfs_security_handle *sec)
{}
#endif

#endif  /*  __KERNEL__  */

#endif  /*  _LINUX_REISERFS_XATTR_H  */
