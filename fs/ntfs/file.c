/*
 * file.c - NTFS kernel file operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001 Anton Altaparmakov.
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

struct file_operations ntfs_file_ops = {
	llseek:			generic_file_llseek,	/* Seek inside file. */
	read:			generic_file_read,	/* Read from file. */
	write:			NULL,			/* . */
	readdir:		NULL,			/* . */
	poll:			NULL,			/* . */
	ioctl:			NULL,			/* . */
	mmap:			generic_file_mmap,	/* Mmap file. */
	open:			generic_file_open,	/* Open file. */
	flush:			NULL,			/* . */
	release:		NULL,			/* . */
	fsync:			NULL,			/* . */
	fasync:			NULL,			/* . */
	lock:			NULL,			/* . */
	readv:			NULL,			/* . */
	writev:			NULL,			/* . */
	sendpage:		NULL,			/* . */
	get_unmapped_area:	NULL,			/* . */
};

struct inode_operations ntfs_file_inode_ops = {
	create:		NULL,		/* . */
	lookup:		NULL,		/* . */
	link:		NULL,		/* . */
	unlink:		NULL,		/* . */
	symlink:	NULL,		/* . */
	mkdir:		NULL,		/* . */
	rmdir:		NULL,		/* . */
	mknod:		NULL,		/* . */
	rename:		NULL,		/* . */
	readlink:	NULL,		/* . */
	follow_link:	NULL,		/* . */
	truncate:	NULL,		/* . */
	permission:	NULL,		/* . */
	revalidate:	NULL,		/* . */
	setattr:	NULL,		/* . */
	getattr:	NULL,		/* . */
};

#if 0
/* NOTE: read, write, poll, fsync, readv, writev can be called without the big
 * kernel lock held in all filesystems. */
struct file_operations {
	struct module *owner;
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char *, size_t, loff_t *);
	int (*readdir) (struct file *, void *, filldir_t);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	int (*ioctl) (struct inode *, struct file *, unsigned int,
			unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*flush) (struct file *);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, struct dentry *, int datasync);
	int (*fasync) (int, struct file *, int);
	int (*lock) (struct file *, int, struct file_lock *);
	ssize_t (*readv) (struct file *, const struct iovec *, unsigned long,
			loff_t *);
	ssize_t (*writev) (struct file *, const struct iovec *, unsigned long,
			loff_t *);
	ssize_t (*sendpage) (struct file *, struct page *, int, size_t,
			loff_t *, int);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long,
			unsigned long, unsigned long, unsigned long);
};

struct inode_operations {
	int (*create) (struct inode *,struct dentry *,int);
	struct dentry * (*lookup) (struct inode *,struct dentry *);
	int (*link) (struct dentry *,struct inode *,struct dentry *);
	int (*unlink) (struct inode *,struct dentry *);
	int (*symlink) (struct inode *,struct dentry *,const char *);
	int (*mkdir) (struct inode *,struct dentry *,int);
	int (*rmdir) (struct inode *,struct dentry *);
	int (*mknod) (struct inode *,struct dentry *,int,int);
	int (*rename) (struct inode *, struct dentry *,
			struct inode *, struct dentry *);
	int (*readlink) (struct dentry *, char *,int);
	int (*follow_link) (struct dentry *, struct nameidata *);
	void (*truncate) (struct inode *);
	int (*permission) (struct inode *, int);
	int (*revalidate) (struct dentry *);
	int (*setattr) (struct dentry *, struct iattr *);
	int (*getattr) (struct dentry *, struct iattr *);
};
#endif

struct file_operations ntfs_empty_file_ops = {
	llseek:			NULL,			/* . */
	read:			NULL,			/* . */
	write:			NULL,			/* . */
	readdir:		NULL,			/* . */
	poll:			NULL,			/* . */
	ioctl:			NULL,			/* . */
	mmap:			NULL,			/* . */
	open:			NULL,			/* . */
	flush:			NULL,			/* . */
	release:		NULL,			/* . */
	fsync:			NULL,			/* . */
	fasync:			NULL,			/* . */
	lock:			NULL,			/* . */
	readv:			NULL,			/* . */
	writev:			NULL,			/* . */
	sendpage:		NULL,			/* . */
	get_unmapped_area:	NULL,			/* . */
};

struct inode_operations ntfs_empty_inode_ops = {
	create:		NULL,		/* . */
	lookup:		NULL,		/* . */
	link:		NULL,		/* . */
	unlink:		NULL,		/* . */
	symlink:	NULL,		/* . */
	mkdir:		NULL,		/* . */
	rmdir:		NULL,		/* . */
	mknod:		NULL,		/* . */
	rename:		NULL,		/* . */
	readlink:	NULL,		/* . */
	follow_link:	NULL,		/* . */
	truncate:	NULL,		/* . */
	permission:	NULL,		/* . */
	revalidate:	NULL,		/* . */
	setattr:	NULL,		/* . */
	getattr:	NULL,		/* . */
};

