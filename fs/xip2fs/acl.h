/*
 *  linux/fs/xip2fs/acl.h, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include <linux/xattr_acl.h>

#define XIP2_ACL_VERSION	0x0001
#define XIP2_ACL_MAX_ENTRIES	32

typedef struct {
	__u16		e_tag;
	__u16		e_perm;
	__u32		e_id;
} xip2_acl_entry;

typedef struct {
	__u16		e_tag;
	__u16		e_perm;
} xip2_acl_entry_short;

typedef struct {
	__u32		a_version;
} xip2_acl_header;

static inline int xip2_acl_count(size_t size)
{
	ssize_t s;
	size -= sizeof(xip2_acl_header);
	s = size - 4 * sizeof(xip2_acl_entry_short);
	if (s < 0) {
		if (size % sizeof(xip2_acl_entry_short))
			return -1;
		return size / sizeof(xip2_acl_entry_short);
	} else {
		if (s % sizeof(xip2_acl_entry))
			return -1;
		return s / sizeof(xip2_acl_entry) + 4;
	}
}

#ifdef CONFIG_XIP2_FS_POSIX_ACL

/* Value for inode->u.ext2_i.i_acl and inode->u.ext2_i.i_default_acl
   if the ACL has not been cached */
#define XIP2_ACL_NOT_CACHED ((void *)-1)

/* acl.c */
extern int xip2_permission (struct inode *, int, struct nameidata *);

extern int init_xip2_acl(void);
extern void exit_xip2_acl(void);

#else
#include <linux/sched.h>
#define xip2_permission NULL
#define xip2_get_acl	NULL
#endif

