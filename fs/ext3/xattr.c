/*
 * linux/fs/ext3/xattr.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 *
 * Fix by Harrison Xing <harrison@mountainviewdata.com>.
 * Ext3 code with a lot of help from Eric Jarman <ejarman@acm.org>.
 * Extended attributes for symlinks and special files added per
 *  suggestion of Luka Renko <luka.renko@hermes.si>.
 * xattr consolidation Copyright (c) 2004 James Morris <jmorris@redhat.com>,
 *  Red Hat Inc.
 * ea-in-inode support by Alex Tomas <alex@clusterfs.com> aka bzzz
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
 * descriptors are variable in size, and alligned to EXT3_XATTR_PAD
 * byte boundaries. The entry descriptors are sorted by attribute name,
 * so that two extended attribute blocks can be compared efficiently.
 *
 * Attribute values are aligned to the end of the block, stored in
 * no specific order. They are also padded to EXT3_XATTR_PAD byte
 * boundaries. No additional gaps are left between them.
 *
 * Locking strategy
 * ----------------
 * EXT3_I(inode)->i_file_acl is protected by EXT3_I(inode)->xattr_sem.
 * EA blocks are only changed if they are exclusive to an inode, so
 * holding xattr_sem also means that nothing but the EA block's reference
 * count will change. Multiple writers to an EA block are synchronized
 * by the bh lock. No more than a single bh lock is held at any time
 * to avoid deadlocks.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include <linux/mbcache.h>
#include <linux/quotaops.h>
#include <linux/rwsem.h>
#include "xattr.h"
#include "acl.h"

#define HDR(bh) ((struct ext3_xattr_header *)((bh)->b_data))
#define ENTRY(ptr) ((struct ext3_xattr_entry *)(ptr))
#define FIRST_ENTRY(bh) ENTRY(HDR(bh)+1)
#define IS_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)

#ifdef EXT3_XATTR_DEBUG
# define ea_idebug(inode, f...) do { \
		printk(KERN_DEBUG "inode %s:%ld: ", \
			inode->i_sb->s_id, inode->i_ino); \
		printk(f); \
		printk("\n"); \
	} while (0)
# define ea_bdebug(bh, f...) do { \
		char b[BDEVNAME_SIZE]; \
		printk(KERN_DEBUG "block %s:%lu: ", \
			bdevname(bh->b_bdev, b), \
			(unsigned long) bh->b_blocknr); \
		printk(f); \
		printk("\n"); \
	} while (0)
#else
# define ea_idebug(f...)
# define ea_bdebug(f...)
#endif

static int ext3_xattr_set_handle2(handle_t *handle, struct inode *inode,
				       struct buffer_head *old_bh,
				       struct ext3_xattr_header *header);
static int ext3_xattr_cache_insert(struct buffer_head *);
static struct buffer_head *ext3_xattr_cache_find(handle_t *, struct inode *,
						 struct ext3_xattr_header *,
						 int *);
static void ext3_xattr_cache_remove(struct buffer_head *);
static void ext3_xattr_rehash(struct ext3_xattr_header *,
			      struct ext3_xattr_entry *);

static struct mb_cache *ext3_xattr_cache;

static struct xattr_handler *ext3_xattr_handler_map[EXT3_XATTR_INDEX_MAX] = {
	[EXT3_XATTR_INDEX_USER]		     = &ext3_xattr_user_handler,
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	[EXT3_XATTR_INDEX_POSIX_ACL_ACCESS]  = &ext3_xattr_acl_access_handler,
	[EXT3_XATTR_INDEX_POSIX_ACL_DEFAULT] = &ext3_xattr_acl_default_handler,
#endif
	[EXT3_XATTR_INDEX_TRUSTED]	     = &ext3_xattr_trusted_handler,
#ifdef CONFIG_EXT3_FS_SECURITY
	[EXT3_XATTR_INDEX_SECURITY]	     = &ext3_xattr_security_handler,
#endif
};

struct xattr_handler *ext3_xattr_handlers[] = {
	&ext3_xattr_user_handler,
	&ext3_xattr_trusted_handler,
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	&ext3_xattr_acl_access_handler,
	&ext3_xattr_acl_default_handler,
#endif
#ifdef CONFIG_EXT3_FS_SECURITY
	&ext3_xattr_security_handler,
#endif
	NULL
};

static inline struct xattr_handler *
ext3_xattr_handler(int name_index)
{
	struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index <= EXT3_XATTR_INDEX_MAX)
		handler = ext3_xattr_handler_map[name_index];
	return handler;
}

/*
 * Inode operation listxattr()
 *
 * dentry->d_inode->i_sem: don't care
 */
ssize_t
ext3_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	return ext3_xattr_list(dentry->d_inode, buffer, size);
}

/*
 * ext3_xattr_block_get()
 *
 * routine looks for attribute in EA block and returns it's value and size
 */
int
ext3_xattr_block_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	struct buffer_head *bh = NULL;
	struct ext3_xattr_entry *entry;
	size_t name_len, size;
	char *end;
	int error;

	ea_idebug(inode, "name=%d.%s, buffer=%p, buffer_size=%ld",
		  name_index, name, buffer, (long)buffer_size);

	if (name == NULL)
		return -EINVAL;
	error = -ENODATA;
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %d", EXT3_I(inode)->i_file_acl);
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	error = -EIO;
	if (!bh)
		goto cleanup;
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(HDR(bh)->h_refcount));
	end = bh->b_data + bh->b_size;
	if (HDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    HDR(bh)->h_blocks != cpu_to_le32(1)) {
bad_block:	ext3_error(inode->i_sb, "ext3_xattr_get",
			"inode %ld: bad block %d", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}
	/* find named attribute */
	name_len = strlen(name);

	error = -ERANGE;
	if (name_len > 255)
		goto cleanup;
	entry = FIRST_ENTRY(bh);
	while (!IS_LAST_ENTRY(entry)) {
		struct ext3_xattr_entry *next =
			EXT3_XATTR_NEXT(entry);
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
		struct ext3_xattr_entry *next =
			EXT3_XATTR_NEXT(entry);
		if ((char *)next >= end)
			goto bad_block;
		entry = next;
	}
	if (ext3_xattr_cache_insert(bh))
		ea_idebug(inode, "cache insert failed");
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

	if (ext3_xattr_cache_insert(bh))
		ea_idebug(inode, "cache insert failed");
	if (buffer) {
		error = -ERANGE;
		if (size > buffer_size)
			goto cleanup;
		/* return value of attribute */
		memcpy(buffer, bh->b_data + le16_to_cpu(entry->e_value_offs),
			size);
	}
	error = size;

cleanup:
	brelse(bh);

	return error;
}

