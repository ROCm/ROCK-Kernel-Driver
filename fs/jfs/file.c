/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include "jfs_incore.h"
#include "jfs_txnmgr.h"
#include "jfs_debug.h"


extern int generic_file_open(struct inode *, struct file *);
extern loff_t generic_file_llseek(struct file *, loff_t, int origin);

extern int jfs_commit_inode(struct inode *, int);

int jfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	int rc = 0;

	if (!(inode->i_state & I_DIRTY))
		return rc;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return rc;

	rc |= jfs_commit_inode(inode, 1);

	return rc ? -EIO : 0;
}

/*
 * Guts of jfs_truncate.  Called with locks already held.  Can be called
 * with directory for truncating directory index table.
 */
void jfs_truncate_nolock(struct inode *ip, loff_t length)
{
	loff_t newsize;
	tid_t tid;

	ASSERT(length >= 0);

	if (test_cflag(COMMIT_Nolink, ip)) {
		xtTruncate(0, ip, length, COMMIT_WMAP);
		return;
	}

	do {
		tid = txBegin(ip->i_sb, 0);

		/*
		 * The commit_sem cannot be taken before txBegin.
		 * txBegin may block and there is a chance the inode
		 * could be marked dirty and need to be committed
		 * before txBegin unblocks
		 */
		down(&JFS_IP(ip)->commit_sem);

		newsize = xtTruncate(tid, ip, length,
				     COMMIT_TRUNCATE | COMMIT_PWMAP);
		if (newsize < 0) {
			txEnd(tid);
			up(&JFS_IP(ip)->commit_sem);
			break;
		}

		ip->i_mtime = ip->i_ctime = CURRENT_TIME;
		mark_inode_dirty(ip);

		txCommit(tid, 1, &ip, 0);
		txEnd(tid);
		up(&JFS_IP(ip)->commit_sem);
	} while (newsize > length);	/* Truncate isn't always atomic */
}

static void jfs_truncate(struct inode *ip)
{
	jFYI(1, ("jfs_truncate: size = 0x%lx\n", (ulong) ip->i_size));

	IWRITE_LOCK(ip);
	jfs_truncate_nolock(ip, ip->i_size);
	IWRITE_UNLOCK(ip);
}

struct inode_operations jfs_file_inode_operations = {
	.truncate	= jfs_truncate,
};

struct file_operations jfs_file_operations = {
	.open		= generic_file_open,
	.llseek		= generic_file_llseek,
	.write		= generic_file_write,
	.read		= generic_file_read,
	.mmap		= generic_file_mmap,
 	.sendfile	= generic_file_sendfile,
	.fsync		= jfs_fsync,
};
