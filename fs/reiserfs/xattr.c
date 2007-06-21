/*
 * linux/fs/reiserfs/xattr.c
 *
 * Copyright (c) 2002 by Jeff Mahoney, <jeffm@suse.com>
 *
 */

/*
 * In order to implement EA/ACLs in a clean, backwards compatible manner,
 * they are implemented as files in a "private" directory.
 * Each EA is in it's own file, with the directory layout like so (/ is assumed
 * to be relative to fs root). Inside the /.reiserfs_priv/xattrs directory,
 * directories named using the capital-hex form of the objectid and
 * generation number are used. Inside each directory are individual files
 * named with the name of the extended attribute.
 *
 * So, for objectid 12648430, we could have:
 * /.reiserfs_priv/xattrs/C0FFEE.0/system.posix_acl_access
 * /.reiserfs_priv/xattrs/C0FFEE.0/system.posix_acl_default
 * /.reiserfs_priv/xattrs/C0FFEE.0/user.Content-Type
 * .. or similar.
 *
 * The file contents are the text of the EA. The size is known based on the
 * stat data describing the file.
 *
 * In the case of system.posix_acl_access and system.posix_acl_default, since
 * these are special cases for filesystem ACLs, they are interpreted by the
 * kernel, in addition, they are negatively and positively cached and attached
 * to the inode so that unnecessary lookups are avoided.
 *
 * Locking works like so:
 * Every component (xattr root, xattr dir, and the xattrs themselves) are
 * protected by their i_mutex.
 */

#include <linux/reiserfs_fs.h>
#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <linux/reiserfs_acl.h>
#include <asm/uaccess.h>
#include <net/checksum.h>
#include <linux/smp_lock.h>
#include <linux/stat.h>
#include <asm/semaphore.h>

#define PRIVROOT_NAME ".reiserfs_priv"
#define XAROOT_NAME   "xattrs"

struct xattr_handler *reiserfs_xattr_handlers[] = {
	&reiserfs_xattr_user_handler,
	&reiserfs_xattr_trusted_handler,
#ifdef CONFIG_REISERFS_FS_SECURITY
	&reiserfs_xattr_security_handler,
#endif
#ifdef CONFIG_REISERFS_FS_POSIX_ACL
	&reiserfs_posix_acl_access_handler,
	&reiserfs_posix_acl_default_handler,
#endif
	NULL
};

/*
 * In order to implement different sets of xattr operations for each xattr
 * prefix with the generic xattr API, a filesystem should create a
 * null-terminated array of struct xattr_handler (one for each prefix) and
 * hang a pointer to it off of the s_xattr field of the superblock.
 *
 * The generic_fooxattr() functions will use this list to dispatch xattr
 * operations to the correct xattr_handler.
 */
#define for_each_xattr_handler(handlers, handler)		\
		for ((handler) = *(handlers)++;			\
			(handler) != NULL;			\
			(handler) = *(handlers)++)

/* This is the implementation for the xattr plugin infrastructure */
static inline struct xattr_handler *find_xattr_handler_prefix(struct xattr_handler **handlers,
                                                       const char *name)
{
	struct xattr_handler *xah;

	if (!handlers)
		return NULL;

	for_each_xattr_handler(handlers, xah) {
		if (strncmp(xah->prefix, name, strlen(xah->prefix)) == 0)
			break;
	}

	return xah;
}

#define xattr_may_create(flags)	(!flags || flags & XATTR_CREATE)

static struct dentry *lookup_or_create_dir(struct dentry *parent,
                                           const char *name, int flags)
{
	struct dentry *dentry;
	BUG_ON(!parent);

	mutex_lock_nested(&parent->d_inode->i_mutex, I_MUTEX_XATTR);

	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry)) {
		mutex_unlock(&parent->d_inode->i_mutex);
		return dentry;
	} else if (!dentry->d_inode) {
		int err = -ENODATA;

		if (xattr_may_create(flags))
			err = vfs_mkdir(parent->d_inode, dentry, NULL, 0700);

		if (err) {
			dput(dentry);
			dentry = ERR_PTR(err);
		}
	}

	mutex_unlock(&parent->d_inode->i_mutex);
	return dentry;
}

