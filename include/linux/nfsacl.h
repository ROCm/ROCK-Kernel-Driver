/*
 * File: linux/nfsacl.h
 *
 * (C) 2003 Andreas Gruenbacher <agruen@suse.de>
 */


#ifndef __LINUX_NFSACL_H
#define __LINUX_NFSACL_H

#include <linux/posix_acl.h>

/* Maximum number of ACL entries over NFS */
#define NFS3_ACL_MAX_ENTRIES	1024

#define NFSACL_MAXWORDS		(2*(2+3*NFS3_ACL_MAX_ENTRIES))
#define NFSACL_MAXPAGES		((2*(8+12*NFS3_ACL_MAX_ENTRIES) + PAGE_SIZE-1) \
				 >> PAGE_SHIFT)

static inline unsigned int
nfsacl_size(struct posix_acl *acl_access, struct posix_acl *acl_default)
{
	unsigned int w = 16;
	w += max(acl_access ? (int)acl_access->a_count : 3, 4) * 12;
	if (acl_default)
		w += max((int)acl_default->a_count, 4) * 12;
	return w;
}

extern unsigned int
nfsacl_encode(struct xdr_buf *buf, unsigned int base, struct inode *inode,
	      struct posix_acl *acl, int encode_entries, int typeflag);
extern unsigned int
nfsacl_decode(struct xdr_buf *buf, unsigned int base, unsigned int *aclcnt,
	      struct posix_acl **pacl);

#endif  /* __LINUX_NFSACL_H */
