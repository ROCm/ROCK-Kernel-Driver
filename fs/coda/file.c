/*
 * File operations for Coda.
 * Original version: (C) 1996 Peter Braam 
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>
#include <linux/coda_proc.h>

static ssize_t
coda_file_write(struct file *file,const char *buf,size_t count,loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct inode *container = inode->i_mapping->host;
	ssize_t n;

        down(&container->i_sem);

	n = generic_file_write(file, buf, count, ppos);
	inode->i_size = container->i_size;

        up(&container->i_sem);

	return n;
}

/* exported from this file (used for dirs) */
int coda_fsync(struct file *coda_file, struct dentry *coda_dentry, int datasync)
{
	struct inode *inode = coda_dentry->d_inode;
	struct dentry cont_dentry;
	int result = 0;
	ENTRY;
	coda_vfs_stat.fsync++;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;

	if ( inode->i_mapping == &inode->i_data ) {
		printk("coda_fsync: no container inode!\n");
                return -1; 
        }

	cont_dentry.d_inode = inode->i_mapping->host;
  
	down(&cont_dentry.d_inode->i_sem);
	result = file_fsync(NULL, &cont_dentry, datasync);
	up(&cont_dentry.d_inode->i_sem);

	if ( result == 0 && datasync == 0 ) {
		lock_kernel();
		result = venus_fsync(inode->i_sb, coda_i2f(inode));
		unlock_kernel();
	}

	return result;
}

struct file_operations coda_file_operations = {
	read:		generic_file_read,
	write:		coda_file_write,
	mmap:		generic_file_mmap,
	open:		coda_open,
	release:	coda_release,
	fsync:		coda_fsync,
};

