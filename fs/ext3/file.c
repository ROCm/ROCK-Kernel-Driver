/*
 *  linux/fs/ext3/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext3 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 *	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/jbd.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>
#include <linux/smp_lock.h>

/*
 * Called when an inode is released. Note that this is different
 * from ext3_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ext3_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE)
		ext3_discard_prealloc (inode);
	return 0;
}

/*
 * Called when an inode is about to be opened.
 * We use this to disallow opening RW large files on 32bit systems if
 * the caller didn't specify O_LARGEFILE.  On 64bit systems we force
 * on this flag in sys_open.
 */
static int ext3_open_file (struct inode * inode, struct file * filp)
{
	if (!(filp->f_flags & O_LARGEFILE) &&
	    inode->i_size > 0x7FFFFFFFLL)
		return -EFBIG;
	return 0;
}

/*
 * ext3_file_write().
 *
 * Most things are done in ext3_prepare_write() and ext3_commit_write().
 */

static ssize_t
ext3_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;

	/*
	 * Nasty: if the file is subject to synchronous writes then we need
	 * to force generic_osync_inode() to call ext3_write_inode().
	 * We do that by marking the inode dirty.  This adds much more
	 * computational expense than we need, but we're going to sync
	 * anyway.
	 */
	if (IS_SYNC(inode) || (file->f_flags & O_SYNC))
		mark_inode_dirty(inode);

	return generic_file_write(file, buf, count, ppos);
}

struct file_operations ext3_file_operations = {
	llseek:		generic_file_llseek,	/* BKL held */
	read:		generic_file_read,	/* BKL not held.  Don't need */
	write:		ext3_file_write,	/* BKL not held.  Don't need */
	ioctl:		ext3_ioctl,		/* BKL held */
	mmap:		generic_file_mmap,
	open:		ext3_open_file,		/* BKL not held.  Don't need */
	release:	ext3_release_file,	/* BKL not held.  Don't need */
	fsync:		ext3_sync_file,		/* BKL held */
};

struct inode_operations ext3_file_inode_operations = {
	truncate:	ext3_truncate,		/* BKL held */
	setattr:	ext3_setattr,		/* BKL held */
};

