/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/*
 * Interface to VFS. Reiser4 file_operations are defined here.
 *
 * This file contains definitions of functions that are installed into ->i_fop
 * field of reiser4 inodes.
 *
 * By the most part these functions simply find object plugin of inode
 * involved, and call appropriate plugin method to do the actual work.
 */

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
#include <linux/mpage.h>
#include <linux/backing-dev.h>
#include <linux/quotaops.h>
#include <linux/security.h>


/* file operations */

static loff_t reiser4_llseek(struct file *, loff_t, int);
static ssize_t reiser4_read(struct file *, char *, size_t, loff_t *);
static ssize_t reiser4_write(struct file *, const char *, size_t, loff_t *);
static int reiser4_readdir(struct file *, void *, filldir_t);
static int reiser4_ioctl(struct inode *, struct file *, unsigned int cmd, unsigned long arg);
static int reiser4_mmap(struct file *, struct vm_area_struct *);
static int reiser4_release(struct inode *, struct file *);
static int reiser4_fsync(struct file *, struct dentry *, int datasync);
static int reiser4_open(struct inode *, struct file *);
static ssize_t reiser4_sendfile(struct file *, loff_t *, size_t, read_actor_t, void __user *);

#if 0
static unsigned int reiser4_poll(struct file *, struct poll_table_struct *);
static int reiser4_flush(struct file *);
static int reiser4_fasync(int, struct file *, int);
static int reiser4_lock(struct file *, int, struct file_lock *);
static ssize_t reiser4_readv(struct file *, const struct iovec *, unsigned long, loff_t *);
static ssize_t reiser4_writev(struct file *, const struct iovec *, unsigned long, loff_t *);
static ssize_t reiser4_sendpage(struct file *, struct page *, int, size_t, loff_t *, int);
static unsigned long reiser4_get_unmapped_area(struct file *, unsigned long,
					       unsigned long, unsigned long, unsigned long);
#endif

/*
 * ->llseek() file operation for reiser4. Calls ->seek() method of object
 * plugin.
 */
static loff_t
reiser4_llseek(struct file *file, loff_t off, int origin)
{
	loff_t result;
	file_plugin *fplug;
	struct inode *inode = file->f_dentry->d_inode;
	loff_t(*seek_fn) (struct file *, loff_t, int);
	reiser4_context ctx;

	init_context(&ctx, inode->i_sb);
	reiser4_stat_inc(vfs_calls.llseek);

	ON_TRACE(TRACE_VFS_OPS,
		 "llseek: (i_ino %li, size %lld): off %lli, origin %d\n", inode->i_ino, inode->i_size, off, origin);

	fplug = inode_file_plugin(inode);
	assert("nikita-2291", fplug != NULL);
	seek_fn = fplug->seek ? : generic_file_llseek;
	result = seek_fn(file, off, origin);
	reiser4_exit_context(&ctx);
	return result;
}

/* reiser4_readdir() - our readdir() method.

   readdir(2)/getdents(2) interface is based on implicit assumption that
   readdir can be restarted from any particular point by supplying file
   system with off_t-full of data. That is, file system fill ->d_off
   field in struct dirent and later user passes ->d_off to the
   seekdir(3), which is, actually, implemented by glibc as lseek(2) on
   directory.

   Reiser4 cannot restart readdir from 64 bits of data, because two last
   components of the key of directory entry are unknown, which given 128
   bits: locality and type fields in the key of directory entry are
   always known, to start readdir() from given point objectid and offset
   fields have to be filled.

   See plugin/dir/dir.c:readdir_common() for the details of our solution.
*/
static int
reiser4_readdir(struct file *f /* directory file being read */ ,
		void *dirent /* opaque data passed to us by VFS */ ,
		filldir_t filldir	/* filler function passed to us
					 * by VFS */ )
{
	dir_plugin *dplug;
	int result;
	struct inode *inode;
	reiser4_context ctx;

	inode = f->f_dentry->d_inode;
	init_context(&ctx, inode->i_sb);
	write_syscall_log("%s", f->f_dentry->d_name.name);
	reiser4_stat_inc(vfs_calls.readdir);

	dplug = inode_dir_plugin(inode);
	if ((dplug != NULL) && (dplug->readdir != NULL))
		result = dplug->readdir(f, dirent, filldir);
	else
		result = RETERR(-ENOTDIR);

	/*
	 * directory st_atime is updated by callers (if necessary).
	 */
	write_syscall_log("ex");
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/*
  reiser4_ioctl - handler for ioctl for inode supported commands:
*/
static int
reiser4_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int result;
	reiser4_context ctx;

	init_context(&ctx, inode->i_sb);
	write_syscall_log("%s", filp->f_dentry->d_name.name);
	reiser4_stat_inc(vfs_calls.ioctl);

	if (inode_file_plugin(inode)->ioctl == NULL)
		result = -ENOSYS;
	else
		result = inode_file_plugin(inode)->ioctl(inode, filp, cmd, arg);

	write_syscall_log("ex");
	reiser4_exit_context(&ctx);
	return result;
}

