/*
 *  linux/fs/xip2fs/xattr.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


/*
 * Extended attributes are stored on disk blocks allocated outside of
 * any inode. The i_file_acl field is then made to point to this allocated
 * block. If all extended attributes of an inode are identical, these
 * inodes may share the same extended attribute block. Such situations
 * are automatically detected by keeping a cache of recent attribute block
 * numbers and hashes over the block's contents in memory.
 *
 *
 * Extended attribute block layout:
 *
 *   +------------------+
 *   | header           |
 *   ¦ entry 1          | |
 *   | entry 2          | | growing downwards
 *   | entry 3          | v
 *   | four null bytes  |
 *   | . . .            |
 *   | value 1          | ^
 *   | value 3          | | growing upwards
 *   | value 2          | |
 *   +------------------+
 *
 * The block header is followed by multiple entry descriptors. These entry
 * descriptors are variable in size, and alligned to EXT2_XATTR_PAD
 * byte boundaries. The entry descriptors are sorted by attribute name,
 * so that two extended attribute blocks can be compared efficiently.
 *
 * Attribute values are aligned to the end of the block, stored in
 * no specific order. They are also padded to EXT2_XATTR_PAD byte
 * boundaries. No additional gaps are left between them.
 *
 * Locking strategy
 * ----------------
 * XIP2_I(inode)->i_file_acl is protected by XIP2_I(inode)->xattr_sem.
 * EA blocks are only changed if they are exclusive to an inode, so
 * holding xattr_sem also means that nothing but the EA block's reference
 * count will change. Multiple writers to an EA block are synchronized
 * by the bh lock. No more than a single bh lock is held at any time
 * to avoid deadlocks.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mbcache.h>
#include <linux/quotaops.h>
#include <linux/rwsem.h>
#include "xip2.h"
#include "xattr.h"
#include "acl.h"

/* These symbols may be needed by a module. */
EXPORT_SYMBOL(xip2_xattr_register);
EXPORT_SYMBOL(xip2_xattr_unregister);
EXPORT_SYMBOL(xip2_xattr_get);
EXPORT_SYMBOL(xip2_xattr_list);

#define HDR(buffer_ptr) ((struct xip2_xattr_header *)(buffer_ptr))
#define ENTRY(ptr) ((struct xip2_xattr_entry *)(ptr))
#define FIRST_ENTRY(buffer_ptr) ENTRY(HDR(buffer_ptr)+1)
#define IS_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)

#ifdef EXT2_XATTR_DEBUG
# define ea_idebug(inode, f...) do { \
		printk(KERN_DEBUG "inode %s:%ld: ", \
			inode->i_sb->s_id, inode->i_ino); \
		printk(f); \
		printk("\n"); \
	} while (0)
# define ea_bdebug(block_ptr, inode, f...) do { \
		char b[BDEVNAME_SIZE]; \
		printk(KERN_DEBUG "block %s:%lu: ", \
			XIP2_SB(inode->i_sb)->mem_area.name, \
			(unsigned long) block_ptr); \
		printk(f); \
		printk("\n"); \
	} while (0)
#else
# define ea_idebug(f...)
# define ea_bdebug(f...)
#endif

static struct xip2_xattr_handler *xip2_xattr_handlers[XIP2_XATTR_INDEX_MAX];
static rwlock_t xip2_handler_lock = RW_LOCK_UNLOCKED;

int
xip2_xattr_register(int name_index, struct xip2_xattr_handler *handler)
{
	int error = -EINVAL;

	if (name_index > 0 && name_index <= XIP2_XATTR_INDEX_MAX) {
		write_lock(&xip2_handler_lock);
		if (!xip2_xattr_handlers[name_index-1]) {
			xip2_xattr_handlers[name_index-1] = handler;
			error = 0;
		}
		write_unlock(&xip2_handler_lock);
	}
	return error;
}

void
xip2_xattr_unregister(int name_index, struct xip2_xattr_handler *handler)
{
	if (name_index > 0 || name_index <= XIP2_XATTR_INDEX_MAX) {
		write_lock(&xip2_handler_lock);
		xip2_xattr_handlers[name_index-1] = NULL;
		write_unlock(&xip2_handler_lock);
	}
}

static inline const char *
strcmp_prefix(const char *a, const char *a_prefix)
{
	while (*a_prefix && *a == *a_prefix) {
		a++;
		a_prefix++;
	}
	return *a_prefix ? NULL : a;
}

/*
 * Decode the extended attribute name, and translate it into
 * the name_index and name suffix.
 */
static struct xip2_xattr_handler *
xip2_xattr_resolve_name(const char **name)
{
	struct xip2_xattr_handler *handler = NULL;
	int i;

	if (!*name)
		return NULL;
	read_lock(&xip2_handler_lock);
	for (i=0; i<XIP2_XATTR_INDEX_MAX; i++) {
		if (xip2_xattr_handlers[i]) {
			const char *n = strcmp_prefix(*name,
				xip2_xattr_handlers[i]->prefix);
			if (n) {
				handler = xip2_xattr_handlers[i];
				*name = n;
				break;
			}
		}
	}
	read_unlock(&xip2_handler_lock);
	return handler;
}