/*
 * ext3_xattr_ibody_get()
 *
 * routine looks for attribute in inode body and returns it's value and size
 */
int
ext3_xattr_ibody_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	int size, name_len = strlen(name), storage_size;
	struct ext3_xattr_entry *last;
	struct ext3_inode *raw_inode;
	struct ext3_iloc iloc;
	char *start, *end;
	int ret = -ENOENT;

	if (EXT3_SB(inode->i_sb)->s_inode_size <= EXT3_GOOD_OLD_INODE_SIZE)
		return -ENOENT;

	ret = ext3_get_inode_loc(inode, &iloc, 1);
	if (ret)
		return ret;
	raw_inode = ext3_raw_inode(&iloc);

	storage_size = EXT3_SB(inode->i_sb)->s_inode_size -
				EXT3_GOOD_OLD_INODE_SIZE -
				EXT3_I(inode)->i_extra_isize -
				sizeof(__u32);
	start = (char *) raw_inode + EXT3_GOOD_OLD_INODE_SIZE +
			EXT3_I(inode)->i_extra_isize;
	if (le32_to_cpu((*(__u32*) start)) != EXT3_XATTR_MAGIC) {
		brelse(iloc.bh);
		return -ENOENT;
	}
	start += sizeof(__u32);
	end = (char *) raw_inode + EXT3_SB(inode->i_sb)->s_inode_size;

	last = (struct ext3_xattr_entry *) start;
	while (!IS_LAST_ENTRY(last)) {
		struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(last);
		if (le32_to_cpu(last->e_value_size) > storage_size ||
				(char *) next >= end) {
			ext3_error(inode->i_sb, "ext3_xattr_ibody_get",
				"inode %ld", inode->i_ino);
			brelse(iloc.bh);
			return -EIO;
		}
		if (name_index == last->e_name_index &&
		    name_len == last->e_name_len &&
		    !memcmp(name, last->e_name, name_len))
			goto found;
		last = next;
	}

	/* can't find EA */
	brelse(iloc.bh);
	return -ENOENT;

found:
	size = le32_to_cpu(last->e_value_size);
	if (buffer) {
		ret = -ERANGE;
		if (buffer_size >= size) {
			memcpy(buffer, start + le16_to_cpu(last->e_value_offs),
				size);
			ret = size;
		}
	} else
		ret = size;
	brelse(iloc.bh);
	return ret;
}

/*
 * ext3_xattr_get()
 *
 * Copy an extended attribute into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
ext3_xattr_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	int err;

	down_read(&EXT3_I(inode)->xattr_sem);

	/* try to find attribute in inode body */
	err = ext3_xattr_ibody_get(inode, name_index, name,
					buffer, buffer_size);
	if (err < 0)
		/* search was unsuccessful, try to find EA in dedicated block */
		err = ext3_xattr_block_get(inode, name_index, name,
				buffer, buffer_size);
	up_read(&EXT3_I(inode)->xattr_sem);

	return err;
}

/* ext3_xattr_block_list()
 *
 * generate list of attributes stored in EA block
 */
int
ext3_xattr_block_list(struct inode *inode, char *buffer, size_t buffer_size)
{
	struct buffer_head *bh = NULL;
	struct ext3_xattr_entry *entry;
	char *end;
	size_t rest = buffer_size;
	int error;

	ea_idebug(inode, "buffer=%p, buffer_size=%ld",
		  buffer, (long)buffer_size);

	error = 0;
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %d", EXT3_I(inode)->i_file_acl);
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	error = -EIO;
	if (!bh)
		goto cleanup;
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		(int) atomic_read(&(bh->b_count)), (int) le32_to_cpu(HDR(bh)->h_refcount));
	end = bh->b_data + bh->b_size;
	if (HDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    HDR(bh)->h_blocks != cpu_to_le32(1)) {
bad_block:	ext3_error(inode->i_sb, "ext3_xattr_list",
			"inode %ld: bad block %d", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}

	/* check the on-disk data structure */
	entry = FIRST_ENTRY(bh);
	while (!IS_LAST_ENTRY(entry)) {
		struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(entry);

		if ((char *)next >= end)
			goto bad_block;
		entry = next;
	}
	if (ext3_xattr_cache_insert(bh))
		ea_idebug(inode, "cache insert failed");

	/* list the attribute names */
	for (entry = FIRST_ENTRY(bh); !IS_LAST_ENTRY(entry);
	     entry = EXT3_XATTR_NEXT(entry)) {
		struct xattr_handler *handler =
			ext3_xattr_handler(entry->e_name_index);

		if (handler) {
			size_t size = handler->list(inode, buffer, rest,
						    entry->e_name,
						    entry->e_name_len);
			if (buffer) {
				if (size > rest) {
					error = -ERANGE;
					goto cleanup;
				}
				buffer += size;
			}
			rest -= size;
		}
	}
	error = buffer_size - rest;  /* total size */

cleanup:
	brelse(bh);

	return error;
}

/* ext3_xattr_ibody_list()
 *
 * generate list of attributes stored in inode body
 */
int
ext3_xattr_ibody_list(struct inode *inode, char *buffer, size_t buffer_size)
{
	struct ext3_xattr_entry *last;
	struct ext3_inode *raw_inode;
	size_t rest = buffer_size;
	struct ext3_iloc iloc;
	char *start, *end;
	int storage_size;
	int size = 0;
	int ret;

	if (EXT3_SB(inode->i_sb)->s_inode_size <= EXT3_GOOD_OLD_INODE_SIZE)
		return 0;

	ret = ext3_get_inode_loc(inode, &iloc, 1);
	if (ret)
		return ret;
	raw_inode = ext3_raw_inode(&iloc);

	storage_size = EXT3_SB(inode->i_sb)->s_inode_size -
				EXT3_GOOD_OLD_INODE_SIZE -
				EXT3_I(inode)->i_extra_isize -
				sizeof(__u32);
	start = (char *) raw_inode + EXT3_GOOD_OLD_INODE_SIZE +
			EXT3_I(inode)->i_extra_isize;
	if (le32_to_cpu((*(__u32*) start)) != EXT3_XATTR_MAGIC)
		goto cleanup;
	start += sizeof(__u32);
	end = (char *) raw_inode + EXT3_SB(inode->i_sb)->s_inode_size;

	last = (struct ext3_xattr_entry *) start;
	while (!IS_LAST_ENTRY(last)) {
		struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(last);
		if ((char *) next >= end) {
			ext3_error(inode->i_sb, "ext3_xattr_ibody_list",
					"inode %ld", inode->i_ino);
			ret = -EIO;
			goto cleanup;
		}
		last = next;
	}

	last = (struct ext3_xattr_entry *) start;
	for (; !IS_LAST_ENTRY(last); last = EXT3_XATTR_NEXT(last)) {
		struct xattr_handler *handler =
			ext3_xattr_handler(last->e_name_index);

		if (!handler)
			continue;

		size += handler->list(inode, buffer, rest, last->e_name,
					last->e_name_len);
		if (buffer) {
			if (size > rest) {
				ret = -ERANGE;
				goto cleanup;
			}
			buffer += size;
		}
		rest -= size;
	}
	ret = buffer_size - rest; /* total size */

cleanup:
	brelse(iloc.bh);
	return ret;
}