/* ->mmap() VFS method in reiser4 file_operations */
static int
reiser4_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode;
	int result;
	reiser4_context ctx;

	init_context(&ctx, file->f_dentry->d_inode->i_sb);
	write_syscall_log("%s", file->f_dentry->d_name.name);
	reiser4_stat_inc(vfs_calls.mmap);

	ON_TRACE(TRACE_VFS_OPS, "MMAP: (i_ino %lli, size %lld)\n",
		 get_inode_oid(file->f_dentry->d_inode),
		 file->f_dentry->d_inode->i_size);

	inode = file->f_dentry->d_inode;
	assert("nikita-2936", inode_file_plugin(inode)->mmap != NULL);
	result = inode_file_plugin(inode)->mmap(file, vma);
	write_syscall_log("ex");
	reiser4_exit_context(&ctx);
	return result;
}

/* reiser4 implementation of ->read() VFS method, member of reiser4 struct file_operations

 reads some part of a file from the filesystem into the user space buffer

 gets the plugin for the file and calls its read method which does everything except some initialization

*/
static ssize_t
reiser4_read(struct file *file /* file to read from */ ,
	     char *buf		/* user-space buffer to put data read
				 * from the file */ ,
	     size_t count /* bytes to read */ ,
	     loff_t * off	/* current position within the file, which needs to be increased by the act of reading. Reads
				 * start from here. */ )
{
	ssize_t result;
	struct inode *inode;
	reiser4_context ctx;

	assert("umka-072", file != NULL);
	assert("umka-073", buf != NULL);
	assert("umka-074", off != NULL);

	inode = file->f_dentry->d_inode;
	init_context(&ctx, inode->i_sb);
	write_syscall_log("%s", file->f_dentry->d_name.name);
	reiser4_stat_inc(vfs_calls.read);

	ON_TRACE(TRACE_VFS_OPS,
		 "READ: (i_ino %li, size %lld): %u bytes from pos %lli\n",
		 inode->i_ino, inode->i_size, count, *off);

	result = perm_chk(inode, read, file, buf, count, off);
	if (likely(result == 0)) {
		file_plugin *fplug;

		fplug = inode_file_plugin(inode);
		assert("nikita-417", fplug != NULL);
		assert("nikita-2935", fplug->write != NULL);

		/* unix_file_read is one method that might be invoked below */
		result = fplug->read(file, buf, count, off);
	}
	write_syscall_log("ex");
	reiser4_exit_context(&ctx);
	return result;
}