static inline struct xip2_xattr_handler *
xip2_xattr_handler(int name_index)
{
	struct xip2_xattr_handler *handler = NULL;
	if (name_index > 0 && name_index <= XIP2_XATTR_INDEX_MAX) {
		read_lock(&xip2_handler_lock);
		handler = xip2_xattr_handlers[name_index-1];
		read_unlock(&xip2_handler_lock);
	}
	return handler;
}

/*
 * Inode operation getxattr()
 *
 * dentry->d_inode->i_sem: don't care
 */
ssize_t
xip2_getxattr(struct dentry *dentry, const char *name,
	      void *buffer, size_t size)
{
	struct xip2_xattr_handler *handler;
	struct inode *inode = dentry->d_inode;

	handler = xip2_xattr_resolve_name(&name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->get(inode, name, buffer, size);
}

/*
 * Inode operation listxattr()
 *
 * dentry->d_inode->i_sem: don't care
 */
ssize_t
xip2_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	return xip2_xattr_list(dentry->d_inode, buffer, size);
}

/*
 * xip2_xattr_get()
 *
 * Copy an extended attribute into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
xip2_xattr_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	void *block_ptr = NULL;
	struct xip2_xattr_entry *entry;
	size_t name_len, size;
	char *end;
	int error;

	ea_idebug(inode, "name=%d.%s, buffer=%p, buffer_size=%ld",
		  name_index, name, buffer, (long)buffer_size);

	if (name == NULL)
		return -EINVAL;
	down_read(&XIP2_I(inode)->xattr_sem);
	error = -ENODATA;
	if (!XIP2_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %d", XIP2_I(inode)->i_file_acl);
	block_ptr = xip2_sb_bread(inode->i_sb, XIP2_I(inode)->i_file_acl);
	error = -EIO;
	if (!block_ptr)
		goto cleanup;
	ea_bdebug(block_ptr, inode, "refcount=%d",
		le32_to_cpu(HDR(block_ptr)->h_refcount));
	end = block_ptr + PAGE_SIZE;
	if (HDR(block_ptr)->h_magic != cpu_to_le32(XIP2_XATTR_MAGIC) ||
	    HDR(block_ptr)->h_blocks != cpu_to_le32(1)) {
bad_block:	xip2_error(inode->i_sb, "xip2_xattr_get",
			"inode %ld: bad block %d", inode->i_ino,
			XIP2_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}
	/* find named attribute */
	name_len = strlen(name);

	error = -ERANGE;
	if (name_len > 255)
		goto cleanup;
	entry = FIRST_ENTRY(block_ptr);
	while (!IS_LAST_ENTRY(entry)) {
		struct xip2_xattr_entry *next =
			XIP2_XATTR_NEXT(entry);
		if ((char *)next >= end)
			goto bad_block;
		if (name_index == entry->e_name_index &&
		    name_len == entry->e_name_len &&
		    memcmp(name, entry->e_name, name_len) == 0)
			goto found;
		entry = next;
	}
	/* Check the remaining name entries */
	while (!IS_LAST_ENTRY(entry)) {
		struct xip2_xattr_entry *next =
			XIP2_XATTR_NEXT(entry);
		if ((char *)next >= end)
			goto bad_block;
		entry = next;
	}
	error = -ENODATA;
	goto cleanup;
found:
	/* check the buffer size */
	if (entry->e_value_block != 0)
		goto bad_block;
	size = le32_to_cpu(entry->e_value_size);
	if (size > inode->i_sb->s_blocksize ||
	    le16_to_cpu(entry->e_value_offs) + size > inode->i_sb->s_blocksize)
		goto bad_block;

	if (buffer) {
		error = -ERANGE;
		if (size > buffer_size)
			goto cleanup;
		/* return value of attribute */
		memcpy(buffer, block_ptr + le16_to_cpu(entry->e_value_offs),
			size);
	}
	error = size;

cleanup:
	up_read(&XIP2_I(inode)->xattr_sem);

	return error;
}

