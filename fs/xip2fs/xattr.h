/*
 *  linux/fs/xip2fs/xattr.h, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/xattr.h>

/* Magic value in attribute blocks */
#define XIP2_XATTR_MAGIC		0xEA020000

/* Maximum number of references to one attribute block */
#define XIP2_XATTR_REFCOUNT_MAX		1024

/* Name indexes */
#define XIP2_XATTR_INDEX_MAX			10
#define XIP2_XATTR_INDEX_USER			1
#define XIP2_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define XIP2_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define XIP2_XATTR_INDEX_TRUSTED		4
#define XIP2_XATTR_INDEX_LUSTRE 		5
#define XIP2_XATTR_INDEX_SECURITY		6

struct xip2_xattr_header {
	__u32	h_magic;	/* magic number for identification */
	__u32	h_refcount;	/* reference count */
	__u32	h_blocks;	/* number of disk blocks used */
	__u32	h_hash;		/* hash value of all attributes */
	__u32	h_reserved[4];	/* zero right now */
};

struct xip2_xattr_entry {
	__u8	e_name_len;	/* length of name */
	__u8	e_name_index;	/* attribute name index */
	__u16	e_value_offs;	/* offset in disk block of value */
	__u32	e_value_block;	/* disk block attribute is stored on (n/i) */
	__u32	e_value_size;	/* size of attribute value */
	__u32	e_hash;		/* hash value of name and value */
	char	e_name[0];	/* attribute name */
};

#define XIP2_XATTR_PAD_BITS		2
#define XIP2_XATTR_PAD		(1<<XIP2_XATTR_PAD_BITS)
#define XIP2_XATTR_ROUND		(XIP2_XATTR_PAD-1)
#define XIP2_XATTR_LEN(name_len) \
	(((name_len) + XIP2_XATTR_ROUND + \
	sizeof(struct xip2_xattr_entry)) & ~XIP2_XATTR_ROUND)
#define XIP2_XATTR_NEXT(entry) \
	( (struct xip2_xattr_entry *)( \
	  (char *)(entry) + XIP2_XATTR_LEN((entry)->e_name_len)) )
#define XIP2_XATTR_SIZE(size) \
	(((size) + XIP2_XATTR_ROUND) & ~XIP2_XATTR_ROUND)

# ifdef CONFIG_XIP2_FS_XATTR

struct xip2_xattr_handler {
	char *prefix;
	size_t (*list)(char *list, struct inode *inode, const char *name,
		       int name_len);
	int (*get)(struct inode *inode, const char *name, void *buffer,
		   size_t size);
	int (*set)(struct inode *inode, const char *name, const void *buffer,
		   size_t size, int flags);
};

extern int xip2_xattr_register(int, struct xip2_xattr_handler *);
extern void xip2_xattr_unregister(int, struct xip2_xattr_handler *);

extern ssize_t xip2_getxattr(struct dentry *, const char *, void *, size_t);
extern ssize_t xip2_listxattr(struct dentry *, char *, size_t);

extern int xip2_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int xip2_xattr_list(struct inode *, char *, size_t);

extern int init_xip2_xattr(void);
extern void exit_xip2_xattr(void);

# else  /* CONFIG_XIP2_FS_XATTR */
#  define xip2_getxattr		NULL
#  define xip2_listxattr	NULL

static inline int
xip2_xattr_get(struct inode *inode, int name_index,
	       const char *name, void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static inline int
xip2_xattr_list(struct inode *inode, char *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static inline int
init_xip2_xattr(void)
{
	return 0;
}

static inline void
exit_xip2_xattr(void)
{
}

# endif  /* CONFIG_XIP2_FS_XATTR */

extern struct xip2_xattr_handler xip2_xattr_user_handler;
extern struct xip2_xattr_handler xip2_xattr_trusted_handler;
extern struct xip2_xattr_handler xip2_xattr_security_handler;