static struct dentry *open_xa_root(struct super_block *sb, int flags)
{
	struct dentry *privroot = REISERFS_SB(sb)->priv_root;
	if (!privroot)
		return ERR_PTR(-ENODATA);
	return lookup_or_create_dir(privroot, XAROOT_NAME, flags);
}

static struct dentry *open_xa_dir(const struct inode *inode, int flags)
{
	struct dentry *xaroot, *xadir;
	char namebuf[17];

	xaroot = open_xa_root(inode->i_sb, flags);
	if (IS_ERR(xaroot))
		return xaroot;

	snprintf(namebuf, sizeof(namebuf), "%X.%X",
		 le32_to_cpu(INODE_PKEY(inode)->k_objectid),
		 inode->i_generation);

	xadir = lookup_or_create_dir(xaroot, namebuf, flags);
	dput(xaroot);
	return xadir;

}

static struct dentry *xattr_lookup(struct inode *inode, const char *name,
                                   int flags)
{
	struct dentry *xadir, *xafile;
	int err = 0;

	xadir = open_xa_dir(inode, flags);
	if (IS_ERR(xadir))
		return ERR_PTR(PTR_ERR(xadir));

	mutex_lock_nested(&xadir->d_inode->i_mutex, I_MUTEX_XATTR);
	xafile = lookup_one_len(name, xadir, strlen(name));
	if (IS_ERR(xafile)) {
		err = PTR_ERR(xafile);
		goto out;
	}

	if (xafile->d_inode && (flags & XATTR_CREATE))
		err = -EEXIST;

	if (!xafile->d_inode) {
		err = -ENODATA;
		if (xattr_may_create(flags))
			err = vfs_create(xadir->d_inode, xafile,
			                 0700|S_IFREG, NULL);
	}

	if (err)
		dput(xafile);
out:
	mutex_unlock(&xadir->d_inode->i_mutex);
	dput(xadir);
	if (err)
		return ERR_PTR(err);
	return xafile;
}

static struct file *open_xattr_file(struct inode *inode,
                                    const char *name, int flags)
{
	struct dentry *dentry = xattr_lookup(inode, name, flags);

	if (IS_ERR(dentry))
		return (void *)dentry;

	return dentry_open(dentry, NULL, O_RDWR|O_NOATIME);
}

/* Internal operations on file data */
static inline void reiserfs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

static struct page *reiserfs_get_page(struct inode *dir, unsigned long n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page;
	/* We can deadlock if we try to free dentries,
	   and an unlink/rmdir has just occured - GFP_NOFS avoids this */
	mapping_set_gfp_mask(mapping, GFP_NOFS);
	page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
		if (PageError(page))
			goto fail;
	}
	return page;

      fail:
	reiserfs_put_page(page);
	return ERR_PTR(-EIO);
}

static inline __u32 xattr_hash(const char *msg, int len)
{
	return csum_partial(msg, len, 0);
}

/* Generic extended attribute operations that can be used by xa plugins */

/*
 * inode->i_mutex: down
 */