/* ->write() VFS method in reiser4 file_operations */
static ssize_t
reiser4_write(struct file *file /* file to write on */ ,
	      const char *buf	/* user-space buffer to get data
				 * to write into the file */ ,
	      size_t size /* bytes to write */ ,
	      loff_t * off	/* offset to start writing
				 * from. This is updated to indicate
				 * actual number of bytes written */ )
{
	struct inode *inode;
	ssize_t result;
	reiser4_context ctx;

	assert("nikita-1421", file != NULL);
	assert("nikita-1422", buf != NULL);
	assert("nikita-1424", off != NULL);

	inode = file->f_dentry->d_inode;
	init_context(&ctx, inode->i_sb);
	write_syscall_log("%s", file->f_dentry->d_name.name);
	reiser4_stat_inc(vfs_calls.write);

	ON_TRACE(TRACE_VFS_OPS,
		 "WRITE: (i_ino %li, size %lld): %u bytes to pos %lli\n", inode->i_ino, inode->i_size, size, *off);

	result = perm_chk(inode, write, file, buf, size, off);
	if (likely(result == 0)) {
		file_plugin *fplug;

		fplug = inode_file_plugin(inode);
		assert("nikita-2934", fplug->read != NULL);

		result = fplug->write(file, buf, size, off);
	}
	write_syscall_log("ex");
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/* Release reiser4 file. This is f_op->release() method. Called when last
   holder closes a file */
static int
reiser4_release(struct inode *i /* inode released */ ,
		struct file *f /* file released */ )
{
	file_plugin *fplug;
	int result;
	reiser4_context ctx;

	assert("umka-081", i != NULL);
	assert("nikita-1447", f != NULL);

	init_context(&ctx, i->i_sb);
	fplug = inode_file_plugin(i);
	assert("umka-082", fplug != NULL);

	ON_TRACE(TRACE_VFS_OPS,
		 "RELEASE: (i_ino %li, size %lld)\n", i->i_ino, i->i_size);

	if (fplug->release != NULL && get_current_context() == &ctx)
		result = fplug->release(i, f);
	else
		/*
		  no ->release method defined, or we are within reiser4
		  context already. How latter is possible? Simple:

		  (gdb) bt
		  #0  get_exclusive_access ()
		  #2  0xc01e56d3 in release_unix_file ()
		  #3  0xc01c3643 in reiser4_release ()
		  #4  0xc014cae0 in __fput ()
		  #5  0xc013ffc3 in remove_vm_struct ()
		  #6  0xc0141786 in exit_mmap ()
		  #7  0xc0118480 in mmput ()
		  #8  0xc0133205 in oom_kill ()
		  #9  0xc01332d1 in out_of_memory ()
		  #10 0xc013bc1d in try_to_free_pages ()
		  #11 0xc013427b in __alloc_pages ()
		  #12 0xc013f058 in do_anonymous_page ()
		  #13 0xc013f19d in do_no_page ()
		  #14 0xc013f60e in handle_mm_fault ()
		  #15 0xc01131e5 in do_page_fault ()
		  #16 0xc0104935 in error_code ()
		  #17 0xc025c0c6 in __copy_to_user_ll ()
		  #18 0xc01d496f in read_tail ()
		  #19 0xc01e4def in read_unix_file ()
		  #20 0xc01c3504 in reiser4_read ()
		  #21 0xc014bd4f in vfs_read ()
		  #22 0xc014bf66 in sys_read ()
		*/
		result = 0;

	reiser4_free_file_fsdata(f);

	reiser4_exit_context(&ctx);
	return result;
}

/*
 * ->open file operation for reiser4. This is optional method. It's only
 * present for mounts that support pseudo files. When "nopseudo" mount option
 * is used, this method is zeroed, which speeds open(2) system call a bit.
 */
static int
reiser4_open(struct inode * inode, struct file * file)
{
	int result;

	reiser4_context ctx;
	file_plugin *fplug;

	init_context(&ctx, inode->i_sb);
	reiser4_stat_inc(vfs_calls.open);
	fplug = inode_file_plugin(inode);

	if (fplug->open != NULL)
		result = fplug->open(inode, file);
	else
		result = 0;

	reiser4_exit_context(&ctx);
	return result;
}

/* ->fsync file operation for reiser4. */
static int
reiser4_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int result;
	reiser4_context ctx;
	file_plugin *fplug;
	struct inode *inode;

	inode = dentry->d_inode;
	init_context(&ctx, inode->i_sb);
	fplug = inode_file_plugin(inode);
	if (fplug->sync != NULL)
		result = fplug->sync(inode, datasync);
	else
		result = 0;
	context_set_commit_async(&ctx);
	reiser4_exit_context(&ctx);
	return result;
}

/* Reads @count bytes from @file and calls @actor for every read page. This is
   needed for loop back devices support. */
static ssize_t reiser4_sendfile(struct file *file, loff_t *ppos,
				size_t count, read_actor_t actor,
				void __user *target)
{
	int result;
	file_plugin *fplug;
	reiser4_context ctx;
	struct inode *inode;

	inode = file->f_dentry->d_inode;
	init_context(&ctx, inode->i_sb);

	fplug = inode_file_plugin(inode);

	if (fplug->sendfile != NULL)
		result = fplug->sendfile(file, ppos, count, actor, target);
	else
		result = RETERR(-EINVAL);

	reiser4_exit_context(&ctx);
	return result;
}


struct file_operations reiser4_file_operations = {
	.llseek   = reiser4_llseek,	/* d */
	.read     = reiser4_read,	/* d */
	.write    = reiser4_write,	/* d */
	.readdir  = reiser4_readdir,	/* d */
/* 	.poll              = reiser4_poll, */
	.ioctl    = reiser4_ioctl,
	.mmap     = reiser4_mmap,	/* d */
 	.open              = reiser4_open,
/* 	.flush             = reiser4_flush, */
	.release  = reiser4_release,	/* d */
 	.fsync    = reiser4_fsync        /* d */,
	.sendfile = reiser4_sendfile,
/* 	.fasync            = reiser4_fasync, */
/* 	.lock              = reiser4_lock, */
/* 	.readv             = reiser4_readv, */
/* 	.writev            = reiser4_writev, */
/* 	.sendpage          = reiser4_sendpage, */
/* 	.get_unmapped_area = reiser4_get_unmapped_area */
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