/*
 * ext3_xattr_list()
 *
 * Copy a list of attribute names into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
ext3_xattr_list(struct inode *inode, char *buffer, size_t buffer_size)
{
	int size = buffer_size;
	int error;

	down_read(&EXT3_I(inode)->xattr_sem);

	/* get list of attributes stored in inode body */
	error = ext3_xattr_ibody_list(inode, buffer, buffer_size);
	if (error < 0) {
		/* some error occured while collecting
		 * attributes in inode body */
		size = 0;
		goto cleanup;
	}
	size = error;

	/* get list of attributes stored in dedicated block */
	if (buffer) {
		buffer_size -= error;
		if (buffer_size <= 0) {
			buffer = NULL;
			buffer_size = 0;
		} else
			buffer += error;
	}

	error = ext3_xattr_block_list(inode, buffer, buffer_size);
	/* listing was successful, so we return len */
	if (error < 0)
		size = 0;

cleanup:
	up_read(&EXT3_I(inode)->xattr_sem);
	return error + size;
}

/*
 * If the EXT3_FEATURE_COMPAT_EXT_ATTR feature of this file system is
 * not set, set it.
 */
static void ext3_xattr_update_super_block(handle_t *handle,
					  struct super_block *sb)
{
	if (EXT3_HAS_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_EXT_ATTR))
		return;

	lock_super(sb);
	if (ext3_journal_get_write_access(handle, EXT3_SB(sb)->s_sbh) == 0) {
		EXT3_SB(sb)->s_es->s_feature_compat |=
			cpu_to_le32(EXT3_FEATURE_COMPAT_EXT_ATTR);
		sb->s_dirt = 1;
		ext3_journal_dirty_metadata(handle, EXT3_SB(sb)->s_sbh);
	}
	unlock_super(sb);
}

/*
 * ext3_xattr_ibody_find()
 *
 * search attribute and calculate free space in inode body
 * NOTE: free space includes space our attribute hold
 */
int
ext3_xattr_ibody_find(struct inode *inode, int name_index,
			const char *name, int *free)
{
	struct ext3_xattr_entry *last;
	struct ext3_inode *raw_inode;
	int name_len = strlen(name);
	int err, storage_size;
	struct ext3_iloc iloc;
	char *start, *end;
	int ret = -ENOENT;

	*free = 0;
	if (EXT3_SB(inode->i_sb)->s_inode_size <= EXT3_GOOD_OLD_INODE_SIZE)
		return ret;

	err = ext3_get_inode_loc(inode, &iloc, 1);
	if (err)
		return -EIO;
	raw_inode = ext3_raw_inode(&iloc);

	storage_size = EXT3_SB(inode->i_sb)->s_inode_size -
				EXT3_GOOD_OLD_INODE_SIZE -
				EXT3_I(inode)->i_extra_isize -
				sizeof(__u32);
	*free = storage_size - sizeof(__u32);
	start = (char *) raw_inode + EXT3_GOOD_OLD_INODE_SIZE +
			EXT3_I(inode)->i_extra_isize;
	if (le32_to_cpu((*(__u32*) start)) != EXT3_XATTR_MAGIC) {
		brelse(iloc.bh);
		return -ENOENT;
	}
	start += sizeof(__u32);
	end = (char *) raw_inode + EXT3_SB(inode->i_sb)->s_inode_size;

	last = (struct ext3_xattr_entry *) start;
	while (!IS_LAST_ENTRY(last)) {
		struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(last);
		if (le32_to_cpu(last->e_value_size) > storage_size ||
				(char *) next >= end) {
			ext3_error(inode->i_sb, "ext3_xattr_ibody_find",
				"inode %ld", inode->i_ino);
			brelse(iloc.bh);
			return -EIO;
		}

		if (name_index == last->e_name_index &&
		    name_len == last->e_name_len &&
		    !memcmp(name, last->e_name, name_len)) {
			ret = 0;
		} else {
			*free -= EXT3_XATTR_LEN(last->e_name_len);
			*free -= le32_to_cpu(last->e_value_size);
		}
		last = next;
	}

	brelse(iloc.bh);
	return ret;
}

/*
 * ext3_xattr_block_find()
 *
 * search attribute and calculate free space in EA block (if it allocated)
 * NOTE: free space includes space our attribute hold
 */
int
ext3_xattr_block_find(struct inode *inode, int name_index,
			const char *name, int *free)
{
	struct buffer_head *bh = NULL;
	struct ext3_xattr_entry *entry;
	char *end;
	int name_len, error = -ENOENT;

	if (!EXT3_I(inode)->i_file_acl) {
		*free = inode->i_sb->s_blocksize -
			sizeof(struct ext3_xattr_header) -
			sizeof(__u32);
		return -ENOENT;
	}
	ea_idebug(inode, "reading block %d", EXT3_I(inode)->i_file_acl);
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	if (!bh)
		return -EIO;
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(HDR(bh)->h_refcount));
	end = bh->b_data + bh->b_size;
	if (HDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    HDR(bh)->h_blocks != cpu_to_le32(1)) {
bad_block:	ext3_error(inode->i_sb, "ext3_xattr_get",
			"inode %ld: bad block %d", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		brelse(bh);
		return -EIO;
	}
	/* find named attribute */
	name_len = strlen(name);
	*free = bh->b_size - sizeof(__u32);

	entry = FIRST_ENTRY(bh);
	while (!IS_LAST_ENTRY(entry)) {
		struct ext3_xattr_entry *next =
			EXT3_XATTR_NEXT(entry);
		if ((char *)next >= end)
			goto bad_block;
		if (name_index == entry->e_name_index &&
		    name_len == entry->e_name_len &&
		    memcmp(name, entry->e_name, name_len) == 0) {
			error = 0;
		} else {
			*free -= EXT3_XATTR_LEN(entry->e_name_len);
			*free -= le32_to_cpu(entry->e_value_size);
		}
		entry = next;
	}
	brelse(bh);

	return error;
}

