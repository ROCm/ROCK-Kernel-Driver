/*
 *   fs/cifs/file.c
 *
 *   vfs operations that deal with files
 * 
 *   Copyright (c) International Business Machines  Corp., 2002
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/version.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

int
cifs_open(struct inode *inode, struct file *file)
{
	int rc = -EACCES;
	int xid, oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
    struct cifsFileInfo *pCifsFile;
    struct cifsInodeInfo *pCifsInode;
	char *full_path = NULL;
	int desiredAccess = 0x20197;
	int disposition = FILE_OPEN;
	__u16 netfid;

	xid = GetXid();

	cFYI(1, (" inode = 0x%p file flags are %x", inode, file->f_flags));
	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(file->f_dentry);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		desiredAccess = GENERIC_READ;
	else if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		desiredAccess = GENERIC_WRITE;
	else if ((file->f_flags & O_ACCMODE) == O_RDWR)
		desiredAccess = GENERIC_ALL;

/* BB check other flags carefully to find equivalent NTCreateX flags */

/*
#define O_CREAT		   0100	
#define O_EXCL		   0200	
#define O_NOCTTY	   0400	
#define O_TRUNC		  01000	
#define O_APPEND	  02000
#define O_NONBLOCK	  04000
#define O_NDELAY	O_NONBLOCK
#define O_SYNC		 010000
#define FASYNC		 020000	
#define O_DIRECT	 040000	
#define O_LARGEFILE	0100000
#define O_DIRECTORY	0200000	
#define O_NOFOLLOW	0400000
#define O_ATOMICLOOKUP	01000000 */

	if (file->f_flags & O_CREAT)
		disposition = FILE_OVERWRITE;

	/* BB finish adding in oplock support BB */
	if (oplockEnabled)
		oplock = TRUE;
	else
		oplock = FALSE;

	/* BB pass O_SYNC flag through on file attributes .. BB */
	rc = CIFSSMBOpen(xid, pTcon, full_path, disposition, desiredAccess,			 
			 -1 /* i.e. dummy value, ignored for time being */,
			 &netfid, &oplock, cifs_sb->local_nls);
	if (rc) {
		cFYI(1, ("\ncifs_open returned 0x%x ", rc));
		cFYI(1, (" oplock: %d ", oplock));
	} else {
		file->private_data =
		    kmalloc(sizeof (struct cifsFileInfo), GFP_KERNEL);
		if (file->private_data) {
			memset(file->private_data, 0,
			       sizeof (struct cifsFileInfo));
            pCifsFile = (struct cifsFileInfo *) file->private_data;
			pCifsFile->netfid = netfid;
			pCifsFile->pid = current->pid;
            list_add(&pCifsFile->tlist,&pTcon->openFileList);
            pCifsInode = CIFS_I(file->f_dentry->d_inode);
            list_add(&pCifsFile->flist,&pCifsInode->openFileList);
            if(file->f_flags & O_CREAT) {           
                 /* time to set mode which we can not
                 set earlier due to problems creating new read-only files */
                if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)                
                    CIFSSMBUnixSetPerms(xid, pTcon, full_path, inode->i_mode,
                                        0xFFFFFFFFFFFFFFFF,  
                                        0xFFFFFFFFFFFFFFFF,
                                        cifs_sb->local_nls);
                else {/* BB to be implemented via Windows secrty descriptors*/
         /* eg CIFSSMBWinSetPerms(xid,pTcon,full_path,mode,-1,-1,local_nls);*/
                    /* in the meantime we could set the r/o dos attribute 
                       when perms are e.g.:   
                            mode & 0222 == 0 */
                }
            }

		}
	}

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_close(struct inode *inode, struct file *file)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *pSMBFile =
	    (struct cifsFileInfo *) file->private_data;

	cFYI(1, ("\n  inode = 0x%p with ", inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;
 	if (pSMBFile) {
        list_del(&pSMBFile->flist);
        list_del(&pSMBFile->tlist);
        rc = CIFSSMBClose(xid, pTcon, pSMBFile->netfid);
		kfree(file->private_data);
		file->private_data = NULL;
	} else
		rc = -EBADF;

	FreeXid(xid);
	return rc;

}

int
cifs_closedir(struct inode *inode, struct file *file)
{
	int rc = 0;
	int xid;
	struct cifsFileInfo *pSMBFileStruct =
	    (struct cifsFileInfo *) file->private_data;

	cFYI(1, ("\nClosedir inode = 0x%p with ", inode));

	xid = GetXid();

	if (pSMBFileStruct) {
		cFYI(1, ("\nFreeing private data in close dir"));
		kfree(file->private_data);
		file->private_data = NULL;
	}
	FreeXid(xid);
	return rc;
}

int
cifs_lock(struct file *file, int cmd, struct file_lock *pfLock)
{
	int rc, xid;
	__u32 lockType = LOCKING_ANDX_LARGE_FILES;
	__u32 numLock = 0;
	__u32 numUnlock = 0;
	__u64 length;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	length = 1 + pfLock->fl_end - pfLock->fl_start;

	rc = -EACCES;

	xid = GetXid();

	cFYI(1,
	     ("\nLock parm: 0x%x flockflags: 0x%x flocktype: 0x%x start: %lld end: %lld",
	      cmd, pfLock->fl_flags, pfLock->fl_type, pfLock->fl_start,
	      pfLock->fl_end));

	if (pfLock->fl_flags & FL_POSIX)
		cFYI(1, ("\nPosix "));
	if (pfLock->fl_flags & FL_FLOCK)
		cFYI(1, ("\nFlock "));
	if (pfLock->fl_flags & FL_SLEEP)
		cFYI(1, ("\nBlocking lock "));
	if (pfLock->fl_flags & FL_ACCESS)
		cFYI(1, ("\nProcess suspended by mandatory locking "));
	if (pfLock->fl_flags & FL_LEASE)
		cFYI(1, ("\nLease on file "));
	if (pfLock->fl_flags & 0xFFD0)
		cFYI(1, ("\n Unknown lock flags "));

	if (pfLock->fl_type == F_WRLCK) {
		cFYI(1, ("\nF_WRLCK "));
		numLock = 1;
	} else if (pfLock->fl_type == F_UNLCK) {
		cFYI(1, ("\nF_UNLCK "));
		numUnlock = 1;
	} else if (pfLock->fl_type == F_RDLCK) {
		cFYI(1, ("\nF_RDLCK "));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else if (pfLock->fl_type == F_EXLCK) {
		cFYI(1, ("\nF_EXLCK "));
		numLock = 1;
	} else if (pfLock->fl_type == F_SHLCK) {
		cFYI(1, ("\nF_SHLCK "));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else
		cFYI(1, ("\nUnknown type of lock "));

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	if (IS_GETLK(cmd)) {
		rc = CIFSSMBLock(xid, pTcon,
				 ((struct cifsFileInfo *) file->
				  private_data)->netfid,
				 length,
				 pfLock->fl_start, 0, 1, lockType,
				 0 /* wait flag */ );
		if (rc == 0) {
			rc = CIFSSMBLock(xid, pTcon,
					 ((struct cifsFileInfo *) file->
					  private_data)->netfid,
					 length,
					 pfLock->fl_start, 1 /* numUnlock */ ,
					 0 /* numLock */ , lockType,
					 0 /* wait flag */ );
			pfLock->fl_type = F_UNLCK;
			if (rc != 0)
				cERROR(1,
				       ("\nError unlocking previously locked range %d during test of lock ",
					rc));
			rc = 0;

		} else {
			/* if rc == ERR_SHARING_VIOLATION ? */
			rc = 0;	/* do not change lock type to unlock since range in use */
		}

		FreeXid(xid);
		return rc;
	}

	rc = CIFSSMBLock(xid, pTcon,
			 ((struct cifsFileInfo *) file->private_data)->
			 netfid, length,
			 pfLock->fl_start, numUnlock, numLock, lockType,
			 0 /* wait flag */ );
	FreeXid(xid);
	return rc;
}

ssize_t
cifs_write(struct file * file, const char *write_data,
	   size_t write_size, loff_t * poffset)
{
	int rc = 0;
	int bytes_written = 0;
	int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid, long_op;

	xid = GetXid();

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	/*cFYI(1,
	   (" write %d bytes to offset %lld of %s \n", write_size,
	   *poffset, file->f_dentry->d_name.name)); */

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	if (*poffset > file->f_dentry->d_inode->i_size)
		long_op = 2;	/* writes past end of file can take a long time */
	else
		long_op = 1;

	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = CIFSSMBWrite(xid, pTcon,
				  ((struct cifsFileInfo *) file->
				   private_data)->netfid,
				  write_size - total_written, *poffset,
				  &bytes_written,
				  write_data + total_written, long_op);
		if (rc || (bytes_written == 0)) {
			FreeXid(xid);
			if (total_written)
				break;
			else {
				FreeXid(xid);
				return rc;
			}
		} else
			*poffset += bytes_written;
		long_op = FALSE; /* subsequent writes fast - 15 seconds is plenty */
	}
	file->f_dentry->d_inode->i_ctime = file->f_dentry->d_inode->i_mtime = CURRENT_TIME;
	if (bytes_written > 0) {
		if (*poffset > file->f_dentry->d_inode->i_size)
			file->f_dentry->d_inode->i_size = *poffset;
	}
	mark_inode_dirty_sync(file->f_dentry->d_inode);
	return total_written;
}

static int
cifs_writepage(struct page *page)
{
	struct address_space *mapping = page->mapping;
	int rc = -EFAULT;
	int bytes_written = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;
	struct inode *inode = page->mapping->host;
	loff_t *poffset = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	/* figure out which file struct to use 
	   if (file->private_data == NULL) {
	   FreeXid(xid);
	   return -EBADF;
	   }     
	 */
	if (!mapping) {
		FreeXid(xid);
		return -EFAULT;
	}

	/* BB fix and add missing call to cifs_writepage_sync here */

	inode->i_ctime = inode->i_mtime = CURRENT_TIME;	/* BB is this right? */
	if ((bytes_written > 0) && (poffset)) {
		if (*poffset > inode->i_size)
			inode->i_size = *poffset;
	}
	mark_inode_dirty_sync(inode);

	FreeXid(xid);
	return rc;
}

static int
cifs_prepare_write(struct file *file, struct page *page, unsigned offset,
		   unsigned to)
{
	return 0;	/* eventually add code to flush any incompatible requests */
}

static int
cifs_commit_write(struct file *file, struct page *page, unsigned offset,
		  unsigned to)
{
	long rc = -EFAULT;

	lock_kernel();
/*	status = cifs_updatepage(file, page, offset, to-offset); */

/* BB add - do we really need to lock the kernel here for so long ? */

	unlock_kernel();
	return rc;
}

int
cifs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int xid;
	int rc = 0;
	xid = GetXid();
	/* BB fill in code to flush write behind buffers - when we start supporting oplock */
	cFYI(1, (" name: %s datasync: 0x%x ", dentry->d_name.name, datasync));
	FreeXid(xid);
	return rc;
}

static int
cifs_sync_page(struct page *page)
{
	struct address_space *mapping;
	struct inode *inode;
	unsigned long index = page->index;
	unsigned int rpages = 0;
	int rc = 0;

	mapping = page->mapping;
	if (!mapping)
		return 0;
	inode = mapping->host;
	if (!inode)
		return 0;

/*	fill in rpages then 
    result = cifs_pagein_inode(inode, index, rpages); *//* BB finish */

	cFYI(1, ("\nrpages is %d for sync page of Index %ld ", rpages, index));

	if (rc < 0)
		return rc;
	return 0;
}

ssize_t
cifs_read(struct file * file, char *read_data, size_t read_size,
	  loff_t * poffset)
{
	int rc = -EACCES;
	int bytes_read = 0;
	int total_read;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	for (total_read = 0; read_size > total_read; total_read += bytes_read) {
		rc = CIFSSMBRead(xid, pTcon,
				 ((struct cifsFileInfo *) file->
				  private_data)->netfid,
				 read_size - total_read, *poffset,
				 &bytes_read, read_data + total_read);
		if (rc || (bytes_read == 0)) {
			if (total_read) {
				break;
			} else {
				FreeXid(xid);
				return rc;
			}
		} else
			*poffset += bytes_read;
	}

	FreeXid(xid);
	return total_read;
}

static int
cifs_readpage_sync(struct file *file, struct page *page)
{
	unsigned int count = PAGE_CACHE_SIZE;
	int rc = -EACCES;
	int bytes_read = 0;
	int total_read = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	/* BB finish adding missing here */

	cFYI(1,
	     ("\nCount is %d total read %d bytes read %d ", count, total_read,
	      bytes_read));

	FreeXid(xid);
	return rc;
}

static int
cifs_readpage(struct file *file, struct page *page)
{
	int rc;

	page_cache_get(page);
	rc = cifs_readpage_sync(file, page);
	/* for reads over a certain size we could initiate async read ahead */
	page_cache_release(page);
	return rc;
}

void
fill_in_inode(struct inode *tmp_inode,
	      FILE_DIRECTORY_INFO * pfindData, int *pobject_type)
{
	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	pfindData->ExtFileAttributes =
	    le32_to_cpu(pfindData->ExtFileAttributes);
	pfindData->AllocationSize = le64_to_cpu(pfindData->AllocationSize);
	pfindData->EndOfFile = le64_to_cpu(pfindData->EndOfFile);
	cifsInfo->cifsAttrs = pfindData->ExtFileAttributes;
	cifsInfo->time = jiffies;
	atomic_inc(&cifsInfo->inUse);	/* inc on every refresh of inode info */

	/* Linux can not store file creation time unfortunately so we ignore it */
	tmp_inode->i_atime =
	    le64_to_cpu(cifs_NTtimeToUnix(pfindData->LastAccessTime));
	tmp_inode->i_mtime =
	    le64_to_cpu(cifs_NTtimeToUnix(pfindData->LastWriteTime));
	tmp_inode->i_ctime =
	    le64_to_cpu(cifs_NTtimeToUnix(pfindData->ChangeTime));
	/* should we treat the dos attribute of read-only as read-only mode bit e.g. 555 */
	tmp_inode->i_mode = S_IALLUGO & ~(S_ISUID | S_IXGRP);	/* 2767 perms - indicate mandatory locking */
	cFYI(0,
	     ("\nCIFS FFIRST: Attributes came in as 0x%x",
	      pfindData->ExtFileAttributes));
	if (pfindData->ExtFileAttributes & ATTR_REPARSE) {
		*pobject_type = DT_LNK;
		tmp_inode->i_mode |= S_IFLNK;	/* BB can this and S_IFREG or S_IFDIR be set at same time as in Windows? */
	} else if (pfindData->ExtFileAttributes & ATTR_DIRECTORY) {
		*pobject_type = DT_DIR;
		tmp_inode->i_mode = S_IRWXUGO;	/* override default perms since we do not lock dirs */
		tmp_inode->i_mode |= S_IFDIR;
	} else {
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
	}/* could add code here - to validate if device or weird share type? */

	/* can not fill in nlink here as in qpathinfo version and in Unx search */
	tmp_inode->i_size = pfindData->EndOfFile;
	tmp_inode->i_blocks =
	    do_div(pfindData->AllocationSize, tmp_inode->i_blksize);
	cFYI(1,
	     ("\nFinddata alloc size (from smb) %lld",
	      pfindData->AllocationSize));
	if (pfindData->AllocationSize < pfindData->EndOfFile)
		cFYI(1, ("\nServer inconsistency Error: it says allocation size less than end of file "));
	cFYI(1,
	     ("\nCIFS FFIRST: Size %ld and blocks %ld and blocksize %ld",
	      (unsigned long) tmp_inode->i_size, tmp_inode->i_blocks,
	      tmp_inode->i_blksize));
	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, (" File inode "));
		tmp_inode->i_op = &cifs_file_inode_ops;
		tmp_inode->i_fop = &cifs_file_ops;
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, (" Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, (" Symbolic Link inode "));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
	} else {
		cFYI(1, ("\nInit special inode "));
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   kdev_t_to_nr(tmp_inode->i_rdev));
	}
}