int
reiserfs_xattr_set_handle(struct reiserfs_transaction_handle *th,
                          struct inode *inode, const char *name,
			  const void *buffer, size_t buffer_size, int flags)
{
	int err = 0;
	struct file *fp;
	struct page *page;
	char *data;
	struct address_space *mapping;
	size_t file_pos = 0;
	size_t buffer_pos = 0;
	struct inode *xinode;
	struct iattr newattrs;
	__u32 xahash = 0;

	if (get_inode_sd_version(inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	/* Clearing it out - delete it. */
	if (!buffer) {
		struct dentry *dentry;

		dentry = xattr_lookup(inode, name, XATTR_REPLACE);
		if (IS_ERR(dentry)) {
			err = PTR_ERR(dentry);
			if (err == -ENODATA)
				err = 0;
			return err;
		}

		err = vfs_unlink(dentry->d_parent->d_inode, dentry, NULL);
		if (!err) {
			inode->i_ctime = CURRENT_TIME_SEC;
			mark_inode_dirty(inode);
		}
		return err;
	}

	fp = open_xattr_file(inode, name, flags);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		goto out;
	}

	xahash = xattr_hash(buffer, buffer_size);
	xinode = fp->f_path.dentry->d_inode;

	/* Resize it so we're ok to write there */
	newattrs.ia_size = buffer_size;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
	mutex_lock_nested(&xinode->i_mutex, I_MUTEX_XATTR);
	err = notify_change(fp->f_path.dentry, NULL, &newattrs);
	if (err)
		goto out_filp;

	mapping = xinode->i_mapping;

	/* If we're in writeback mode, we still need to order xattr writes */
	if (reiserfs_data_writeback(xinode->i_sb))
		REISERFS_I(xinode)->i_flags |= i_data_ordered;

	while (buffer_pos < buffer_size || buffer_pos == 0) {
		size_t chunk;
		size_t skip = 0;
		size_t page_offset = (file_pos & (PAGE_CACHE_SIZE - 1));
		if (buffer_size - buffer_pos > PAGE_CACHE_SIZE)
			chunk = PAGE_CACHE_SIZE;
		else
			chunk = buffer_size - buffer_pos;

		page = reiserfs_get_page(xinode, file_pos >> PAGE_CACHE_SHIFT);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out_filp;
		}

		lock_page(page);
		data = page_address(page);

		if (file_pos == 0) {
			struct reiserfs_xattr_header *rxh;
			skip = file_pos = sizeof(struct reiserfs_xattr_header);
			if (chunk + skip > PAGE_CACHE_SIZE)
				chunk = PAGE_CACHE_SIZE - skip;
			rxh = (struct reiserfs_xattr_header *)data;
			rxh->h_magic = cpu_to_le32(REISERFS_XATTR_MAGIC);
			rxh->h_hash = cpu_to_le32(xahash);
		}

		err = mapping->a_ops->prepare_write(fp, page, page_offset,
						    page_offset + chunk + skip);
		if (!err) {
			if (buffer)
				memcpy(data + skip, buffer + buffer_pos, chunk);
			err =
			    mapping->a_ops->commit_write(fp, page, page_offset,
							 page_offset + chunk +
							 skip);
		}
		unlock_page(page);
		reiserfs_put_page(page);
		buffer_pos += chunk;
		file_pos += chunk;
		skip = 0;
		if (err || buffer_size == 0 || !buffer)
			break;
	}

      out_filp:
	mutex_unlock(&xinode->i_mutex);
	fput(fp);

      out:

	/* We can't mark the inode dirty if it's not hashed. This is the case
	 * when we're inheriting the default ACL. If we dirty it, the inode
	 * gets marked dirty, but won't (ever) make it onto the dirty list until
	 * it's synced explicitly to clear I_DIRTY. This is bad. */
	if (!hlist_unhashed(&inode->i_hash)) {
		inode->i_ctime = CURRENT_TIME_SEC;
		mark_inode_dirty(inode);
	}

	return err;
}

/* We need to start a transaction to maintain lock ordering */
int reiserfs_xattr_set(struct inode *inode, const char *name,
                       const void *buffer, size_t buffer_size, int flags)
{

	struct reiserfs_transaction_handle th;
	int error, error2;
	size_t jbegin_count = reiserfs_xattr_nblocks(inode, buffer_size);

	if (!(flags & XATTR_REPLACE))
		jbegin_count += reiserfs_xattr_jcreate_nblocks(inode);

	reiserfs_write_lock(inode->i_sb);
	error = journal_begin(&th, inode->i_sb, jbegin_count);
	if (error) {
		reiserfs_write_unlock(inode->i_sb);
		return error;
	}

	error = reiserfs_xattr_set_handle(&th, inode, name,
	                                  buffer, buffer_size, flags);

	error2 = journal_end(&th, inode->i_sb, jbegin_count);
	if (error == 0)
		error = error2;
	reiserfs_write_unlock(inode->i_sb);

	return error;
}

/*
 * inode->i_mutex: down
 */
