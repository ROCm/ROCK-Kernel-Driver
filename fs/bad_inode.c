/*
 *  linux/fs/bad_inode.c
 *
 *  Copyright (C) 1997, Stephen Tweedie
 *
 *  Provide stub functions for unreadable inodes
 *
 *  Fabian Frederick : August 2003 - All file operations assigned to EIO
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/smp_lock.h>
#include <linux/namei.h>

static int return_EIO_int(void)
{
	return -EIO;
}
#define EIO_ERROR_INT ((void *) (return_EIO_int))

static ssize_t return_EIO_ssize(void)
{
	return -EIO;
}
#define EIO_ERROR_SSIZE ((void *) (return_EIO_ssize))

static long return_EIO_long(void)
{
	return -EIO;
}
#define EIO_ERROR_LOFF ((void *) (return_EIO_loff))

static loff_t return_EIO_loff(void)
{
	return -EIO;
}
#define EIO_ERROR_LONG ((void *) (return_EIO_long))

static long * return_EIO_ptr(void)
{
	return ERR_PTR(-EIO);
}
#define EIO_ERROR_PTR ((void *) (return_EIO_ptr))

static const struct file_operations bad_file_ops =
{
	.llseek		= EIO_ERROR_LOFF,
	.read		= EIO_ERROR_SSIZE,
	.write		= EIO_ERROR_SSIZE,
	.aio_read	= EIO_ERROR_SSIZE,
	.aio_write	= EIO_ERROR_SSIZE,
	.readdir	= EIO_ERROR_INT,
	.poll		= EIO_ERROR_INT,
	.ioctl		= EIO_ERROR_INT,
	.unlocked_ioctl	= EIO_ERROR_LONG,
	.compat_ioctl	= EIO_ERROR_LONG,
	.mmap		= EIO_ERROR_INT,
	.open		= EIO_ERROR_INT,
	.flush		= EIO_ERROR_INT,
	.release	= EIO_ERROR_INT,
	.fsync		= EIO_ERROR_INT,
	.aio_fsync	= EIO_ERROR_INT,
	.fasync		= EIO_ERROR_INT,
	.lock		= EIO_ERROR_INT,
	.readv		= EIO_ERROR_SSIZE,
	.writev		= EIO_ERROR_SSIZE,
	.sendfile	= EIO_ERROR_SSIZE,
	.sendpage	= EIO_ERROR_SSIZE,
	.get_unmapped_area = EIO_ERROR_LONG,
	.check_flags	= EIO_ERROR_INT,
	.dir_notify	= EIO_ERROR_INT,
	.flock		= EIO_ERROR_INT,
	.splice_write	= EIO_ERROR_SSIZE,
	.splice_read	= EIO_ERROR_SSIZE,
};

static struct inode_operations bad_inode_ops =
{
	.create		= EIO_ERROR_INT,
	.lookup		= EIO_ERROR_PTR,
	.link		= EIO_ERROR_INT,
	.unlink		= EIO_ERROR_INT,
	.symlink	= EIO_ERROR_INT,
	.mkdir		= EIO_ERROR_INT,
	.rmdir		= EIO_ERROR_INT,
	.mknod		= EIO_ERROR_INT,
	.rename		= EIO_ERROR_INT,
	.readlink	= EIO_ERROR_INT,
	/* follow_link must be no-op, otherwise unmounting this inode
	   won't work */
	/* put_link is a void function */
	/* truncate is a void function */
	.permission	= EIO_ERROR_INT,
	.getattr	= EIO_ERROR_INT,
	.setattr	= EIO_ERROR_INT,
	.setxattr	= EIO_ERROR_INT,
	.getxattr	= EIO_ERROR_SSIZE,
	.listxattr	= EIO_ERROR_SSIZE,
	.removexattr	= EIO_ERROR_INT,
	/* truncate_range is a void function */
};


/*
 * When a filesystem is unable to read an inode due to an I/O error in
 * its read_inode() function, it can call make_bad_inode() to return a
 * set of stubs which will return EIO errors as required. 
 *
 * We only need to do limited initialisation: all other fields are
 * preinitialised to zero automatically.
 */
 
/**
 *	make_bad_inode - mark an inode bad due to an I/O error
 *	@inode: Inode to mark bad
 *
 *	When an inode cannot be read due to a media or remote network
 *	failure this function makes the inode "bad" and causes I/O operations
 *	on it to fail from this point on.
 */
 
void make_bad_inode(struct inode * inode) 
{
	remove_inode_hash(inode);

	inode->i_mode = S_IFREG;
	inode->i_atime = inode->i_mtime = inode->i_ctime =
		current_fs_time(inode->i_sb);
	inode->i_op = &bad_inode_ops;	
	inode->i_fop = &bad_file_ops;	
}
EXPORT_SYMBOL(make_bad_inode);

/*
 * This tests whether an inode has been flagged as bad. The test uses
 * &bad_inode_ops to cover the case of invalidated inodes as well as
 * those created by make_bad_inode() above.
 */
 
/**
 *	is_bad_inode - is an inode errored
 *	@inode: inode to test
 *
 *	Returns true if the inode in question has been marked as bad.
 */
 
int is_bad_inode(struct inode * inode) 
{
	return (inode->i_op == &bad_inode_ops);	
}

EXPORT_SYMBOL(is_bad_inode);
