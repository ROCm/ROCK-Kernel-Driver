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
#include "jfs_dmap.h"
#include "jfs_txnmgr.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"
#include "jfs_debug.h"
#ifdef CONFIG_JFS_DMAPI
#include <linux/uio.h>
#include "jfs_dmapi.h"
#endif


extern int jfs_commit_inode(struct inode *, int);
extern void jfs_truncate(struct inode *);
extern int jfs_acl_chmod(struct inode *);

int jfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	int rc = 0;

	if (!(inode->i_state & I_DIRTY) ||
	    (datasync && !(inode->i_state & I_DIRTY_DATASYNC))) {
		/* Make sure committed changes hit the disk */
		jfs_flush_journal(JFS_SBI(inode->i_sb)->log, 1);
		return rc;
	}

	rc |= jfs_commit_inode(inode, 1);

	return rc ? -EIO : 0;
}

static int jfs_open(struct inode *inode, struct file *file)
{
	int rc;

	if ((rc = generic_file_open(inode, file)))
		return rc;

	/*
	 * We attempt to allow only one "active" file open per aggregate
	 * group.  Otherwise, appending to files in parallel can cause
	 * fragmentation within the files.
	 *
	 * If the file is empty, it was probably just created and going
	 * to be written to.  If it has a size, we'll hold off until the
	 * file is actually grown.
	 */
	if (S_ISREG(inode->i_mode) && file->f_mode & FMODE_WRITE &&
	    (inode->i_size == 0)) {
		struct jfs_inode_info *ji = JFS_IP(inode);
		if (ji->active_ag == -1) {
			ji->active_ag = ji->agno;
			atomic_inc(
			    &JFS_SBI(inode->i_sb)->bmap->db_active[ji->agno]);
		}
	}

	return 0;
}

int jfs_release(struct inode *inode, struct file *file)
{
	struct jfs_inode_info *ji = JFS_IP(inode);

	if (ji->active_ag != -1) {
		struct bmap *bmap = JFS_SBI(inode->i_sb)->bmap;
		atomic_dec(&bmap->db_active[ji->active_ag]);
		ji->active_ag = -1;
	}

#ifdef CONFIG_JFS_DMAPI
	if ((atomic_read(&file->f_dentry->d_count) == 1) && 
	    (DM_EVENT_ENABLED(inode, DM_EVENT_CLOSE)))
		JFS_SEND_NAMESP(DM_EVENT_CLOSE, inode, DM_RIGHT_NULL, NULL,
				DM_RIGHT_NULL, NULL, NULL, 0, 0, 0);
#endif	

	return 0;
}

#if defined(CONFIG_JFS_DMAPI) || defined(CONFIG_JFS_POSIX_ACL)
int jfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int rc;

	rc = inode_change_ok(inode, iattr);
	if (rc)
		return rc;

#ifdef CONFIG_JFS_DMAPI
	if ((iattr->ia_valid & ATTR_SIZE) &&
	    DM_EVENT_ENABLED(inode, DM_EVENT_TRUNCATE)) {
		rc = JFS_SEND_DATA(DM_EVENT_TRUNCATE, inode, iattr->ia_size, 0,
				   0, NULL);
		
		if (rc)
			return rc;
	}
#endif
	
	inode_setattr(inode, iattr);

#ifdef CONFIG_JFS_POSIX_ACL
	if (iattr->ia_valid & ATTR_MODE)
		rc = jfs_acl_chmod(inode);
#endif

#ifdef CONFIG_JFS_DMAPI
	if (DM_EVENT_ENABLED(inode, DM_EVENT_ATTRIBUTE))
		JFS_SEND_NAMESP(DM_EVENT_ATTRIBUTE, inode, DM_RIGHT_NULL, NULL,
				DM_RIGHT_NULL, NULL, NULL, 0, 0, 0);
	
	if (JFS_SBI(inode->i_sb)->flag & JFS_DMI) {
		/* Metadata change */
		if (rc >= 0) {
			inode->i_version++;
			mark_inode_dirty(inode);
		}
	}
#endif	

	return rc;
}
#endif

#ifdef CONFIG_JFS_DMAPI
static ssize_t jfs_read(struct file *file, char __user *bufp, 
	         	size_t count, loff_t *ppos)
{
	struct inode *ip = file->f_dentry->d_inode;
	int error;

	if (DM_EVENT_ENABLED(ip, DM_EVENT_READ)) {
		error = JFS_SEND_DATA(DM_EVENT_READ, ip, *ppos, count, 
				FILP_DELAY_FLAG(file), NULL /*locktype*/);
		if (error)
			return error;
	}

	return generic_file_read(file, bufp, count, ppos);
}