/*
 * ext3_xattr_ibody_set()
 *
 * this routine add/remove/replace attribute in inode body
 */
int
ext3_xattr_ibody_set(handle_t *handle, struct inode *inode, int name_index,
		      const char *name, const void *value, size_t value_len,
		      int flags)
{
	struct ext3_xattr_entry *last, *next, *here = NULL;
	struct ext3_inode *raw_inode;
	int name_len = strlen(name);
	int esize = EXT3_XATTR_LEN(name_len);
	struct buffer_head *bh;
	int err, storage_size;
	struct ext3_iloc iloc;
	int free, min_offs;
	char *start, *end;

	if (EXT3_SB(inode->i_sb)->s_inode_size <= EXT3_GOOD_OLD_INODE_SIZE)
		return -ENOSPC;

	err = ext3_get_inode_loc(inode, &iloc, 1);
	if (err)
		return err;
	raw_inode = ext3_raw_inode(&iloc);
	bh = iloc.bh;

	storage_size = EXT3_SB(inode->i_sb)->s_inode_size -
				EXT3_GOOD_OLD_INODE_SIZE -
				EXT3_I(inode)->i_extra_isize -
				sizeof(__u32);
	start = (char *) raw_inode + EXT3_GOOD_OLD_INODE_SIZE +
			EXT3_I(inode)->i_extra_isize;
	if ((*(__u32*) start) != EXT3_XATTR_MAGIC) {
		/* inode had no attributes before */
		*((__u32*) start) = cpu_to_le32(EXT3_XATTR_MAGIC);
	}
	start += sizeof(__u32);
	end = (char *) raw_inode + EXT3_SB(inode->i_sb)->s_inode_size;
	min_offs = storage_size;
	free = storage_size - sizeof(__u32);

	last = (struct ext3_xattr_entry *) start;
	while (!IS_LAST_ENTRY(last)) {
		next = EXT3_XATTR_NEXT(last);
		if (le32_to_cpu(last->e_value_size) > storage_size ||
				(char *) next >= end) {
			ext3_error(inode->i_sb, "ext3_xattr_ibody_set",
				"inode %ld", inode->i_ino);
			brelse(bh);
			return -EIO;
		}

		if (last->e_value_size) {
			int offs = le16_to_cpu(last->e_value_offs);
			if (offs < min_offs)
				min_offs = offs;
		}
		if (name_index == last->e_name_index &&
			name_len == last->e_name_len &&
			!memcmp(name, last->e_name, name_len))
			here = last;
		else {
			/* we calculate all but our attribute
			 * because it will be removed before changing */
			free -= EXT3_XATTR_LEN(last->e_name_len);
			free -= le32_to_cpu(last->e_value_size);
		}
		last = next;
	}

	if (value && (esize + value_len > free)) {
		brelse(bh);
		return -ENOSPC;
	}

	err = ext3_reserve_inode_write(handle, inode, &iloc);
	if (err) {
		brelse(bh);
		return err;
	}

	/* optimization: can we simple replace old value ? */
	if (here && value_len == le32_to_cpu(here->e_value_size)) {
		int offs = le16_to_cpu(here->e_value_offs);
		memcpy(start + offs, value, value_len);
		goto done;
	}

	if (here) {
		/* time to remove old value */
		struct ext3_xattr_entry *e;
		int size = le32_to_cpu(here->e_value_size);
		int border = le16_to_cpu(here->e_value_offs);
		char *src;

		/* move tail */
		memmove(start + min_offs + size, start + min_offs,
				border - min_offs);

		/* recalculate offsets */
		e = (struct ext3_xattr_entry *) start;
		while (!IS_LAST_ENTRY(e)) {
			struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(e);
			int offs = le16_to_cpu(e->e_value_offs);
			if (offs < border)
				e->e_value_offs =
					cpu_to_le16(offs + size);
			e = next;
		}
		min_offs += size;

		/* remove entry */
		border = EXT3_XATTR_LEN(here->e_name_len);
		src = (char *) here + EXT3_XATTR_LEN(here->e_name_len);
		size = (char *) last - src;
		if ((char *) here + size > end)
			printk("ALERT at %s:%d: 0x%p + %d > 0x%p\n",
					__FILE__, __LINE__, here, size, end);
		memmove(here, src, size);
		last = (struct ext3_xattr_entry *) ((char *) last - border);
		*((__u32 *) last) = 0;
	}

	if (value) {
		int offs = min_offs - value_len;
		/* use last to create new entry */
		last->e_name_len = strlen(name);
		last->e_name_index = name_index;
		last->e_value_offs = cpu_to_le16(offs);
		last->e_value_size = cpu_to_le32(value_len);
		last->e_hash = last->e_value_block = 0;
		memset(last->e_name, 0, esize);
		memcpy(last->e_name, name, last->e_name_len);
		if (start + offs + value_len > end)
			printk("ALERT at %s:%d: 0x%p + %d + %zd > 0x%p\n",
					__FILE__, __LINE__, start, offs,
					value_len, end);
		memcpy(start + offs, value, value_len);
		last = EXT3_XATTR_NEXT(last);
		*((__u32 *) last) = 0;
	}

done:
	ext3_mark_iloc_dirty(handle, inode, &iloc);
	brelse(bh);

	return 0;
}

