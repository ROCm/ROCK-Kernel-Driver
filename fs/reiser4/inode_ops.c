/* Copyright 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Interface to VFS. Reiser4 inode_operations are defined here. */

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "coord.h"
#include "plugin/item/item.h"
#include "plugin/file/file.h"
#include "plugin/security/perm.h"
#include "plugin/disk_format/disk_format.h"
#include "plugin/plugin.h"
#include "plugin/plugin_set.h"
#include "plugin/object.h"
#include "txnmgr.h"
#include "jnode.h"
#include "znode.h"
#include "block_alloc.h"
#include "tree.h"
#include "log.h"
#include "vfs_ops.h"
#include "inode.h"
#include "page_cache.h"
#include "ktxnmgrd.h"
#include "super.h"
#include "reiser4.h"
#include "kattr.h"
#include "entd.h"
#include "emergency_flush.h"

#include <linux/profile.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/quotaops.h>
#include <linux/security.h>

/* inode operations */

static int reiser4_create(struct inode *, struct dentry *, int,
			  struct nameidata *);
static struct dentry *reiser4_lookup(struct inode *, struct dentry *,
				     struct nameidata *);
static int reiser4_link(struct dentry *, struct inode *, struct dentry *);
static int reiser4_unlink(struct inode *, struct dentry *);
static int reiser4_rmdir(struct inode *, struct dentry *);
static int reiser4_symlink(struct inode *, struct dentry *, const char *);
static int reiser4_mkdir(struct inode *, struct dentry *, int);
static int reiser4_mknod(struct inode *, struct dentry *, int, dev_t);
static int reiser4_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);
static int reiser4_readlink(struct dentry *, char *, int);
static int reiser4_follow_link(struct dentry *, struct nameidata *);
static void reiser4_truncate(struct inode *);
static int reiser4_permission(struct inode *, int, struct nameidata *);
static int reiser4_setattr(struct dentry *, struct iattr *);
static int reiser4_getattr(struct vfsmount *mnt, struct dentry *, struct kstat *);

#if 0
static int reiser4_setxattr(struct dentry *, const char *, void *, size_t, int);
static ssize_t reiser4_getxattr(struct dentry *, const char *, void *, size_t);
static ssize_t reiser4_listxattr(struct dentry *, char *, size_t);
static int reiser4_removexattr(struct dentry *, const char *);
#endif

reiser4_internal int invoke_create_method(struct inode *parent,
					  struct dentry *dentry,
					  reiser4_object_create_data * data);

/* ->create() VFS method in reiser4 inode_operations */
static int
reiser4_create(struct inode *parent	/* inode of parent
					 * directory */,
	       struct dentry *dentry	/* dentry of new object to
					 * create */,
	       int mode /* new object mode */,
	       struct nameidata *nameidata)
{
	reiser4_object_create_data data;
	xmemset(&data, 0, sizeof data);

	reiser4_stat_inc_at(parent->i_sb, vfs_calls.create);

	data.mode = S_IFREG | mode;
	data.id = UNIX_FILE_PLUGIN_ID;
	return invoke_create_method(parent, dentry, &data);
}

/* ->mkdir() VFS method in reiser4 inode_operations */
static int
reiser4_mkdir(struct inode *parent	/* inode of parent
					 * directory */ ,
	      struct dentry *dentry	/* dentry of new object to
					 * create */ ,
	      int mode /* new object's mode */ )
{
	reiser4_object_create_data data;

	reiser4_stat_inc_at(parent->i_sb, vfs_calls.mkdir);

	data.mode = S_IFDIR | mode;
	data.id = DIRECTORY_FILE_PLUGIN_ID;
	return invoke_create_method(parent, dentry, &data);
}

/* ->symlink() VFS method in reiser4 inode_operations */
static int
reiser4_symlink(struct inode *parent	/* inode of parent
					 * directory */ ,
		struct dentry *dentry	/* dentry of new object to
					 * create */ ,
		const char *linkname	/* pathname to put into
					 * symlink */ )
{
	reiser4_object_create_data data;

	reiser4_stat_inc_at(parent->i_sb, vfs_calls.symlink);

	data.name = linkname;
	data.id = SYMLINK_FILE_PLUGIN_ID;
	data.mode = S_IFLNK | S_IRWXUGO;
	return invoke_create_method(parent, dentry, &data);
}