void
unix_fill_in_inode(struct inode *tmp_inode,
		   FILE_UNIX_INFO * pfindData, int *pobject_type)
{
	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	cifsInfo->time = jiffies;
	atomic_inc(&cifsInfo->inUse);

	tmp_inode->i_atime =
	    le64_to_cpu(cifs_NTtimeToUnix(pfindData->LastAccessTime));
	tmp_inode->i_mtime =
	    le64_to_cpu(cifs_NTtimeToUnix(pfindData->LastModificationTime));
	tmp_inode->i_ctime =
	    le64_to_cpu(cifs_NTtimeToUnix(pfindData->LastStatusChange));

	/* tmp_inode->i_mtime =
	   cifs_NTtimeToUnix(pfindData->LastModificationTime);
	   tmp_inode->i_ctime =
	   cifs_NTtimeToUnix(pfindData->LastStatusChange); */
	tmp_inode->i_mode = le64_to_cpu(pfindData->Permissions);
	pfindData->Type = le32_to_cpu(pfindData->Type);
	if (pfindData->Type == UNIX_FILE) {
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
	} else if (pfindData->Type == UNIX_SYMLINK) {
		*pobject_type = DT_LNK;
		tmp_inode->i_mode |= S_IFLNK;
	} else if (pfindData->Type == UNIX_DIR) {
		*pobject_type = DT_DIR;
		tmp_inode->i_mode |= S_IFDIR;
	} else if (pfindData->Type == UNIX_CHARDEV) {
		*pobject_type = DT_CHR;
		tmp_inode->i_mode |= S_IFCHR;
	} else if (pfindData->Type == UNIX_BLOCKDEV) {
		*pobject_type = DT_BLK;
		tmp_inode->i_mode |= S_IFBLK;
	} else if (pfindData->Type == UNIX_FIFO) {
		*pobject_type = DT_FIFO;
		tmp_inode->i_mode |= S_IFIFO;
	} else if (pfindData->Type == UNIX_SOCKET) {
		*pobject_type = DT_SOCK;
		tmp_inode->i_mode |= S_IFSOCK;
	}

	tmp_inode->i_uid = le64_to_cpu(pfindData->Uid);
	tmp_inode->i_gid = le64_to_cpu(pfindData->Gid);
	tmp_inode->i_nlink = le64_to_cpu(pfindData->Nlinks);

	pfindData->NumOfBytes = le64_to_cpu(pfindData->NumOfBytes);
	pfindData->EndOfFile = le64_to_cpu(pfindData->EndOfFile);
	tmp_inode->i_size = pfindData->EndOfFile;
	tmp_inode->i_blocks =
	    do_div(pfindData->NumOfBytes, tmp_inode->i_blksize);
	cFYI(0, ("\nFinddata alloc size (from smb) %lld", pfindData->NumOfBytes));	/* BB remove */
	if (pfindData->NumOfBytes < pfindData->EndOfFile)
		cFYI(0, ("\nServer inconsistency Error: it says allocation size less than end of file "));	/* BB remove */
	cFYI(1, ("\nCIFS FFIRST: Size %ld and blocks %ld ", (unsigned long) tmp_inode->i_size, tmp_inode->i_blocks));	/* BB remove */
	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, (" File inode "));
		tmp_inode->i_op = &cifs_file_inode_ops;
		tmp_inode->i_fop = &cifs_file_ops;
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, (" Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, (" Symbolic Link inode "));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
/* tmp_inode->i_fop = *//* do not need to set to anything */
	} else {
		cFYI(1, ("\nInit special inode "));
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   kdev_t_to_nr(tmp_inode->i_rdev));
	}
}