/*
 * ext3_xattr_set_handle()
 *
 * Create, replace or remove an extended attribute for this inode. Buffer
 * is NULL to remove an existing extended attribute, and non-NULL to
 * either replace an existing extended attribute, or create a new extended
 * attribute. The flags XATTR_REPLACE and XATTR_CREATE
 * specify that an extended attribute must exist and must not exist
 * previous to the call, respectively.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext3_xattr_set_handle(handle_t *handle, struct inode *inode, int name_index,
		const char *name, const void *value, size_t value_len,
		int flags)
{
	int free1 = -1, free2 = -1;
	int err, where = 0, total;
	int name_len;

	ea_idebug(inode, "name=%d.%s, value=%p, value_len=%ld",
		  name_index, name, value, (long)value_len);

	if (IS_RDONLY(inode))
		return -EROFS;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	if (value == NULL)
		value_len = 0;
	if (name == NULL)
		return -EINVAL;
	name_len = strlen(name);
	if (name_len > 255 || value_len > inode->i_sb->s_blocksize)
		return -ERANGE;
	down_write(&EXT3_I(inode)->xattr_sem);

#define EX_FOUND_IN_IBODY	1
#define EX_FOUND_IN_BLOCK	2

	/* try to find attribute in inode body */
	err = ext3_xattr_ibody_find(inode, name_index, name, &free1);
	if (err == 0) {
		/* found EA in inode */
		where = EX_FOUND_IN_IBODY;
	} else if (err == -ENOENT) {
		/* there is no such attribute in inode body */
		/* try to find attribute in dedicated block */
		err = ext3_xattr_block_find(inode, name_index, name, &free2);
		if (err != 0 && err != -ENOENT) {
			/* not found EA in block */
			goto finish;
		} else if (err == 0) {
			/* found EA in block */
			where = EX_FOUND_IN_BLOCK;
		}
	} else
		goto finish;

	/* check flags: may replace? may create ? */
	if (where && (flags & XATTR_CREATE)) {
		err = -EEXIST;
		goto finish;
	} else if (!where && (flags & XATTR_REPLACE)) {
		err = -ENODATA;
		goto finish;
	}

	/* check if we have enough space to store attribute */
	total = EXT3_XATTR_LEN(strlen(name)) + value_len;
	if (total > free1 && free2 > 0 && total > free2) {
		/* have no enough space */
		err = -ENOSPC;
		goto finish;
	}

	/* there are two cases when we want to remove EA from original storage:
	 * a) EA is stored in the inode, but new value doesn't fit
	 * b) EA is stored in the block, but new value fit in inode
	 */
	if (where == EX_FOUND_IN_IBODY && total > free1)
		ext3_xattr_ibody_set(handle, inode, name_index, name,
					NULL, 0, flags);
	else if (where == EX_FOUND_IN_BLOCK && total <= free1)
		ext3_xattr_block_set(handle, inode, name_index,
					name, NULL, 0, flags);

	/* try to store EA in inode body */
	err = ext3_xattr_ibody_set(handle, inode, name_index, name,
					value, value_len, flags);
	if (err) {
		/* can't store EA in inode body: try to store in block */
		err = ext3_xattr_block_set(handle, inode, name_index, name,
						value, value_len, flags);
	}

finish:
	up_write(&EXT3_I(inode)->xattr_sem);
	return err;
}

/*
 * ext3_xattr_block_set()
 *
 * this routine add/remove/replace attribute in EA block
 */
int
ext3_xattr_block_set(handle_t *handle, struct inode *inode, int name_index,
		      const char *name, const void *value, size_t value_len,
		      int flags)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh = NULL;
	struct ext3_xattr_header *header = NULL;
	struct ext3_xattr_entry *here, *last;
	size_t name_len, free, min_offs = sb->s_blocksize;
	int not_found = 1, error;
	char *end;

	/*
	 * header -- Points either into bh, or to a temporarily
	 *           allocated buffer.
	 * here -- The named entry found, or the place for inserting, within
	 *         the block pointed to by header.
	 * last -- Points right after the last named entry within the block
	 *         pointed to by header.
	 * min_offs -- The offset of the first value (values are aligned
	 *             towards the end of the block).
	 * end -- Points right after the block pointed to by header.
	 */
	name_len = strlen(name);
	if (EXT3_I(inode)->i_file_acl) {
		/* The inode already has an extended attribute block. */
		bh = sb_bread(sb, EXT3_I(inode)->i_file_acl);
		error = -EIO;
		if (!bh)
			goto cleanup;
		ea_bdebug(bh, "b_count=%d, refcount=%d",
			atomic_read(&(bh->b_count)),
			le32_to_cpu(HDR(bh)->h_refcount));
		header = HDR(bh);
		end = bh->b_data + bh->b_size;
		if (header->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
		    header->h_blocks != cpu_to_le32(1)) {
bad_block:		ext3_error(sb, "ext3_xattr_set",
				"inode %ld: bad block %d", inode->i_ino,
				EXT3_I(inode)->i_file_acl);
			error = -EIO;
			goto cleanup;
		}
		/* Find the named attribute. */
		here = FIRST_ENTRY(bh);
		while (!IS_LAST_ENTRY(here)) {
			struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(here);
			if ((char *)next >= end)
				goto bad_block;
			if (!here->e_value_block && here->e_value_size) {
				size_t offs = le16_to_cpu(here->e_value_offs);
				if (offs < min_offs)
					min_offs = offs;
			}
			not_found = name_index - here->e_name_index;
			if (!not_found)
				not_found = name_len - here->e_name_len;
			if (!not_found)
				not_found = memcmp(name, here->e_name,name_len);
			if (not_found <= 0)
				break;
			here = next;
		}
		last = here;
		/* We still need to compute min_offs and last. */
		while (!IS_LAST_ENTRY(last)) {
			struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(last);
			if ((char *)next >= end)
				goto bad_block;
			if (!last->e_value_block && last->e_value_size) {
				size_t offs = le16_to_cpu(last->e_value_offs);
				if (offs < min_offs)
					min_offs = offs;
			}
			last = next;
		}

		/* Check whether we have enough space left. */
		free = min_offs - ((char*)last - (char*)header) - sizeof(__u32);
	} else {
		/* We will use a new extended attribute block. */
		free = sb->s_blocksize -
			sizeof(struct ext3_xattr_header) - sizeof(__u32);
		here = last = NULL;  /* avoid gcc uninitialized warning. */
	}

	if (not_found) {
		/* Request to remove a nonexistent attribute? */
		error = -ENODATA;
		if (flags & XATTR_REPLACE)
			goto cleanup;
		error = 0;
		if (value == NULL)
			goto cleanup;
	} else {
		/* Request to create an existing attribute? */
		error = -EEXIST;
		if (flags & XATTR_CREATE)
			goto cleanup;
		if (!here->e_value_block && here->e_value_size) {
			size_t size = le32_to_cpu(here->e_value_size);

			if (le16_to_cpu(here->e_value_offs) + size > 
			    sb->s_blocksize || size > sb->s_blocksize)
				goto bad_block;
			free += EXT3_XATTR_SIZE(size);
		}
		free += EXT3_XATTR_LEN(name_len);
	}
	error = -ENOSPC;
	if (free < EXT3_XATTR_LEN(name_len) + EXT3_XATTR_SIZE(value_len))
		goto cleanup;

	/* Here we know that we can set the new attribute. */

	if (header) {
		int credits = 0;

		/* assert(header == HDR(bh)); */
		if (header->h_refcount != cpu_to_le32(1))
			goto skip_get_write_access;
		/* ext3_journal_get_write_access() requires an unlocked bh,
		   which complicates things here. */
		error = ext3_journal_get_write_access_credits(handle, bh,
							      &credits);
		if (error)
			goto cleanup;
		lock_buffer(bh);
		if (header->h_refcount == cpu_to_le32(1)) {
			ea_bdebug(bh, "modifying in-place");
			ext3_xattr_cache_remove(bh);
			/* keep the buffer locked while modifying it. */
		} else {
			int offset;

			unlock_buffer(bh);
			journal_release_buffer(handle, bh, credits);
		skip_get_write_access:
			ea_bdebug(bh, "cloning");
			header = kmalloc(bh->b_size, GFP_KERNEL);
			error = -ENOMEM;
			if (header == NULL)
				goto cleanup;
			memcpy(header, HDR(bh), bh->b_size);
			header->h_refcount = cpu_to_le32(1);
			offset = (char *)here - bh->b_data;
			here = ENTRY((char *)header + offset);
			offset = (char *)last - bh->b_data;
			last = ENTRY((char *)header + offset);
		}
	} else {
		/* Allocate a buffer where we construct the new block. */
		header = kmalloc(sb->s_blocksize, GFP_KERNEL);
		error = -ENOMEM;
		if (header == NULL)
			goto cleanup;
		memset(header, 0, sb->s_blocksize);
		end = (char *)header + sb->s_blocksize;
		header->h_magic = cpu_to_le32(EXT3_XATTR_MAGIC);
		header->h_blocks = header->h_refcount = cpu_to_le32(1);
		last = here = ENTRY(header+1);
	}

	/* Iff we are modifying the block in-place, bh is locked here. */

	if (not_found) {
		/* Insert the new name. */
		size_t size = EXT3_XATTR_LEN(name_len);
		size_t rest = (char *)last - (char *)here;
		memmove((char *)here + size, here, rest);
		memset(here, 0, size);
		here->e_name_index = name_index;
		here->e_name_len = name_len;
		memcpy(here->e_name, name, name_len);
	} else {
		if (!here->e_value_block && here->e_value_size) {
			char *first_val = (char *)header + min_offs;
			size_t offs = le16_to_cpu(here->e_value_offs);
			char *val = (char *)header + offs;
			size_t size = EXT3_XATTR_SIZE(
				le32_to_cpu(here->e_value_size));

			if (size == EXT3_XATTR_SIZE(value_len)) {
				/* The old and the new value have the same
				   size. Just replace. */
				here->e_value_size = cpu_to_le32(value_len);
				memset(val + size - EXT3_XATTR_PAD, 0,
				       EXT3_XATTR_PAD); /* Clear pad bytes. */
				memcpy(val, value, value_len);
				goto skip_replace;
			}

			/* Remove the old value. */
			memmove(first_val + size, first_val, val - first_val);
			memset(first_val, 0, size);
			here->e_value_offs = 0;
			min_offs += size;

			/* Adjust all value offsets. */
			last = ENTRY(header+1);
			while (!IS_LAST_ENTRY(last)) {
				size_t o = le16_to_cpu(last->e_value_offs);
				if (!last->e_value_block && o < offs)
					last->e_value_offs =
						cpu_to_le16(o + size);
				last = EXT3_XATTR_NEXT(last);
			}
		}
		if (value == NULL) {
			/* Remove the old name. */
			size_t size = EXT3_XATTR_LEN(name_len);
			last = ENTRY((char *)last - size);
			memmove(here, (char*)here + size,
				(char*)last - (char*)here);
			memset(last, 0, size);
		}
	}

	if (value != NULL) {
		/* Insert the new value. */
		here->e_value_size = cpu_to_le32(value_len);
		if (value_len) {
			size_t size = EXT3_XATTR_SIZE(value_len);
			char *val = (char *)header + min_offs - size;
			here->e_value_offs =
				cpu_to_le16((char *)val - (char *)header);
			memset(val + size - EXT3_XATTR_PAD, 0,
			       EXT3_XATTR_PAD); /* Clear the pad bytes. */
			memcpy(val, value, value_len);
		}
	}