int
reiserfs_xattr_get(struct inode *inode, const char *name, void *buffer,
		   size_t buffer_size)
{
	ssize_t err = 0;
	struct file *fp;
	size_t isize;
	size_t file_pos = 0;
	size_t buffer_pos = 0;
	struct page *page;
	struct inode *xinode;
	__u32 hash = 0;

	if (name == NULL)
		return -EINVAL;

	/* We can't have xattrs attached to v1 items since they don't have
	 * generation numbers */
	if (get_inode_sd_version(inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	fp = open_xattr_file(inode, name, XATTR_REPLACE);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		goto out;
	}

	xinode = fp->f_path.dentry->d_inode;
	isize = xinode->i_size;

	/* Just return the size needed */
	if (buffer == NULL) {
		err = isize - sizeof(struct reiserfs_xattr_header);
		goto out_fput;
	}

	if (buffer_size < isize - sizeof(struct reiserfs_xattr_header)) {
		err = -ERANGE;
		goto out_fput;
	}

	mutex_lock(&fp->f_path.dentry->d_inode->i_mutex);

	while (file_pos < isize) {
		size_t chunk;
		char *data;
		size_t skip = 0;
		if (isize - file_pos > PAGE_CACHE_SIZE)
			chunk = PAGE_CACHE_SIZE;
		else
			chunk = isize - file_pos;

		page = reiserfs_get_page(xinode, file_pos >> PAGE_CACHE_SHIFT);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out_fput;
		}

		lock_page(page);
		data = page_address(page);
		if (file_pos == 0) {
			struct reiserfs_xattr_header *rxh =
			    (struct reiserfs_xattr_header *)data;
			skip = file_pos = sizeof(struct reiserfs_xattr_header);
			chunk -= skip;
			/* Magic doesn't match up.. */
			if (rxh->h_magic != cpu_to_le32(REISERFS_XATTR_MAGIC)) {
				unlock_page(page);
				reiserfs_put_page(page);
				reiserfs_warning(inode->i_sb, "jdm-20001",
						 "Invalid magic for xattr (%s) "
						 "associated with %k", name,
						 INODE_PKEY(inode));
				err = -EIO;
				goto out_fput;
			}
			hash = le32_to_cpu(rxh->h_hash);
		}
		memcpy(buffer + buffer_pos, data + skip, chunk);
		unlock_page(page);
		reiserfs_put_page(page);
		file_pos += chunk;
		buffer_pos += chunk;
		skip = 0;
	}
	err = isize - sizeof(struct reiserfs_xattr_header);

	if (xattr_hash(buffer, isize - sizeof(struct reiserfs_xattr_header)) !=
	    hash) {
		reiserfs_warning(inode->i_sb, "jdm-20002",
				 "Invalid hash for xattr (%s) associated "
				 "with %k", name, INODE_PKEY(inode));
		err = -EIO;
		goto out_fput;
	}

      out_fput:
	mutex_unlock(&fp->f_path.dentry->d_inode->i_mutex);
	fput(fp);

      out:
	return err;
}

#if 0
static int __restart_transaction_if_needed(struct inode *inode, int chunk)
{
	struct reiserfs_transaction_handle *th = current->journal_info;
	struct super_block *s = th->t_super;
	int len = th->t_blocks_allocated;
	int err;

	BUG_ON(!th->t_trans_id);
	BUG_ON(!th->t_refcount);

	if (!journal_transaction_should_end(th, len))
		return 0;

	/* we cannot restart while nested */
	if (th->t_refcount > 1) {
		reiserfs_warning(th->t_super, "jdm-20003", "can't restart nested transaction in %s\n", __FUNCTION__);
		return 0;
	}

	err = journal_end(th, s, len);
	if (!err) {
		err = journal_begin(th, s, chunk);
		if (!err)
			reiserfs_update_inode_transaction(inode);
	}

	return err;
}
#endif

/* The following are side effects of other operations that aren't explicitly
 * modifying extended attributes. This includes operations such as permissions
 * or ownership changes, object deletions, etc. */
struct reiserfs_dentry_buf {
	struct dentry *xadir;
	int count;
	struct dentry *dentries[8];
};

static int
fill_with_dentries(void *buf, const char *name, int namelen, loff_t offset,
                   u64 ino, unsigned int d_type)
{
	struct reiserfs_dentry_buf *dbuf = buf;
	struct dentry *dentry;

	if (dbuf->count == ARRAY_SIZE(dbuf->dentries))
		return -ENOSPC;

	if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
		return 0;

	dentry = lookup_one_len(name, dbuf->xadir, namelen);
	if (IS_ERR(dentry)) {
		return PTR_ERR(dentry);
	} else if (!dentry->d_inode) {
		/* This should never happen */
		dput(dentry);
		return -ENODATA;
	}

	dbuf->dentries[dbuf->count++] = dentry;
	return 0;
}

