/*
 * file.c - NTFS kernel file operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ntfs.h"

/**
 * ntfs_file_open - called when an inode is about to be opened
 * @vi:		inode to be opened
 * @filp:	file structure describing the inode
 *
 * Limit file size to the page cache limit on architectures where unsigned long
 * is 32-bits. This is the most we can do for now without overflowing the page
 * cache page index. Doing it this way means we don't run into problems because
 * of existing too large files. It would be better to allow the user to read
 * the beginning of the file but I doubt very much anyone is going to hit this
 * check on a 32-bit architecture, so there is no point in adding the extra
 * complexity required to support this.
 *
 * On 64-bit architectures, the check is hopefully optimized away by the
 * compiler.
 *
 * After the check passes, just call generic_file_open() to do its work.
 */
static int ntfs_file_open(struct inode *vi, struct file *filp)
{
	if (sizeof(unsigned long) < 8) {
		if (vi->i_size > MAX_LFS_FILESIZE)
			return -EFBIG;
	}
	return generic_file_open(vi, filp);
}

struct file_operations ntfs_file_ops = {
	.llseek		= generic_file_llseek,	  /* Seek inside file. */
	.read		= generic_file_read,	  /* Read from file. */
	.aio_read	= generic_file_aio_read,  /* Async read from file. */
	.readv		= generic_file_readv,	  /* Read from file. */
#ifdef NTFS_RW
	.write		= generic_file_write,	  /* Write to file. */
	.aio_write	= generic_file_aio_write, /* Async write to file. */
	.writev		= generic_file_writev,	  /* Write to file. */
	/*.release	= ,*/			  /* Last file is closed.  See
						     fs/ext2/file.c::
						     ext2_release_file() for
						     how to use this to discard
						     preallocated space for
						     write opened files. */
	/*.fsync	= ,*/			  /* Sync a file to disk.  See
						     fs/buffer.c::sys_fsync()
						     and file_fsync(). */
	/*.aio_fsync	= ,*/			  /* Sync all outstanding async
						     i/o operations on a
						     kiocb. */
#endif /* NTFS_RW */
	/*.ioctl	= ,*/			  /* Perform function on the
						     mounted filesystem. */
	.mmap		= generic_file_mmap,	  /* Mmap file. */
	.open		= ntfs_file_open,	  /* Open file. */
	.sendfile	= generic_file_sendfile,  /* Zero-copy data send with
						     the data source being on
						     the ntfs partition.  We
						     do not need to care about
						     the data destination. */
	/*.sendpage	= ,*/			  /* Zero-copy data send with
						     the data destination being
						     on the ntfs partition.  We
						     do not need to care about
						     the data source. */
};

struct inode_operations ntfs_file_inode_ops = {
#ifdef NTFS_RW
	.truncate	= ntfs_truncate,
	.setattr	= ntfs_setattr,
#endif /* NTFS_RW */
};

struct file_operations ntfs_empty_file_ops = {};

struct inode_operations ntfs_empty_inode_ops = {};