/*
 * xip2_xattr_list()
 *
 * Copy a list of attribute names into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
xip2_xattr_list(struct inode *inode, char *buffer, size_t buffer_size)
{
	void *block_ptr = NULL;
	struct xip2_xattr_entry *entry;
	size_t size = 0;
	char *buf, *end;
	int error;

	ea_idebug(inode, "buffer=%p, buffer_size=%ld",
		  buffer, (long)buffer_size);

	down_read(&XIP2_I(inode)->xattr_sem);
	error = 0;
	if (!XIP2_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %d", XIP2_I(inode)->i_file_acl);
	block_ptr = xip2_sb_bread(inode->i_sb, XIP2_I(inode)->i_file_acl);
	error = -EIO;
	if (!block_ptr)
		goto cleanup;
	ea_bdebug(block_ptr, inode, "refcount=%d",
		le32_to_cpu(HDR(block_ptr)->h_refcount));
	end = block_ptr + PAGE_SIZE;
	if (HDR(block_ptr)->h_magic != cpu_to_le32(XIP2_XATTR_MAGIC) ||
	    HDR(block_ptr)->h_blocks != cpu_to_le32(1)) {
bad_block:	xip2_error(inode->i_sb, "xip2_xattr_list",
			"inode %ld: bad block %d", inode->i_ino,
			XIP2_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}
	/* compute the size required for the list of attribute names */
	for (entry = FIRST_ENTRY(block_ptr); !IS_LAST_ENTRY(entry);
	     entry = XIP2_XATTR_NEXT(entry)) {
		struct xip2_xattr_handler *handler;
		struct xip2_xattr_entry *next =
			XIP2_XATTR_NEXT(entry);
		if ((char *)next >= end)
			goto bad_block;

		handler = xip2_xattr_handler(entry->e_name_index);
		if (handler)
			size += handler->list(NULL, inode, entry->e_name,
					      entry->e_name_len);
	}

	if (!buffer) {
		error = size;
		goto cleanup;
	} else {
		error = -ERANGE;
		if (size > buffer_size)
			goto cleanup;
	}

	/* list the attribute names */
	buf = buffer;
	for (entry = FIRST_ENTRY(block_ptr); !IS_LAST_ENTRY(entry);
	     entry = XIP2_XATTR_NEXT(entry)) {
		struct xip2_xattr_handler *handler;
		
		handler = xip2_xattr_handler(entry->e_name_index);
		if (handler)
			buf += handler->list(buf, inode, entry->e_name,
					     entry->e_name_len);
	}
	error = size;

cleanup:
	up_read(&XIP2_I(inode)->xattr_sem);

	return error;
}


#define NAME_HASH_SHIFT 5
#define VALUE_HASH_SHIFT 16

/*
 * xip2_xattr_hash_entry()
 *
 * Compute the hash of an extended attribute.
 */
static inline void xip2_xattr_hash_entry(struct xip2_xattr_header *header,
					 struct xip2_xattr_entry *entry)
{
	__u32 hash = 0;
	char *name = entry->e_name;
	int n;

	for (n=0; n < entry->e_name_len; n++) {
		hash = (hash << NAME_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - NAME_HASH_SHIFT)) ^
		       *name++;
	}

	if (entry->e_value_block == 0 && entry->e_value_size != 0) {
		__u32 *value = (__u32 *)((char *)header +
			le16_to_cpu(entry->e_value_offs));
		for (n = (le32_to_cpu(entry->e_value_size) +
		     XIP2_XATTR_ROUND) >> XIP2_XATTR_PAD_BITS; n; n--) {
			hash = (hash << VALUE_HASH_SHIFT) ^
			       (hash >> (8*sizeof(hash) - VALUE_HASH_SHIFT)) ^
			       le32_to_cpu(*value++);
		}
	}
	entry->e_hash = cpu_to_le32(hash);
}

#undef NAME_HASH_SHIFT
#undef VALUE_HASH_SHIFT

int __init
init_xip2_xattr(void)
{
	int	err;
	
	err = xip2_xattr_register(XIP2_XATTR_INDEX_USER,
				  &xip2_xattr_user_handler);
	if (err)
		return err;
	err = xip2_xattr_register(XIP2_XATTR_INDEX_TRUSTED,
				  &xip2_xattr_trusted_handler);
	if (err)
		goto out;
#ifdef CONFIG_XIP2_FS_SECURITY
	err = xip2_xattr_register(XIP2_XATTR_INDEX_SECURITY,
				  &xip2_xattr_security_handler);
	if (err)
		goto out1;
#endif
#ifdef CONFIG_XIP2_FS_POSIX_ACL
	err = init_xip2_acl();
	if (err)
		goto out2;
#endif
	return 0;
#ifdef CONFIG_XIP2_FS_POSIX_ACL
out2:
#endif
#ifdef CONFIG_XIP2_FS_SECURITY
	xip2_xattr_unregister(XIP2_XATTR_INDEX_SECURITY,
			      &xip2_xattr_security_handler);
out1:
#endif
	xip2_xattr_unregister(XIP2_XATTR_INDEX_TRUSTED,
			      &xip2_xattr_trusted_handler);
out:
	xip2_xattr_unregister(XIP2_XATTR_INDEX_USER,
			      &xip2_xattr_user_handler);
	return err;
}

void
exit_xip2_xattr(void)
{
#ifdef CONFIG_XIP2_FS_POSIX_ACL
	exit_xip2_acl();
#endif
#ifdef CONFIG_XIP2_FS_SECURITY
	xip2_xattr_unregister(XIP2_XATTR_INDEX_SECURITY,
			      &xip2_xattr_security_handler);
#endif
	xip2_xattr_unregister(XIP2_XATTR_INDEX_TRUSTED,
			      &xip2_xattr_trusted_handler);
	xip2_xattr_unregister(XIP2_XATTR_INDEX_USER,
			      &xip2_xattr_user_handler);
}
