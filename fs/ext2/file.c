/*
 *  linux/fs/ext2/file.c
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
 *  ext2 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/sched.h>

static loff_t ext2_file_lseek(struct file *, loff_t, int);
static int ext2_open_file (struct inode *, struct file *);

#define EXT2_MAX_SIZE(bits)							\
	(((EXT2_NDIR_BLOCKS + (1LL << (bits - 2)) + 				\
	   (1LL << (bits - 2)) * (1LL << (bits - 2)) + 				\
	   (1LL << (bits - 2)) * (1LL << (bits - 2)) * (1LL << (bits - 2))) * 	\
	  (1LL << bits)) - 1)

static long long ext2_max_sizes[] = {
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
EXT2_MAX_SIZE(10), EXT2_MAX_SIZE(11), EXT2_MAX_SIZE(12), EXT2_MAX_SIZE(13)
};

/*
 * Make sure the offset never goes beyond the 32-bit mark..
 */
static loff_t ext2_file_lseek(
	struct file *file,
	loff_t offset,
	int origin)
{
	struct inode *inode = file->f_dentry->d_inode;

	switch (origin) {
		case 2:
			offset += inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	if (offset<0)
		return -EINVAL;
	if (((unsigned long long) offset >> 32) != 0) {
		if (offset > ext2_max_sizes[EXT2_BLOCK_SIZE_BITS(inode->i_sb)])
			return -EINVAL;
	} 
	if (offset != file->f_pos) {
		file->f_pos = offset;
		file->f_reada = 0;
		file->f_version = ++event;
	}
	return offset;
}

/*
 * Called when an inode is released. Note that this is different
 * from ext2_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ext2_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE)
		ext2_discard_prealloc (inode);
	return 0;
}

/*
 * Called when an inode is about to be open.
 * We use this to disallow opening RW large files on 32bit systems if
 * the caller didn't specify O_LARGEFILE.  On 64bit systems we force
 * on this flag in sys_open.
 */
static int ext2_open_file (struct inode * inode, struct file * filp)
{
	if (!(filp->f_flags & O_LARGEFILE) &&
	    inode->i_size > 0x7FFFFFFFLL)
		return -EFBIG;
	return 0;
}

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ext2 filesystem.
 */
struct file_operations ext2_file_operations = {
	llseek:		ext2_file_lseek,
	read:		generic_file_read,
	write:		generic_file_write,
	ioctl:		ext2_ioctl,
	mmap:		generic_file_mmap,
	open:		ext2_open_file,
	release:	ext2_release_file,
	fsync:		ext2_sync_file,
};

struct inode_operations ext2_file_inode_operations = {
	truncate:	ext2_truncate,
};