void
construct_dentry(struct qstr *qstring, struct file *file,
		 struct inode **ptmp_inode, struct dentry **pnew_dentry)
{
	struct dentry *tmp_dentry;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	cFYI(1, ("\nFor %s ", qstring->name));
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	qstring->hash = full_name_hash(qstring->name, qstring->len);
	tmp_dentry = d_lookup(file->f_dentry, qstring);
	if (tmp_dentry) {
		cFYI(1, (" existing dentry with inode 0x%p ", tmp_dentry->d_inode));	/* BB remove */
		*ptmp_inode = tmp_dentry->d_inode;
		/* BB overwrite the old name? i.e. tmp_dentry->d_name and tmp_dentry->d_name.len ?? */
	} else {
		tmp_dentry = d_alloc(file->f_dentry, qstring);
		*ptmp_inode = new_inode(file->f_dentry->d_sb);
		cFYI(1, ("\nAlloc new inode %p ", *ptmp_inode));
		tmp_dentry->d_op = &cifs_dentry_ops;
		cFYI(1, (" instantiate dentry 0x%p with inode 0x%p ",
			 tmp_dentry, *ptmp_inode));
		d_instantiate(tmp_dentry, *ptmp_inode);
		d_rehash(tmp_dentry);
	}

	tmp_dentry->d_time = jiffies;
	(*ptmp_inode)->i_blksize =
	    (pTcon->ses->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFE00;
	cFYI(1, ("\ni_blksize = %ld", (*ptmp_inode)->i_blksize));
	*pnew_dentry = tmp_dentry;
}

void
cifs_filldir(struct qstr *pqstring, FILE_DIRECTORY_INFO * pfindData,
	     struct file *file, filldir_t filldir, void *direntry)
{
	struct inode *tmp_inode;
	struct dentry *tmp_dentry;
	int object_type;

