#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/xattr_acl.h>

ssize_t
nfs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl;
	int pos=0, len=0;

#	define output(s) do {						\
			if (pos + sizeof(s) <= size) {			\
				memcpy(buffer + pos, s, sizeof(s));	\
				pos += sizeof(s);			\
			}						\
			len += sizeof(s);				\
		} while(0)

	acl = NFS_PROTO(inode)->getacl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
		output("system.posix_acl_access");
		posix_acl_release(acl);
	}

	if (S_ISDIR(inode->i_mode)) {
		acl = NFS_PROTO(inode)->getacl(inode, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		if (acl) {
			output("system.posix_acl_default");
			posix_acl_release(acl);
		}
	}

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
	error = NFS_PROTO(inode)->setacl(inode, type, acl);
	posix_acl_release(acl);

	return error;
}

int
nfs_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	int type;

	if (strcmp(name, XATTR_NAME_ACL_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, XATTR_NAME_ACL_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	return NFS_PROTO(inode)->setacl(inode, type, NULL);
}