skip_replace:
	if (IS_LAST_ENTRY(ENTRY(header+1))) {
		/* This block is now empty. */
		if (bh && header == HDR(bh))
			unlock_buffer(bh);  /* we were modifying in-place. */
		error = ext3_xattr_set_handle2(handle, inode, bh, NULL);
	} else {
		ext3_xattr_rehash(header, here);
		if (bh && header == HDR(bh))
			unlock_buffer(bh);  /* we were modifying in-place. */
		error = ext3_xattr_set_handle2(handle, inode, bh, header);
	}

cleanup:
	brelse(bh);
	if (!(bh && header == HDR(bh)))
		kfree(header);

	return error;
}

/*
 * Second half of ext3_xattr_set_handle(): Update the file system.
 */
static int
ext3_xattr_set_handle2(handle_t *handle, struct inode *inode,
		       struct buffer_head *old_bh,
		       struct ext3_xattr_header *header)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *new_bh = NULL;
	int credits = 0, error;

	if (header) {
		new_bh = ext3_xattr_cache_find(handle, inode, header, &credits);
		if (new_bh) {
			/* We found an identical block in the cache. */
			if (new_bh == old_bh)
				ea_bdebug(new_bh, "keeping this block");
			else {
				/* The old block is released after updating
				   the inode. */
				ea_bdebug(new_bh, "reusing block");

				error = -EDQUOT;
				if (DQUOT_ALLOC_BLOCK(inode, 1)) {
					unlock_buffer(new_bh);
					journal_release_buffer(handle, new_bh,
							       credits);
					goto cleanup;
				}
				HDR(new_bh)->h_refcount = cpu_to_le32(1 +
					le32_to_cpu(HDR(new_bh)->h_refcount));
				ea_bdebug(new_bh, "refcount now=%d",
					le32_to_cpu(HDR(new_bh)->h_refcount));
			}
			unlock_buffer(new_bh);
		} else if (old_bh && header == HDR(old_bh)) {
			/* Keep this block. No need to lock the block as we
			 * don't need to change the reference count. */
			new_bh = old_bh;
			get_bh(new_bh);
			ext3_xattr_cache_insert(new_bh);
		} else {
			/* We need to allocate a new block */
			int goal = le32_to_cpu(
					EXT3_SB(sb)->s_es->s_first_data_block) +
				EXT3_I(inode)->i_block_group *
				EXT3_BLOCKS_PER_GROUP(sb);
			int block = ext3_new_block(handle, inode, goal, &error);
			if (error)
				goto cleanup;
			ea_idebug(inode, "creating block %d", block);

			new_bh = sb_getblk(sb, block);
			if (!new_bh) {
getblk_failed:
				ext3_free_blocks(handle, inode, block, 1);
				error = -EIO;
				goto cleanup;
			}
			lock_buffer(new_bh);
			error = ext3_journal_get_create_access(handle, new_bh);
			if (error) {
				unlock_buffer(new_bh);
				goto getblk_failed;
			}
			memcpy(new_bh->b_data, header, new_bh->b_size);
			set_buffer_uptodate(new_bh);
			unlock_buffer(new_bh);
			ext3_xattr_cache_insert(new_bh);

			ext3_xattr_update_super_block(handle, sb);
		}
		error = ext3_journal_dirty_metadata(handle, new_bh);
		if (error)
			goto cleanup;
	}

	/* Update the inode. */
	EXT3_I(inode)->i_file_acl = new_bh ? new_bh->b_blocknr : 0;
	inode->i_ctime = CURRENT_TIME_SEC;
	ext3_mark_inode_dirty(handle, inode);
	if (IS_SYNC(inode))
		handle->h_sync = 1;

	error = 0;
	if (old_bh && old_bh != new_bh) {
		/*
		 * If there was an old block, and we are no longer using it,
		 * release the old block.
		*/
		error = ext3_journal_get_write_access(handle, old_bh);
		if (error)
			goto cleanup;
		lock_buffer(old_bh);
		if (HDR(old_bh)->h_refcount == cpu_to_le32(1)) {
			/* Free the old block. */
			ea_bdebug(old_bh, "freeing");
			ext3_free_blocks(handle, inode, old_bh->b_blocknr, 1);

			/* ext3_forget() calls bforget() for us, but we
			   let our caller release old_bh, so we need to
			   duplicate the handle before. */
			get_bh(old_bh);
			ext3_forget(handle, 1, inode, old_bh,old_bh->b_blocknr);
		} else {
			/* Decrement the refcount only. */
			HDR(old_bh)->h_refcount = cpu_to_le32(
				le32_to_cpu(HDR(old_bh)->h_refcount) - 1);
			DQUOT_FREE_BLOCK(inode, 1);
			ext3_journal_dirty_metadata(handle, old_bh);
			ea_bdebug(old_bh, "refcount now=%d",
				le32_to_cpu(HDR(old_bh)->h_refcount));
		}
		unlock_buffer(old_bh);
	}

