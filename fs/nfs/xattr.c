#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/xattr_acl.h>

ssize_t
nfs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	int error=0, pos=0, len=0;

	error = -EOPNOTSUPP;
	if (NFS_PROTO(inode)->version == 3 && NFS_PROTO(inode)->checkacls)
		error = NFS_PROTO(inode)->checkacls(inode);
	if (error < 0)
		return error;
	
#	define output(s) do {						\
			if (pos + sizeof(s) <= size) {			\
				memcpy(buffer + pos, s, sizeof(s));	\
				pos += sizeof(s);			\
			}						\
			len += sizeof(s);				\
		} while(0)

	if (error & ACL_TYPE_ACCESS)
		output("system.posix_acl_access");
	if (error & ACL_TYPE_DEFAULT)
		output("system.posix_acl_default");

#	undef output

	if (!buffer || len <= size)
		return len;
	return -ERANGE;
}

ssize_t
nfs_getxattr(struct dentry *dentry, const char *name, void *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl;
	int type, error = 0;

	if (strcmp(name, XATTR_NAME_ACL_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, XATTR_NAME_ACL_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	acl = ERR_PTR(-EOPNOTSUPP);
	if (NFS_PROTO(inode)->version == 3 && NFS_PROTO(inode)->getacl)
		acl = NFS_PROTO(inode)->getacl(inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	else if (acl) {
		if (type == ACL_TYPE_ACCESS && acl->a_count == 0)
			error = -ENODATA;
		else
			error = posix_acl_to_xattr(acl, buffer, size);
		posix_acl_release(acl);
	} else
		error = -ENODATA;

	return error;
}

int
nfs_setxattr(struct dentry *dentry, const char *name,
	     const void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl;
	int type, error;

	if (strcmp(name, XATTR_NAME_ACL_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, XATTR_NAME_ACL_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	acl = posix_acl_from_xattr(value, size);
	if (IS_ERR(acl))
		return PTR_ERR(acl);

	error = -EOPNOTSUPP;
	if (NFS_PROTO(inode)->version == 3 && NFS_PROTO(inode)->setacl)
		error = NFS_PROTO(inode)->setacl(inode, type, acl);
	posix_acl_release(acl);

	return error;
}

int
nfs_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	int error, type;

	if (strcmp(name, XATTR_NAME_ACL_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, XATTR_NAME_ACL_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	error = -EOPNOTSUPP;
	if (NFS_PROTO(inode)->version == 3 && NFS_PROTO(inode)->setacl)
		error = NFS_PROTO(inode)->setacl(inode, type, NULL);

	return error;
}