/* ->mknod() VFS method in reiser4 inode_operations */
static int
reiser4_mknod(struct inode *parent /* inode of parent directory */ ,
	      struct dentry *dentry	/* dentry of new object to
					 * create */ ,
	      int mode /* new object's mode */ ,
	      dev_t rdev /* minor and major of new device node */ )
{
	reiser4_object_create_data data;

	reiser4_stat_inc_at(parent->i_sb, vfs_calls.mknod);

	data.mode = mode;
	data.rdev = rdev;
	data.id = SPECIAL_FILE_PLUGIN_ID;
	return invoke_create_method(parent, dentry, &data);
}

/* ->rename() inode operation */
static int
reiser4_rename(struct inode *old_dir, struct dentry *old, struct inode *new_dir, struct dentry *new)
{
	int result;
	reiser4_context ctx;

	assert("nikita-2314", old_dir != NULL);
	assert("nikita-2315", old != NULL);
	assert("nikita-2316", new_dir != NULL);
	assert("nikita-2317", new != NULL);

	init_context(&ctx, old_dir->i_sb);
	reiser4_stat_inc(vfs_calls.rename);

	result = perm_chk(old_dir, rename, old_dir, old, new_dir, new);
	if (result == 0) {
		dir_plugin *dplug;

		dplug = inode_dir_plugin(old_dir);
		if (dplug == NULL)
			result = RETERR(-ENOTDIR);
		else if (dplug->rename == NULL)
			result = RETERR(-EPERM);
		else
			result = dplug->rename(old_dir, old, new_dir, new);
	}
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/* reiser4_lookup() - entry point for ->lookup() method.

   This is a wrapper for lookup_object which is a wrapper for the directory
   plugin that does the lookup.

   This is installed in ->lookup() in reiser4_inode_operations.
*/
static struct dentry *
reiser4_lookup(struct inode *parent,	/* directory within which we are to
					 * look for the name specified in
					 * dentry */
	       struct dentry *dentry,	/* this contains the name that is to
					   be looked for on entry, and on exit
					   contains a filled in dentry with a
					   pointer to the inode (unless name
					   not found) */
	       struct nameidata *nameidata)
{
	dir_plugin *dplug;
	int retval;
	struct dentry *result;
	reiser4_context ctx;
	int (*lookup) (struct inode * parent_inode, struct dentry **dentry);

	assert("nikita-403", parent != NULL);
	assert("nikita-404", dentry != NULL);

	init_context(&ctx, parent->i_sb);
	reiser4_stat_inc(vfs_calls.lookup);

	/* find @parent directory plugin and make sure that it has lookup
	   method */
	dplug = inode_dir_plugin(parent);
	if (dplug != NULL && dplug->lookup != NULL)
		/* if parent directory has directory plugin with ->lookup
		 * method, use the latter to do lookup */
		lookup = dplug->lookup;
	else if (!reiser4_is_set(parent->i_sb, REISER4_NO_PSEUDO))
		/* even if there is no ->lookup method, pseudo file lookup
		 * should still be performed, but only unless we are in
		 * "no-pseudo" mode */
		lookup = lookup_pseudo_file;
	else
		lookup = NULL;
	if (lookup != NULL) {
		struct dentry *name;

		name = dentry;
		/* call its lookup method */
		retval = lookup(parent, &name);
		if (retval == 0) {
			if (name == NULL) {
				/*
				 * new object was looked up. Initialize it.
				 */
				struct inode *obj;
				file_plugin *fplug;

				obj = dentry->d_inode;
				assert("nikita-2645", obj != NULL);
				fplug = inode_file_plugin(obj);
				retval = fplug->bind(obj, parent);
			}
		} else if (retval == -ENOENT) {
			/* object not found */
			d_add(dentry, NULL);
			retval = 0;
			name = NULL;
		}

		if (retval == 0)
			/* success */
			result = name;
		else
			result = ERR_PTR(retval);
	} else
		result = ERR_PTR(-ENOTDIR);

	/* prevent balance_dirty_pages() from being called: we don't want to
	 * do this under directory i_sem. */
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/* ->readlink() inode method, returns content of symbolic link */
static int
reiser4_readlink(struct dentry *dentry, char *buf, int buflen)
{
	assert("vs-852", S_ISLNK(dentry->d_inode->i_mode));
	reiser4_stat_inc_at(dentry->d_inode->i_sb, vfs_calls.readlink);
	if (!dentry->d_inode->u.generic_ip || !inode_get_flag(dentry->d_inode, REISER4_GENERIC_PTR_USED))
		return RETERR(-EINVAL);
	return vfs_readlink(dentry, buf, buflen, dentry->d_inode->u.generic_ip);
}

/* ->follow_link() inode method. Follows a symbolic link */
static int
reiser4_follow_link(struct dentry *dentry, struct nameidata *data)
{
	assert("vs-851", S_ISLNK(dentry->d_inode->i_mode));

	reiser4_stat_inc_at(dentry->d_inode->i_sb, vfs_calls.follow_link);
	if (!dentry->d_inode->u.generic_ip || !inode_get_flag(dentry->d_inode, REISER4_GENERIC_PTR_USED))
		return RETERR(-EINVAL);
	return vfs_follow_link(data, dentry->d_inode->u.generic_ip);
}

/* ->setattr() inode operation

   Called from notify_change. */
static int
reiser4_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode;
	int result;
	reiser4_context ctx;

	assert("nikita-2269", attr != NULL);

	inode = dentry->d_inode;
	assert("vs-1108", inode != NULL);
	init_context(&ctx, inode->i_sb);
	reiser4_stat_inc(vfs_calls.setattr);
	result = perm_chk(inode, setattr, dentry, attr);
	if (result == 0) {
		if (!inode_get_flag(inode, REISER4_IMMUTABLE)) {
			file_plugin *fplug;

			fplug = inode_file_plugin(inode);
			assert("nikita-2271", fplug != NULL);
			assert("nikita-2296", fplug->setattr != NULL);
			result = fplug->setattr(inode, attr);
		} else
			result = RETERR(-E_REPEAT);
	}
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/* ->getattr() inode operation called (indirectly) by sys_stat(). */
static int
reiser4_getattr(struct vfsmount *mnt UNUSED_ARG, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode;
	int result;
	reiser4_context ctx;

	inode = dentry->d_inode;
	init_context(&ctx, inode->i_sb);
	reiser4_stat_inc(vfs_calls.getattr);
	result = perm_chk(inode, getattr, mnt, dentry, stat);
	if (result == 0) {
		file_plugin *fplug;

		fplug = inode_file_plugin(inode);
		assert("nikita-2295", fplug != NULL);
		assert("nikita-2297", fplug->getattr != NULL);
		result = fplug->getattr(mnt, dentry, stat);
	}
	reiser4_exit_context(&ctx);
	return result;
}

/* helper function: call object plugin to truncate file to @size */
static int
truncate_object(struct inode *inode /* object to truncate */ ,
		loff_t size /* size to truncate object to */ )
{
	file_plugin *fplug;
	int result;

	assert("nikita-1026", inode != NULL);
	assert("nikita-1027", is_reiser4_inode(inode));
	assert("nikita-1028", inode->i_sb != NULL);

	write_syscall_log("%llu %lli", get_inode_oid(inode), size);

	fplug = inode_file_plugin(inode);
	assert("vs-142", fplug != NULL);

	assert("nikita-2933", fplug->truncate != NULL);
	result = fplug->truncate(inode, size);
	if (result != 0)
		warning("nikita-1602", "Truncate error: %i for %lli", result,
			(unsigned long long)get_inode_oid(inode));

	write_syscall_log("ex");
	return result;
}

/* ->truncate() VFS method in reiser4 inode_operations */
static void
reiser4_truncate(struct inode *inode /* inode to truncate */ )
{
	reiser4_context ctx;

	assert("umka-075", inode != NULL);

	init_context(&ctx, inode->i_sb);
	reiser4_stat_inc(vfs_calls.truncate);
	ON_TRACE(TRACE_VFS_OPS, "TRUNCATE: i_ino %li to size %lli\n", inode->i_ino, inode->i_size);

	truncate_object(inode, inode->i_size);

	/* for mysterious reasons ->truncate() VFS call doesn't return
	   value  */
	reiser4_exit_context(&ctx);
}

/* ->permission() method in reiser4_inode_operations. */
static int
reiser4_permission(struct inode *inode /* object */ ,
		   int mask,	/* mode bits to check permissions
				 * for */
		   struct nameidata *nameidata)
{
	/* reiser4_context creation/destruction removed from here,
	   because permission checks currently don't require this.

	   Permission plugin have to create context itself if necessary. */
	assert("nikita-1687", inode != NULL);

	return perm_chk(inode, mask, inode, mask);
}

/* common part of both unlink and rmdir. */
static int
unlink_file(struct inode *parent /* parent directory */ ,
	    struct dentry *victim	/* name of object being
					 * unlinked */ )
{
	int result;
	dir_plugin *dplug;
	reiser4_context ctx;

	init_context(&ctx, parent->i_sb);
	write_syscall_log("%s", victim->d_name.name);

	assert("nikita-1435", parent != NULL);
	assert("nikita-1436", victim != NULL);

	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS, "unlink: %lli/%s\n",
		 get_inode_oid(parent), victim->d_name.name);

	dplug = inode_dir_plugin(parent);
	assert("nikita-1429", dplug != NULL);
	if (dplug->unlink != NULL)
		result = dplug->unlink(parent, victim);
	else
		result = RETERR(-EPERM);
	write_syscall_log("ex");
	/* @victim can be already removed from the disk by this time. Inode is
	   then marked so that iput() wouldn't try to remove stat data. But
	   inode itself is still there.
	*/
	/* we cannot release directory semaphore here, because name has
	 * already been deleted, but dentry (@victim) still exists. */
	/* prevent balance_dirty_pages() from being called: we don't want to
	 * do this under directory i_sem. */

	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/* ->unlink() VFS method in reiser4 inode_operations

   remove link from @parent directory to @victim object: delegate work
   to object plugin
*/
/* Audited by: umka (2002.06.12) */
static int
reiser4_unlink(struct inode *parent /* parent directory */ ,
	       struct dentry *victim	/* name of object being
					 * unlinked */ )
{
	assert("nikita-2011", parent != NULL);
	assert("nikita-2012", victim != NULL);
	assert("nikita-2013", victim->d_inode != NULL);
	reiser4_stat_inc_at(parent->i_sb,vfs_calls.unlink);
	if (inode_dir_plugin(victim->d_inode) == NULL)
		return unlink_file(parent, victim);
	else
		return RETERR(-EISDIR);
}

/* ->rmdir() VFS method in reiser4 inode_operations

   The same as unlink, but only for directories.

*/
/* Audited by: umka (2002.06.12) */
static int
reiser4_rmdir(struct inode *parent /* parent directory */ ,
	      struct dentry *victim	/* name of directory being
					 * unlinked */ )
{
	assert("nikita-2014", parent != NULL);
	assert("nikita-2015", victim != NULL);
	assert("nikita-2016", victim->d_inode != NULL);

	reiser4_stat_inc_at(parent->i_sb, vfs_calls.rmdir);
	if (inode_dir_plugin(victim->d_inode) != NULL)
		/* there is no difference between unlink and rmdir for
		   reiser4 */
		return unlink_file(parent, victim);
	else
		return RETERR(-ENOTDIR);
}

/* ->link() VFS method in reiser4 inode_operations

   entry point for ->link() method.

   This is installed as ->link inode operation for reiser4
   inodes. Delegates all work to object plugin
*/
/* Audited by: umka (2002.06.12) */
static int
reiser4_link(struct dentry *existing	/* dentry of existing
					 * object */ ,
	     struct inode *parent /* parent directory */ ,
	     struct dentry *where /* new name for @existing */ )
{
	int result;
	dir_plugin *dplug;
	reiser4_context ctx;

	assert("umka-080", existing != NULL);
	assert("nikita-1031", parent != NULL);

	init_context(&ctx, parent->i_sb);
	context_set_commit_async(&ctx);
	reiser4_stat_inc(vfs_calls.link);

	dplug = inode_dir_plugin(parent);
	assert("nikita-1430", dplug != NULL);
	if (dplug->link != NULL) {
		result = dplug->link(parent, existing, where);
		if (result == 0)
			d_instantiate(where, existing->d_inode);
	} else {
		result = RETERR(-EPERM);
	}
	up(&existing->d_inode->i_sem);
	up(&parent->i_sem);
	reiser4_exit_context(&ctx);
	down(&parent->i_sem);
	down(&existing->d_inode->i_sem);
	return result;
}

/* call ->create() directory plugin method. */
reiser4_internal int
invoke_create_method(struct inode *parent /* parent directory */ ,
		     struct dentry *dentry	/* dentry of new
						 * object */ ,
		     reiser4_object_create_data * data	/* information
							 * necessary
							 * to create
							 * new
							 * object */ )
{
	int result;
	dir_plugin *dplug;
	reiser4_context ctx;

	init_context(&ctx, parent->i_sb);
	context_set_commit_async(&ctx);
	write_syscall_log("%s %o", dentry->d_name.name, data->mode);

	assert("nikita-426", parent != NULL);
	assert("nikita-427", dentry != NULL);
	assert("nikita-428", data != NULL);

	dplug = inode_dir_plugin(parent);
	if (dplug == NULL)
		result = RETERR(-ENOTDIR);
	else if (dplug->create_child != NULL) {
		struct inode *child;

		child = NULL;

		data->parent = parent;
		data->dentry = dentry;

		result = dplug->create_child(data, &child);
		if (unlikely(result != 0)) {
			if (child != NULL) {
				/*
				 * what we actually want to check in the
				 * assertion below is that @child only
				 * contains items that iput()->... is going to
				 * remove (usually stat-data). Obvious check
				 * for child->i_size == 0 doesn't work for
				 * symlinks.
				 */
				assert("nikita-3140", S_ISLNK(child->i_mode) ||
				       child->i_size == 0);
				reiser4_make_bad_inode(child);
				iput(child);
			}
		} else {
			d_instantiate(dentry, child);
			ON_TRACE(TRACE_VFS_OPS, "create: %s (%o) %llu\n",
				 dentry->d_name.name,
				 data->mode, get_inode_oid(child));
		}
	} else
		result = RETERR(-EPERM);

	write_syscall_log("ex");

	reiser4_exit_context(&ctx);
	return result;
}

struct inode_operations reiser4_inode_operations = {
	.create = reiser4_create,	/* d */
	.lookup = reiser4_lookup,	/* d */
	.link = reiser4_link,	/* d */
	.unlink = reiser4_unlink,	/* d */
	.symlink = reiser4_symlink,	/* d */
	.mkdir = reiser4_mkdir,	/* d */
	.rmdir = reiser4_rmdir,	/* d */
	.mknod = reiser4_mknod,	/* d */
	.rename = reiser4_rename,	/* d */
	.readlink = NULL,
	.follow_link = NULL,
	.truncate = reiser4_truncate,	/* d */
	.permission = reiser4_permission,	/* d */
	.setattr = reiser4_setattr,	/* d */
	.getattr = reiser4_getattr,	/* d */
};

struct inode_operations reiser4_symlink_inode_operations = {
	.setattr = reiser4_setattr,	/* d */
	.getattr = reiser4_getattr,	/* d */
	.readlink = reiser4_readlink,
	.follow_link = reiser4_follow_link
};

struct inode_operations reiser4_special_inode_operations = {
	.setattr = reiser4_setattr,	/* d */
	.getattr = reiser4_getattr	/* d */
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
