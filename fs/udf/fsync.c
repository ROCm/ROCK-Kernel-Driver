/*
 * fsync.c
 *
 * PURPOSE
 *  Fsync handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *  E-mail regarding any portion of the Linux UDF file system should be
 *  directed to the development team mailing list (run by majordomo):
 *      linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *  This file is distributed under the terms of the GNU General Public
 *  License (GPL). Copies of the GPL can be obtained from:
 *      ftp://prep.ai.mit.edu/pub/gnu/GPL
 *  Each contributing author retains all rights to their own work.
 *
 *  (C) 1999-2000 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  05/22/99 blf  Created.
 */

#include "udfdecl.h"

#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/udf_fs.h>
#include "udf_i.h"

static int sync_extent_block (struct inode * inode, Uint32 block, int wait)
{
	struct buffer_head * bh;
	
	if (!block)
		return 0;
	bh = get_hash_table (inode->i_dev, block, inode->i_sb->s_blocksize);
	if (!bh)
		return 0;
	if (wait && buffer_req(bh) && !buffer_uptodate(bh)) {
		/* There can be a parallell read(2) that started read-I/O
		   on the buffer so we can't assume that there's been
		   an I/O error without first waiting I/O completation. */
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
		{
			brelse (bh);
			return -1;
		}
	}
	if (wait || !buffer_uptodate(bh) || !buffer_dirty(bh)) {
		if (wait)
			/* when we return from fsync all the blocks
			   must be _just_ stored on disk */
			wait_on_buffer(bh);
		brelse (bh);
		return 0;
	}
	ll_rw_block (WRITE, 1, &bh);
	atomic_dec(&bh->b_count);
	return 0;
}

static int sync_all_extents(struct inode * inode, int wait)
{
	lb_addr bloc, eloc;
	Uint32 extoffset, lextoffset, elen, offset, block;
	int err = 0, etype;
	struct buffer_head *bh = NULL;
	
	if ((etype = inode_bmap(inode, 0, &bloc, &extoffset, &eloc, &elen, &offset, &bh)) != -1)
	{
		block = udf_get_lb_pblock(inode->i_sb, bloc, 0);
		err |= sync_extent_block(inode, block, wait);
		lextoffset = extoffset;

		while ((etype = udf_next_aext(inode, &bloc, &extoffset, &eloc, &elen, &bh, 1)) != -1)
		{
			if (lextoffset > extoffset)
			{
				block = udf_get_lb_pblock(inode->i_sb, bloc, 0);
				err |= sync_extent_block(inode, block, wait);
			}
			lextoffset = extoffset;
		}
	}
	udf_release_data(bh);
	return err;
}

/*
 *	File may be NULL when we are called. Perhaps we shouldn't
 *	even pass file to fsync ?
 */

int udf_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	int wait, err = 0;
	struct inode *inode = dentry->d_inode;

	lock_kernel();
	if (S_ISLNK(inode->i_mode) && !(inode->i_blocks)) 
	{
		/*
		 * Don't sync fast links! or ICB_FLAG_AD_IN_ICB
		 */
		goto skip;
	}

	err = generic_buffer_fdatasync(inode, 0, ~0UL);

	for (wait=0; wait<=1; wait++)
	{
		err |= sync_all_extents (inode, wait);
	}
skip:
	err |= udf_sync_inode (inode);
	unlock_kernel();
	return err ? -EIO : 0;
}