	pqstring->name = pfindData->FileName;
	pqstring->len = pfindData->FileNameLength;

	construct_dentry(pqstring, file, &tmp_inode, &tmp_dentry);

	fill_in_inode(tmp_inode, pfindData, &object_type);
	filldir(direntry, pfindData->FileName, pqstring->len, file->f_pos,
		tmp_inode->i_ino, object_type);
	dput(tmp_dentry);
}

void
cifs_filldir_unix(struct qstr *pqstring,
		  FILE_UNIX_INFO * pUnixFindData, struct file *file,
		  filldir_t filldir, void *direntry)
{
	struct inode *tmp_inode;
	struct dentry *tmp_dentry;
	int object_type;

	pqstring->name = pUnixFindData->FileName;
	pqstring->len = strnlen(pUnixFindData->FileName, MAX_PATHCONF);

	construct_dentry(pqstring, file, &tmp_inode, &tmp_dentry);

	unix_fill_in_inode(tmp_inode, pUnixFindData, &object_type);
	filldir(direntry, pUnixFindData->FileName, pqstring->len,
		file->f_pos, tmp_inode->i_ino, object_type);
	dput(tmp_dentry);
}

int
cifs_readdir(struct file *file, void *direntry, filldir_t filldir)
{
	int rc = 0;
	int xid, i;
	int Unicode = FALSE;
	int UnixSearch = FALSE;
	__u16 searchHandle;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *cifsFile = NULL;
	char *full_path = NULL;
	char *data;
	struct qstr qstring;
	T2_FFIRST_RSP_PARMS findParms;
	T2_FNEXT_RSP_PARMS findNextParms;
	FILE_DIRECTORY_INFO *pfindData;
	FILE_UNIX_INFO *pfindDataUnix;

	xid = GetXid();

	data = kmalloc(4096, GFP_KERNEL);
	pfindData = (FILE_DIRECTORY_INFO *) data;

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_wildcard_path_from_dentry(file->f_dentry);

	cFYI(1, ("\nFull path: %s start at: %lld ", full_path, file->f_pos));

	switch ((int) file->f_pos) {
	case 0:
		if (filldir
		    (direntry, ".", 1, file->f_pos,
		     file->f_dentry->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("\nFilldir for current dir failed "));
			break;
		}
		file->f_pos++;
		/* fallthrough */
	case 1:
		if (filldir
		    (direntry, "..", 2, file->f_pos,
		     file->f_dentry->d_parent->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("\nFilldir for parent dir failed "));
			break;
		}
		file->f_pos++;
		/* fallthrough */
	case 2:
		/* Should we first check if file->private_data is null? */
		rc = CIFSFindFirst(xid, pTcon, full_path, pfindData,
				   &findParms, cifs_sb->local_nls,
				   &Unicode, &UnixSearch);
		cFYI(1,
		     ("Count: %d  End: %d ", findParms.SearchCount,
		      findParms.EndofSearch));

		if (rc == 0) {
			searchHandle = findParms.SearchHandle;
			file->private_data =
			    kmalloc(sizeof (struct cifsFileInfo), GFP_KERNEL);
			if (file->private_data) {
				memset(file->private_data, 0,
				       sizeof (struct cifsFileInfo));
				cifsFile =
				    (struct cifsFileInfo *) file->private_data;
				cifsFile->netfid = searchHandle;
			}

			renew_parental_timestamps(file->f_dentry);

			for (i = 2; i < findParms.SearchCount + 2; i++) {
				if (UnixSearch == FALSE) {
					pfindData->FileNameLength =
					    le32_to_cpu(pfindData->
							FileNameLength);
					if (Unicode == TRUE)
						pfindData->FileNameLength =
						    cifs_strfromUCS_le
						    (pfindData->FileName,
						     (wchar_t *)
						     pfindData->FileName,
						     (pfindData->
						      FileNameLength) / 2,
						     cifs_sb->local_nls);
					qstring.len = pfindData->FileNameLength;
					if (((qstring.len != 1)
					     || (pfindData->FileName[0] != '.'))
					    && ((qstring.len != 2)
						|| (pfindData->
						    FileName[0] != '.')
						|| (pfindData->
						    FileName[1] != '.'))) {
						cifs_filldir(&qstring,
							     pfindData,
							     file, filldir,
							     direntry);
						file->f_pos++;
					}
				} else {	/* UnixSearch */
					pfindDataUnix =
					    (FILE_UNIX_INFO *) pfindData;
					if (Unicode == TRUE)
						qstring.len =
						    cifs_strfromUCS_le
						    (pfindDataUnix->
						     FileName, (wchar_t *)
						     pfindDataUnix->
						     FileName,
						     MAX_PATHCONF,
						     cifs_sb->local_nls);
					else
						qstring.len =
						    strnlen(pfindDataUnix->
							    FileName,
							    MAX_PATHCONF);
					if (((qstring.len != 1)
					     || (pfindDataUnix->
						 FileName[0] != '.'))
					    && ((qstring.len != 2)
						|| (pfindDataUnix->
						    FileName[0] != '.')
						|| (pfindDataUnix->
						    FileName[1] != '.'))) {
						cifs_filldir_unix(&qstring,
								  pfindDataUnix,
								  file,
								  filldir,
								  direntry);
						file->f_pos++;
					}
				}
				pfindData = (FILE_DIRECTORY_INFO *) ((char *) pfindData + le32_to_cpu(pfindData->NextEntryOffset));	/* works also for Unix find struct since this is the first field of both */
				/* BB also should check to make sure that pointer is not beyond the end of the SMB */
			}	/* end for loop */
			if ((findParms.EndofSearch != 0) && cifsFile) {
				cifsFile->endOfSearch = TRUE;
			}
		} else {
			if (cifsFile)
				cifsFile->endOfSearch = TRUE;
			rc = 0;	/* unless parent directory disappeared - do not return error here (eg Access Denied or no more files) */
		}
		break;
	default:
		if (file->private_data == NULL) {
			rc = -EBADF;
			cFYI(1,
			     ("\nReaddir on closed srch, pos = %lld",
			      file->f_pos));
		} else {
			cifsFile = (struct cifsFileInfo *) file->private_data;
			if (cifsFile->endOfSearch) {
				rc = 0;
				cFYI(1, ("\nEnd of search "));
				break;
			}
			searchHandle = cifsFile->netfid;
			rc = CIFSFindNext(xid, pTcon, pfindData,
					  &findNextParms, searchHandle, 0,
					  &Unicode, &UnixSearch);
			cFYI(1,
			     ("Count: %d  End: %d ",
			      findNextParms.SearchCount,
			      findNextParms.EndofSearch));
			if ((rc == 0) && (findNextParms.SearchCount != 0)) {
				for (i = 0; i < findNextParms.SearchCount; i++) {
					pfindData->FileNameLength =
					    le32_to_cpu(pfindData->
							FileNameLength);
					if (UnixSearch == FALSE) {
						if (Unicode == TRUE)
							pfindData->
							    FileNameLength
							    =
							    cifs_strfromUCS_le
							    (pfindData->
							     FileName,
							     (wchar_t *)
							     pfindData->
							     FileName,
							     (pfindData->
							      FileNameLength)
							     / 2,
							     cifs_sb->
							     local_nls);
						qstring.len =
						    pfindData->FileNameLength;
						if (((qstring.len != 1)
						     || (pfindData->
							 FileName[0] != '.'))
						    && ((qstring.len != 2)
							|| (pfindData->
							    FileName[0] != '.')
							|| (pfindData->
							    FileName[1] !=
							    '.'))) {
							cifs_filldir
							    (&qstring,
							     pfindData,
							     file, filldir,
							     direntry);
							file->f_pos++;
						}
					} else {	/* UnixSearch */
						pfindDataUnix =
						    (FILE_UNIX_INFO *)
						    pfindData;
						if (Unicode == TRUE)
							qstring.len =
							    cifs_strfromUCS_le
							    (pfindDataUnix->
							     FileName,
							     (wchar_t *)
							     pfindDataUnix->
							     FileName,
							     MAX_PATHCONF,
							     cifs_sb->
							     local_nls);
						else
							qstring.len =
							    strnlen
							    (pfindDataUnix->
							     FileName,
							     MAX_PATHCONF);

						if (((qstring.len != 1)
						     || (pfindDataUnix->
							 FileName[0] != '.'))
						    && ((qstring.len != 2)
							|| (pfindDataUnix->
							    FileName[0] != '.')
							|| (pfindDataUnix->
							    FileName[1] !=
							    '.'))) {
							cifs_filldir_unix
							    (&qstring,
							     pfindDataUnix,
							     file, filldir,
							     direntry);
							file->f_pos++;
						}
					}
					pfindData = (FILE_DIRECTORY_INFO *) ((char *) pfindData + le32_to_cpu(pfindData->NextEntryOffset));	/* works also for Unix find struct since this is the first field of both */
					/* BB also should check to make sure that pointer is not beyond the end of the SMB */

				}	/* end for loop */
				if (findNextParms.EndofSearch != 0) {
					cifsFile->endOfSearch = TRUE;
				}
			} else {
				cifsFile->endOfSearch = TRUE;
				rc = 0;	/* unless parent directory disappeared - do not return error here (eg Access Denied or no more files) */
			}
		}
	}			/* end switch */

	if (data)
		kfree(data);
	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}

struct address_space_operations cifs_addr_ops = {
	.readpage = cifs_readpage,
	.sync_page = cifs_sync_page,
	.writepage = cifs_writepage,
	.prepare_write = cifs_prepare_write,
	.commit_write = cifs_commit_write
};
