/*
 *  linux/fs/ext2/fsync.c
 *
 *  Copyright (C) 1993  Stephen Tweedie (sct@dcs.ed.ac.uk)
 *  from
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *                      Laboratoire MASI - Institut Blaise Pascal
 *                      Universite Pierre et Marie Curie (Paris VI)
 *  from
 *  linux/fs/minix/truncate.c   Copyright (C) 1991, 1992  Linus Torvalds
 * 
 *  ext2fs fsync primitive
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 * 
 *  Removed unnecessary code duplication for little endian machines
 *  and excessive __inline__s. 
 *        Andi Kleen, 1997
 *
 * Major simplications and cleanup - we only need to do the metadata, because
 * we can depend on generic_block_fdatasync() to sync the data blocks.
 */

#include "ext2.h"
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>		/* for fsync_inode_buffers() */
#include <linux/pagemap.h>


/*
 *	File may be NULL when we are called. Perhaps we shouldn't
 *	even pass file to fsync ?
 */

int ext2_sync_file(struct file *file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	int err;
	int ret;

	ret = sync_mapping_buffers(inode->i_mapping);

	/* it might make more sense to ext2_error on -EIO from
	 * sync_mapping_buffers as well, but those errors are isolated to just
	 * this file. We can safely return -EIO to fsync and let the app know
	 * they have a problem.  
	 *
	 * AS_EIO indicates a failure to write a metadata page, but we have no 
	 * way of knowing which one.  It's best to force readonly and let fsck 
	 * figure it all out.
	 */
	if (test_and_clear_bit(AS_EIO, &sb->s_bdev->bd_inode->i_mapping->flags)) {
		ext2_error(sb, "ext2_sync_file", "metadata io error");
		if (!ret)
			ret = -EIO;
	}
	if (!(inode->i_state & I_DIRTY))
		return ret;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return ret;

	err = ext2_sync_inode(inode);
	if (ret == 0)
		ret = err;
	return ret;
}