typedef int(*xattr_action)(struct dentry *dentry, void *data);

static int reiserfs_for_each_xattr(struct inode *inode, xattr_action action,
                                   void *data)
{
	struct file *fp;
	struct dentry *dir;
	int err = 0;
	struct reiserfs_dentry_buf buf = {
		.count = 0,
	};

	/* Skip out, an xattr has no xattrs associated with it */
	if (is_reiserfs_priv_object(inode) ||
	    get_inode_sd_version(inode) == STAT_DATA_V1 ||
	    !reiserfs_xattrs(inode->i_sb)) {
		return 0;
	}
	dir = open_xa_dir(inode, XATTR_REPLACE);
	if (IS_ERR(dir)) {
		if (PTR_ERR(dir) != -ENODATA)
			err = PTR_ERR(dir);
		goto out;
	} else if (!dir->d_inode) {
		dput(dir);
		goto out;
	}

	fp = dentry_open(dir, NULL, O_RDWR|O_NOATIME|O_DIRECTORY);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		/* dentry_open dputs the dentry if it fails */
		goto out;
	}

	mutex_lock_nested(&fp->f_path.dentry->d_inode->i_mutex, I_MUTEX_XATTR);
	buf.xadir = dir;

	err = fp->f_op->readdir(fp, &buf, fill_with_dentries);
	while ((err == 0 || err == -ENOSPC) && buf.count) {
		int i;
		err = 0;

		for (i = 0; i < buf.count && buf.dentries[i]; i++) {
			int lerr = 0;
			struct dentry *dentry = buf.dentries[i];

			if (err == 0 && !S_ISDIR(dentry->d_inode->i_mode))
				lerr = action(dentry, data);

			dput(dentry);
			buf.dentries[i] = NULL;

			if (lerr)
				err = lerr;
		}
		buf.count = 0;
		if (err == 0)
			err = fp->f_op->readdir(fp, &buf, fill_with_dentries);
	}

	mutex_unlock(&fp->f_path.dentry->d_inode->i_mutex);

	if (err == 0)
		err = action(dir, data);

	fput(fp);

      out:
	return err;
}

static int delete_one_xattr(struct dentry *dentry, void *data)
{
	struct inode *dir = dentry->d_parent->d_inode;

	/* This is the xattr dir, handle specially. */
	if (S_ISDIR(dentry->d_inode->i_mode)) {
		int err;
		struct reiserfs_transaction_handle th;
		int nblocks = JOURNAL_PER_BALANCE_CNT * 2 + 2 +
		              4 * REISERFS_QUOTA_TRANS_BLOCKS(dir->i_sb);

		BUG_ON(dentry == dentry->d_parent);
		if (dir == dentry->d_inode) {
			printk ("dir = %s\n", dentry->d_parent->d_name.name);
			printk ("dentry = %s\n", dentry->d_name.name);
			return -EBUSY;
		}

		/* reiserfs_rmdir is going to start a transaction itself,
		 * but we need to start it outside of locking the xattr
		 * root to preserve lock ordering. */
		err = journal_begin(&th, dir->i_sb, nblocks);
		if (err == 0) {
			int err2;
			mutex_lock_nested(&dir->i_mutex, I_MUTEX_XATTR);
			err = vfs_rmdir(dir, dentry, NULL);
			err2 = journal_end(&th, dir->i_sb, nblocks);
			mutex_unlock(&dir->i_mutex);
			if (err2)
				err = err2;
		}
		return err;
	}

	return vfs_unlink(dir, dentry, NULL);
}

static int chown_one_xattr(struct dentry *dentry, void *data)
{
	struct iattr *attrs = data;
	return notify_change(dentry, NULL, attrs);
}

int reiserfs_delete_xattrs(struct inode *inode)
{
	return reiserfs_for_each_xattr(inode, delete_one_xattr, NULL);
}

int reiserfs_chown_xattrs(struct inode *inode, struct iattr *attrs)
{
	return reiserfs_for_each_xattr(inode, chown_one_xattr, attrs);
}