cleanup:
	brelse(new_bh);

	return error;
}

/*
 * ext3_xattr_set()
 *
 * Like ext3_xattr_set_handle, but start from an inode. This extended
 * attribute modification is a filesystem transaction by itself.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext3_xattr_set(struct inode *inode, int name_index, const char *name,
	       const void *value, size_t value_len, int flags)
{
	handle_t *handle;
	int error, retries = 0;

retry:
	handle = ext3_journal_start(inode, EXT3_DATA_TRANS_BLOCKS);
	if (IS_ERR(handle)) {
		error = PTR_ERR(handle);
	} else {
		int error2;

		error = ext3_xattr_set_handle(handle, inode, name_index, name,
					      value, value_len, flags);
		error2 = ext3_journal_stop(handle);
		if (error == -ENOSPC &&
		    ext3_should_retry_alloc(inode->i_sb, &retries))
			goto retry;
		if (error == 0)
			error = error2;
	}

	return error;
}

/*
 * ext3_xattr_delete_inode()
 *
 * Free extended attribute resources associated with this inode. This
 * is called immediately before an inode is freed.
 */
void
ext3_xattr_delete_inode(handle_t *handle, struct inode *inode)
{
	struct buffer_head *bh = NULL;

	down_write(&EXT3_I(inode)->xattr_sem);
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	if (!bh) {
		ext3_error(inode->i_sb, "ext3_xattr_delete_inode",
			"inode %ld: block %d read error", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		goto cleanup;
	}
	if (HDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    HDR(bh)->h_blocks != cpu_to_le32(1)) {
		ext3_error(inode->i_sb, "ext3_xattr_delete_inode",
			"inode %ld: bad block %d", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		goto cleanup;
	}
	if (ext3_journal_get_write_access(handle, bh) != 0)
		goto cleanup;
	lock_buffer(bh);
	if (HDR(bh)->h_refcount == cpu_to_le32(1)) {
		ext3_xattr_cache_remove(bh);
		ext3_free_blocks(handle, inode, EXT3_I(inode)->i_file_acl, 1);
		get_bh(bh);
		ext3_forget(handle, 1, inode, bh, EXT3_I(inode)->i_file_acl);
	} else {
		HDR(bh)->h_refcount = cpu_to_le32(
			le32_to_cpu(HDR(bh)->h_refcount) - 1);
		ext3_journal_dirty_metadata(handle, bh);
		if (IS_SYNC(inode))
			handle->h_sync = 1;
		DQUOT_FREE_BLOCK(inode, 1);
	}
	ea_bdebug(bh, "refcount now=%d", le32_to_cpu(HDR(bh)->h_refcount) - 1);
	unlock_buffer(bh);
	EXT3_I(inode)->i_file_acl = 0;

cleanup:
	brelse(bh);
	up_write(&EXT3_I(inode)->xattr_sem);
}

/*
 * ext3_xattr_put_super()
 *
 * This is called when a file system is unmounted.
 */
void
ext3_xattr_put_super(struct super_block *sb)
{
	mb_cache_shrink(ext3_xattr_cache, sb->s_bdev);
}

/*
 * ext3_xattr_cache_insert()
 *
 * Create a new entry in the extended attribute cache, and insert
 * it unless such an entry is already in the cache.
 *
 * Returns 0, or a negative error number on failure.
 */
static int
ext3_xattr_cache_insert(struct buffer_head *bh)
{
	__u32 hash = le32_to_cpu(HDR(bh)->h_hash);
	struct mb_cache_entry *ce;
	int error;

	ce = mb_cache_entry_alloc(ext3_xattr_cache);
	if (!ce)
		return -ENOMEM;
	error = mb_cache_entry_insert(ce, bh->b_bdev, bh->b_blocknr, &hash);
	if (error) {
		mb_cache_entry_free(ce);
		if (error == -EBUSY) {
			ea_bdebug(bh, "already in cache (%d cache entries)",
				atomic_read(&ext3_xattr_cache->c_entry_count));
			error = 0;
		}
	} else {
		ea_bdebug(bh, "inserting [%x] (%d cache entries)", (int)hash,
			  atomic_read(&ext3_xattr_cache->c_entry_count));
		mb_cache_entry_release(ce);
	}
	return error;
}

/*
 * ext3_xattr_cmp()
 *
 * Compare two extended attribute blocks for equality.
 *
 * Returns 0 if the blocks are equal, 1 if they differ, and
 * a negative error number on errors.
 */
static int
ext3_xattr_cmp(struct ext3_xattr_header *header1,
	       struct ext3_xattr_header *header2)
{
	struct ext3_xattr_entry *entry1, *entry2;

	entry1 = ENTRY(header1+1);
	entry2 = ENTRY(header2+1);
	while (!IS_LAST_ENTRY(entry1)) {
		if (IS_LAST_ENTRY(entry2))
			return 1;
		if (entry1->e_hash != entry2->e_hash ||
		    entry1->e_name_len != entry2->e_name_len ||
		    entry1->e_value_size != entry2->e_value_size ||
		    memcmp(entry1->e_name, entry2->e_name, entry1->e_name_len))
			return 1;
		if (entry1->e_value_block != 0 || entry2->e_value_block != 0)
			return -EIO;
		if (memcmp((char *)header1 + le16_to_cpu(entry1->e_value_offs),
			   (char *)header2 + le16_to_cpu(entry2->e_value_offs),
			   le32_to_cpu(entry1->e_value_size)))
			return 1;

		entry1 = EXT3_XATTR_NEXT(entry1);
		entry2 = EXT3_XATTR_NEXT(entry2);
	}
	if (!IS_LAST_ENTRY(entry2))
		return 1;
	return 0;
}

/*
 * ext3_xattr_cache_find()
 *
 * Find an identical extended attribute block.
 *
 * Returns a pointer to the block found, or NULL if such a block was
 * not found or an error occurred.
 */
static struct buffer_head *
ext3_xattr_cache_find(handle_t *handle, struct inode *inode,
		      struct ext3_xattr_header *header, int *credits)
{
	__u32 hash = le32_to_cpu(header->h_hash);
	struct mb_cache_entry *ce;

	if (!header->h_hash)
		return NULL;  /* never share */
	ea_idebug(inode, "looking for cached blocks [%x]", (int)hash);
	ce = mb_cache_entry_find_first(ext3_xattr_cache, 0,
				       inode->i_sb->s_bdev, hash);
	while (ce) {
		struct buffer_head *bh = sb_bread(inode->i_sb, ce->e_block);

		if (!bh) {
			ext3_error(inode->i_sb, "ext3_xattr_cache_find",
				"inode %ld: block %ld read error",
				inode->i_ino, (unsigned long) ce->e_block);
		} else if (ext3_journal_get_write_access_credits(
				handle, bh, credits) == 0) {
			/* ext3_journal_get_write_access() requires an unlocked
			 * bh, which complicates things here. */
			lock_buffer(bh);
			if (le32_to_cpu(HDR(bh)->h_refcount) >
				   EXT3_XATTR_REFCOUNT_MAX) {
				ea_idebug(inode, "block %ld refcount %d>%d",
					  (unsigned long) ce->e_block,
					  le32_to_cpu(HDR(bh)->h_refcount),
					  EXT3_XATTR_REFCOUNT_MAX);
			} else if (!ext3_xattr_cmp(header, HDR(bh))) {
				mb_cache_entry_release(ce);
				/* buffer will be unlocked by caller */
				return bh;
			}
			unlock_buffer(bh);
			journal_release_buffer(handle, bh, *credits);
			*credits = 0;
			brelse(bh);
		}
		ce = mb_cache_entry_find_next(ce, 0, inode->i_sb->s_bdev, hash);
	}
	return NULL;
}

/*
 * ext3_xattr_cache_remove()
 *
 * Remove the cache entry of a block from the cache. Called when a
 * block becomes invalid.
 */
static void
ext3_xattr_cache_remove(struct buffer_head *bh)
{
	struct mb_cache_entry *ce;

	ce = mb_cache_entry_get(ext3_xattr_cache, bh->b_bdev,
				bh->b_blocknr);
	if (ce) {
		ea_bdebug(bh, "removing (%d cache entries remaining)",
			  atomic_read(&ext3_xattr_cache->c_entry_count)-1);
		mb_cache_entry_free(ce);
	} else 
		ea_bdebug(bh, "no cache entry");
}

#define NAME_HASH_SHIFT 5
#define VALUE_HASH_SHIFT 16

/*
 * ext3_xattr_hash_entry()
 *
 * Compute the hash of an extended attribute.
 */
static inline void ext3_xattr_hash_entry(struct ext3_xattr_header *header,
					 struct ext3_xattr_entry *entry)
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
		__le32 *value = (__le32 *)((char *)header +
			le16_to_cpu(entry->e_value_offs));
		for (n = (le32_to_cpu(entry->e_value_size) +
		     EXT3_XATTR_ROUND) >> EXT3_XATTR_PAD_BITS; n; n--) {
			hash = (hash << VALUE_HASH_SHIFT) ^
			       (hash >> (8*sizeof(hash) - VALUE_HASH_SHIFT)) ^
			       le32_to_cpu(*value++);
		}
	}
	entry->e_hash = cpu_to_le32(hash);
}

