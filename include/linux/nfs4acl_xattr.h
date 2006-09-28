#ifndef __NFS4ACL_XATTR_H
#define __NFS4ACL_XATTR_H

#include <linux/nfs4acl.h>

#define NFS4ACL_XATTR "system.nfs4acl"

struct nfs4ace_xattr {
	__be16		e_type;
	__be16		e_flags;
	__be32		e_mask;
	__be32		e_id;
	char		e_who[0];
};

struct nfs4acl_xattr {
	unsigned char	a_version;
	unsigned char	a_flags;
	__be16		a_count;
	__be32		a_owner_mask;
	__be32		a_group_mask;
	__be32		a_other_mask;
};

#define ACL4_XATTR_VERSION	0
#define ACL4_XATTR_MAX_COUNT	1024

extern struct nfs4acl *nfs4acl_from_xattr(const void *, size_t);
extern size_t nfs4acl_xattr_size(const struct nfs4acl *acl);
extern void nfs4acl_to_xattr(const struct nfs4acl *, void *);

#endif /* __NFS4ACL_XATTR_H */