static ssize_t jfs_write(struct file *file, const char __user *bufp,
			 size_t count, loff_t *ppos)
{
	struct inode *ip = file->f_dentry->d_inode;
	int error;

	if (DM_EVENT_ENABLED(ip, DM_EVENT_WRITE)) {
		error = JFS_SEND_DATA(DM_EVENT_WRITE, ip, *ppos, count, 
				FILP_DELAY_FLAG(file), NULL /*locktype*/);
		if (error)
			return error;
	}

	error = generic_file_write(file, bufp, count, ppos);

	if (JFS_SBI(ip->i_sb)->flag & JFS_DMI) {
		/* Data change */
		if (error > 0) {
			ip->i_version++;
			mark_inode_dirty(ip);
		}
	}

	return error;
}

static ssize_t jfs_aio_read(struct kiocb *iocb, char __user *bufp,
			    size_t count, loff_t pos)
{
	struct inode *ip = iocb->ki_filp->f_dentry->d_inode;
	int error;

	if (DM_EVENT_ENABLED(ip, DM_EVENT_READ)) {
		error = JFS_SEND_DATA(DM_EVENT_READ, ip, pos, count,
				FILP_DELAY_FLAG(iocb->ki_filp), 
				NULL /*locktype*/);
		if (error)
			return error;
	}

	return generic_file_aio_read(iocb, bufp, count, pos);
}

static ssize_t jfs_aio_write(struct kiocb *iocb, const char __user *bufp,
			     size_t count, loff_t pos)
{
	struct inode *ip = iocb->ki_filp->f_dentry->d_inode;
	int error;

	if (DM_EVENT_ENABLED(ip, DM_EVENT_WRITE)) {
		error = JFS_SEND_DATA(DM_EVENT_WRITE, ip, pos, count,
				FILP_DELAY_FLAG(iocb->ki_filp), 
				NULL /*locktype*/);
		if (error)
			return error;
	}

	error = generic_file_aio_write(iocb, bufp, count, pos);
	
	if (JFS_SBI(ip->i_sb)->flag & JFS_DMI) {
		/* Data change */
		if (error > 0) {
			ip->i_version++;
			mark_inode_dirty(ip);
		}
	}

	return error;
}

static ssize_t jfs_readv(struct file *file, const struct iovec *invecs,
			 unsigned long count, loff_t *ppos)
{
	struct inode *ip = file->f_dentry->d_inode;
	int error;

	if (DM_EVENT_ENABLED(ip, DM_EVENT_READ)) {
		error = JFS_SEND_DATA(DM_EVENT_READ, ip, *ppos,
				iov_length(invecs, count), 
				FILP_DELAY_FLAG(file), NULL /*locktype*/);
		if (error)
			return error;
	}

	return generic_file_readv(file, invecs, count, ppos);
}

static ssize_t jfs_writev(struct file *file, const struct iovec *outvecs,
			  unsigned long count, loff_t *ppos)
{
	struct inode *ip = file->f_dentry->d_inode;
	int error;

	if (DM_EVENT_ENABLED(ip, DM_EVENT_WRITE)) {
		error = JFS_SEND_DATA(DM_EVENT_WRITE, ip, *ppos,
				iov_length(outvecs, count),
				FILP_DELAY_FLAG(file), NULL /*locktype*/);
		if (error)
			return error;
	}

	error = generic_file_writev(file, outvecs, count, ppos);
	
	if (JFS_SBI(ip->i_sb)->flag & JFS_DMI) {
		/* Data change */
		if (error > 0) {
			ip->i_version++;
			mark_inode_dirty(ip);
		}
	}

	return error;
}

static int jfs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct inode *ip = filp->f_dentry->d_inode;
	int error;
	
	if (S_ISREG(ip->i_mode) && (JFS_SBI(ip->i_sb)->flag & JFS_DMI)) {
		error = JFS_SEND_MMAP(vma, 0);
		if (error) {
			return error;
		}
	}

	return generic_file_mmap(filp, vma);
}	
#endif

struct inode_operations jfs_file_inode_operations = {
	.truncate	= jfs_truncate,
	.setxattr	= jfs_setxattr,
	.getxattr	= jfs_getxattr,
	.listxattr	= jfs_listxattr,
	.removexattr	= jfs_removexattr,
#ifdef CONFIG_JFS_POSIX_ACL
	.setattr	= jfs_setattr,
	.permission	= jfs_permission,
#elif defined(CONFIG_JFS_DMAPI)
	.setattr	= jfs_setattr,
#endif	
};

struct file_operations jfs_file_operations = {
	.open		= jfs_open,
	.llseek		= generic_file_llseek,
	.write		= generic_file_write,
	.read		= generic_file_read,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.readv		= generic_file_readv,
	.writev		= generic_file_writev,
 	.sendfile	= generic_file_sendfile,
	.fsync		= jfs_fsync,
	.release	= jfs_release,
#ifdef CONFIG_JFS_DMAPI
	.write		= jfs_write,
	.read		= jfs_read,
	.aio_read	= jfs_aio_read,
	.aio_write	= jfs_aio_write,
	.mmap		= jfs_mmap,
	.readv		= jfs_readv,
	.writev		= jfs_writev,
#endif	
};
