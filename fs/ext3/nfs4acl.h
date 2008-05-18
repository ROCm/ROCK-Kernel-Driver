#ifndef __FS_EXT3_NFS4ACL_H
#define __FS_EXT3_NFS4ACL_H

#ifdef CONFIG_EXT3_FS_NFS4ACL

#include <linux/nfs4acl.h>

/* Value for i_nfs4acl if NFS4ACL has not been cached */
#define EXT3_NFS4ACL_NOT_CACHED ((void *)-1)

extern int ext3_nfs4acl_permission(struct inode *, unsigned int);
extern int ext3_may_create(struct inode *, int);
extern int ext3_may_delete(struct inode *, struct inode *);
extern int ext3_nfs4acl_init(handle_t *, struct inode *, struct inode *);
extern int ext3_nfs4acl_chmod(struct inode *);

#else  /* CONFIG_FS_EXT3_NFS4ACL */

#define ext3_may_create NULL
#define ext3_may_delete NULL

static inline int
ext3_nfs4acl_init(handle_t *handle, struct inode *inode, struct inode *dir)
{
	return 0;
}

static inline int
ext3_nfs4acl_chmod(struct inode *inode)
{
	return 0;
}

#endif  /* CONFIG_FS_EXT3_NFS4ACL */

#endif  /* __FS_EXT3_NFS4ACL_H */
