/*
 *  linux/fs/ext3/fsync.c
 *
 *  Copyright (C) 1993  Stephen Tweedie (sct@redhat.com)
 *  from
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *                      Laboratoire MASI - Institut Blaise Pascal
 *                      Universite Pierre et Marie Curie (Paris VI)
 *  from
 *  linux/fs/minix/truncate.c   Copyright (C) 1991, 1992  Linus Torvalds
 * 
 *  ext3fs fsync primitive
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

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>

/*
 * akpm: A new design for ext3_sync_file().
 *
 * This is only called from sys_fsync(), sys_fdatasync() and sys_msync().
 * There cannot be a transaction open by this task.
 * Another task could have dirtied this inode.  Its data can be in any
 * state in the journalling system.
 *
 * What we do is just kick off a commit and wait on it.  This will snapshot the
 * inode to disk.
 *
 * Note that there is a serious optimisation we can make here: if the current
 * inode is not part of j_running_transaction or j_committing_transaction
 * then we have nothing to do.  That would require implementation of t_ilist,
 * which isn't too hard.
 */

int ext3_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;

	J_ASSERT(ext3_journal_current_handle() == 0);

	/*
	 * data=writeback:
	 *  The caller's filemap_fdatawrite()/wait will sync the data.
	 *  ext3_force_commit() will sync the metadata
	 *
	 * data=ordered:
	 *  The caller's filemap_fdatawrite() will write the data and
	 *  ext3_force_commit() will wait on the buffers.  Then the caller's
	 *  filemap_fdatawait() will wait on the pages (but all IO is complete)
	 *  Not pretty, but it works.
	 *
	 * data=journal:
	 *  filemap_fdatawrite won't do anything (the buffers are clean).
	 *  ext3_force_commit will write the file data into the journal and
	 *  will wait on that.
	 *  filemap_fdatawait() will encounter a ton of newly-dirtied pages
	 *  (they were dirtied by commit).  But that's OK - the blocks are
	 *  safe in-journal, which is all fsync() needs to ensure.
	 */
	return ext3_force_commit(inode->i_sb);
}