/* Actual operations that are exported to VFS-land */

/*
 * Inode operation getxattr()
 */
ssize_t
reiserfs_getxattr(struct dentry * dentry, const char *name, void *buffer,
		  size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct xattr_handler *handler;

	handler = find_xattr_handler_prefix(inode->i_sb->s_xattr, name);

	if (!handler || get_inode_sd_version(inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	return handler->get(inode, name, buffer, size);
}

/*
 * Inode operation setxattr()
 *
 * dentry->d_inode->i_mutex down
 */
int
reiserfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		  size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct xattr_handler *handler;

	handler = find_xattr_handler_prefix(inode->i_sb->s_xattr, name);

	if (!handler || get_inode_sd_version(inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	return handler->set(inode, name, value, size, flags);
}

/*
 * Inode operation removexattr()
 *
 * dentry->d_inode->i_mutex down
 */
int reiserfs_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	struct xattr_handler *handler;
	handler = find_xattr_handler_prefix(inode->i_sb->s_xattr, name);

	if (!handler || get_inode_sd_version(inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	return handler->set(inode, name, NULL, 0, XATTR_REPLACE);
}

struct listxattr_buf {
	size_t size;
	size_t pos;
	char *buf;
	struct inode *inode;
};

static int listxattr_filler(void *buf, const char *name, int namelen,
                            loff_t offset, u64 ino, unsigned int d_type)
{
	struct listxattr_buf *b = (struct listxattr_buf *)buf;
	size_t size;
	if (name[0] != '.' ||
	    (namelen != 1 && (name[1] != '.' || namelen != 2))) {
		struct xattr_handler *handler;
		handler = find_xattr_handler_prefix(b->inode->i_sb->s_xattr,
		                                    name);
		if (!handler)	/* Unsupported xattr name */
			return 0;
		if (b->buf) {
			size = handler->list(b->inode, b->buf + b->pos,
			                 b->size, name, namelen);
			if (size > b->size)
				return -ERANGE;
		} else {
			size = handler->list(b->inode, NULL, 0, name, namelen);
		}

		b->pos += size;
	}
	return 0;
}

/*
 * Inode operation listxattr()
 *
 * We totally ignore the generic listxattr here because it would be stupid
 * not to. Since the xattrs are organized in a directory, we can just
 * readdir to find them.
 */
ssize_t reiserfs_listxattr(struct dentry * dentry, char *buffer, size_t size)
{
	struct file *fp;
	struct dentry *dir;
	int err = 0;
	struct listxattr_buf buf = {
		.inode = dentry->d_inode,
		.buf = buffer,
		.size = buffer ? size : 0,
	};

	if (!dentry->d_inode)
		return -EINVAL;

	if (!reiserfs_xattrs(dentry->d_sb) ||
	    get_inode_sd_version(dentry->d_inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	dir = open_xa_dir(dentry->d_inode, XATTR_REPLACE);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		if (err == -ENODATA)
			err = 0;  /* Not an error if there aren't any xattrs */
		goto out;
	}

	fp = dentry_open(dir, NULL, O_RDWR|O_NOATIME|O_DIRECTORY);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		/* dentry_open dputs the dentry if it fails */
		goto out;
	}

	err = fp->f_op->readdir(fp, &buf, listxattr_filler);
	if (err)
		goto out_dir;

	err = buf.pos;

      out_dir:
	fput(fp);

      out:
	return err;
}

/* This will catch lookups from the fs root to .reiserfs_priv */
static int
xattr_lookup_poison(struct dentry *dentry, struct qstr *q1, struct qstr *name)
{
	struct dentry *priv_root = REISERFS_SB(dentry->d_sb)->priv_root;
	if (name->len == priv_root->d_name.len &&
	    name->hash == priv_root->d_name.hash &&
	    !memcmp(name->name, priv_root->d_name.name, name->len)) {
		return -ENOENT;
	} else if (q1->len == name->len &&
		   !memcmp(q1->name, name->name, name->len))
		return 0;
	return 1;
}

static struct dentry_operations xattr_lookup_poison_ops = {
	.d_compare = xattr_lookup_poison,
};

/* We need to take a copy of the mount flags since things like
 * MS_RDONLY don't get set until *after* we're called.
 * mount_flags != mount_options */
int reiserfs_xattr_init(struct super_block *s, int mount_flags)
{
	int err = 0;

	/* We need generation numbers to ensure that the oid mapping is correct
	 * v3.5 filesystems don't have them. */
	if (old_format_only(s)) {
		if (reiserfs_xattrs_optional(s)) {
			/* Old format filesystem, but optional xattrs have
			 * been enabled. Error out. */
			reiserfs_warning(s, "jdm-2005",
			                 "xattrs/ACLs not supported "
			                 "on pre-v3.6 format filesystems. "
			                 "Failing mount.");
			err = -EOPNOTSUPP;
		}
		return err;
	}

	/* If we don't have the privroot located yet - go find it */
	if (!REISERFS_SB(s)->priv_root) {
		struct dentry *dentry;
		dentry = lookup_one_len(PRIVROOT_NAME, s->s_root,
					strlen(PRIVROOT_NAME));
		if (!IS_ERR(dentry)) {
			if (!(mount_flags & MS_RDONLY) && !dentry->d_inode) {
				struct inode *inode = dentry->d_parent->d_inode;
				mutex_lock_nested(&inode->i_mutex,
				                  I_MUTEX_XATTR);
				err = inode->i_op->mkdir(inode, dentry, 0700);
				mutex_unlock(&inode->i_mutex);
				if (err) {
					dput(dentry);
					dentry = NULL;
				}

				if (dentry && dentry->d_inode)
					reiserfs_info(s, "Created %s - "
					              "reserved for xattr "
						      "storage.\n",
						      PRIVROOT_NAME);
			} else if (!dentry->d_inode) {
				dput(dentry);
				dentry = NULL;
			}
		} else
			err = PTR_ERR(dentry);

		if (!err && dentry) {
			s->s_root->d_op = &xattr_lookup_poison_ops;
			reiserfs_mark_inode_private(dentry->d_inode);
			REISERFS_SB(s)->priv_root = dentry;
		} else if (!(mount_flags & MS_RDONLY)) {	/* xattrs are unavailable */
			/* If we're read-only it just means that the dir hasn't been
			 * created. Not an error -- just no xattrs on the fs. We'll
			 * check again if we go read-write */
			reiserfs_warning(s, "jdm-20006",
			                 "xattrs/ACLs enabled and couldn't "
					 "find/create .reiserfs_priv. "
					 "Failing mount.");
			err = -EOPNOTSUPP;
			goto error;
		}
	}

	if (!err)
		s->s_xattr = reiserfs_xattr_handlers;

      error:
	/* This is only nonzero if there was an error initializing the xattr
	 * directory or if there is a condition where we don't support them. */
	if (err) {
		clear_bit(REISERFS_XATTRS_USER, &(REISERFS_SB(s)->s_mount_opt));
		clear_bit(REISERFS_POSIXACL, &(REISERFS_SB(s)->s_mount_opt));
	}

	/* The super_block MS_POSIXACL must mirror the (no)acl mount option. */
	s->s_flags = s->s_flags & ~MS_POSIXACL;
	if (reiserfs_posixacl(s))
		s->s_flags |= MS_POSIXACL;

	return err;
}

static int reiserfs_check_acl(struct inode *inode, int mask)
{
	struct posix_acl *acl;
	int error = -EAGAIN; /* do regular unix permission checks by default */

	acl = reiserfs_get_acl(inode, ACL_TYPE_ACCESS);

	if (acl) {
		if (!IS_ERR(acl)) {
			error = posix_acl_permission(inode, acl, mask);
			posix_acl_release(acl);
		} else if (PTR_ERR(acl) != -ENODATA)
			error = PTR_ERR(acl);
	}

	return error;
}

int reiserfs_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	/*
	 * We don't do permission checks on the internal objects.
	 * Permissions are determined by the "owning" object.
	 */
	if (is_reiserfs_priv_object(inode))
		return 0;

	/*
	 * Stat data v1 doesn't support ACLs.
	 */
	if (get_inode_sd_version(inode) == STAT_DATA_V1)
		return generic_permission(inode, mask, NULL);
	else
		return generic_permission(inode, mask, reiserfs_check_acl);
}
