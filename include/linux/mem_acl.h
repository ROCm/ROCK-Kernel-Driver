#ifndef MEM_ACL_H
#define MEM_ACL_H

#include <linux/posix_acl.h>
#include <linux/xattr_acl.h>

struct mem_acl_operations {
	struct posix_acl *(*getacl)(struct inode *, int);
	void (*setacl)(struct inode *, int, struct posix_acl *);
};

size_t mem_acl_list(struct inode *, struct mem_acl_operations *, int, char *,
		   size_t);
int mem_acl_get(struct inode *, struct mem_acl_operations *, int, void *, size_t);
int mem_acl_set(struct inode *, struct mem_acl_operations *, int, const void *, size_t);
int mem_acl_init(struct inode *, struct inode *, struct mem_acl_operations *);
int mem_acl_chmod(struct inode *, struct mem_acl_operations *);

#endif