#undef NAME_HASH_SHIFT
#undef VALUE_HASH_SHIFT

#define BLOCK_HASH_SHIFT 16

/*
 * ext3_xattr_rehash()
 *
 * Re-compute the extended attribute hash value after an entry has changed.
 */
static void ext3_xattr_rehash(struct ext3_xattr_header *header,
			      struct ext3_xattr_entry *entry)
{
	struct ext3_xattr_entry *here;
	__u32 hash = 0;

	ext3_xattr_hash_entry(header, entry);
	here = ENTRY(header+1);
	while (!IS_LAST_ENTRY(here)) {
		if (!here->e_hash) {
			/* Block is not shared if an entry's hash value == 0 */
			hash = 0;
			break;
		}
		hash = (hash << BLOCK_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - BLOCK_HASH_SHIFT)) ^
		       le32_to_cpu(here->e_hash);
		here = EXT3_XATTR_NEXT(here);
	}
	header->h_hash = cpu_to_le32(hash);
}

#undef BLOCK_HASH_SHIFT

int __init
init_ext3_xattr(void)
{
	ext3_xattr_cache = mb_cache_create("ext3_xattr", NULL,
		sizeof(struct mb_cache_entry) +
		sizeof(struct mb_cache_entry_index), 1, 6);
	if (!ext3_xattr_cache)
		return -ENOMEM;
	return 0;
}

void
exit_ext3_xattr(void)
{
	if (ext3_xattr_cache)
		mb_cache_destroy(ext3_xattr_cache);
	ext3_xattr_cache = NULL;
}
