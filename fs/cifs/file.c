/*
 *   fs/cifs/file.c
 *
 *   vfs operations that deal with files
 * 
 *   Copyright (C) International Business Machines  Corp., 2002,2003
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
#include <linux/pagemap.h>
#include <linux/pagevec.h>
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
	struct list_head * tmp;
	char *full_path = NULL;
	int desiredAccess = 0x20197;
	int disposition;
	__u16 netfid;
	FILE_ALL_INFO * buf = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	if (file->f_flags & O_CREAT) {
		/* search inode for this file and fill in file->private_data = */
		pCifsInode = CIFS_I(file->f_dentry->d_inode);
		read_lock(&GlobalSMBSeslock);
		list_for_each(tmp, &pCifsInode->openFileList) {            
			pCifsFile = list_entry(tmp,struct cifsFileInfo, flist);           
			if((pCifsFile->pfile == NULL)&& (pCifsFile->pid = current->pid)){		
			/* set mode ?? */
				pCifsFile->pfile = file; /* needed for writepage */
				file->private_data = pCifsFile;
				break;
			}
		}
		read_unlock(&GlobalSMBSeslock);
		if(file->private_data != NULL) {
			rc = 0;
			FreeXid(xid);
			return rc;
		} else {
			if(file->f_flags & O_EXCL)
				cERROR(1,("could not find file instance for new file %p ",file));
		}
	}

	full_path = build_path_from_dentry(file->f_dentry);

	cFYI(1, (" inode = 0x%p file flags are 0x%x for %s", inode, file->f_flags,full_path));
	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		desiredAccess = GENERIC_READ;
	else if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		desiredAccess = GENERIC_WRITE;
	else if ((file->f_flags & O_ACCMODE) == O_RDWR) {
		/* GENERIC_ALL is too much permission to request */
		/* can cause unnecessary access denied on create */
		/* desiredAccess = GENERIC_ALL; */
		desiredAccess = GENERIC_READ | GENERIC_WRITE;
	}

/*********************************************************************
 *  open flag mapping table:
 *  
 *	POSIX Flag            CIFS Disposition
 *	----------            ---------------- 
 *	O_CREAT               FILE_OPEN_IF
 *	O_CREAT | O_EXCL      FILE_CREATE
 *	O_CREAT | O_TRUNC     FILE_OVERWRITE_IF
 *	O_TRUNC               FILE_OVERWRITE
 *	none of the above     FILE_OPEN
 *
 *	Note that there is not a direct match between disposition
 *	FILE_SUPERSEDE (ie create whether or not file exists although 
 *	O_CREAT | O_TRUNC is similar but truncates the existing
 *	file rather than creating a new file as FILE_SUPERSEDE does
 *	(which uses the attributes / metadata passed in on open call)
 *?
 *?  O_SYNC is a reasonable match to CIFS writethrough flag  
 *?  and the read write flags match reasonably.  O_LARGEFILE
 *?  is irrelevant because largefile support is always used
 *?  by this client. Flags O_APPEND, O_DIRECT, O_DIRECTORY,
 *	 O_FASYNC, O_NOFOLLOW, O_NONBLOCK need further investigation
 *********************************************************************/

	if((file->f_flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		disposition = FILE_CREATE;
	else if((file->f_flags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC))
		disposition = FILE_OVERWRITE_IF;
	else if((file->f_flags & O_CREAT) == O_CREAT)
		disposition = FILE_OPEN_IF;
	else
		disposition = FILE_OPEN;

	if (oplockEnabled)
		oplock = REQ_OPLOCK;
	else
		oplock = FALSE;

	/* BB pass O_SYNC flag through on file attributes .. BB */

	/* Also refresh inode by passing in file_info buf returned by SMBOpen 
	   and calling get_inode_info with returned buf (at least 
	   helps non-Unix server case */
	buf = kmalloc(sizeof(FILE_ALL_INFO),GFP_KERNEL);
	if(buf==0) {
		if (full_path)
			kfree(full_path);
		FreeXid(xid);
		return -ENOMEM;
	}
	rc = CIFSSMBOpen(xid, pTcon, full_path, disposition, desiredAccess,
			CREATE_NOT_DIR, &netfid, &oplock, buf, cifs_sb->local_nls);
	if (rc) {
		cFYI(1, ("cifs_open returned 0x%x ", rc));
		cFYI(1, ("oplock: %d ", oplock));	
	} else {
		if(file->private_data)
			kfree(file->private_data);
		file->private_data =
			kmalloc(sizeof (struct cifsFileInfo), GFP_KERNEL);
		if (file->private_data) {
			memset(file->private_data, 0, sizeof(struct cifsFileInfo));
			pCifsFile = (struct cifsFileInfo *) file->private_data;
			pCifsFile->netfid = netfid;
			pCifsFile->pid = current->pid;
			init_MUTEX(&pCifsFile->fh_sem);
			pCifsFile->pfile = file; /* needed for writepage */
			pCifsFile->pInode = inode;
			pCifsFile->invalidHandle = FALSE;
			pCifsFile->closePend     = FALSE;
			write_lock(&file->f_owner.lock);
			write_lock(&GlobalSMBSeslock);
			list_add(&pCifsFile->tlist,&pTcon->openFileList);
			pCifsInode = CIFS_I(file->f_dentry->d_inode);
			if(pCifsInode) {
				list_add(&pCifsFile->flist,&pCifsInode->openFileList);
				write_unlock(&GlobalSMBSeslock);
				write_unlock(&file->f_owner.lock);
				if(pCifsInode->clientCanCacheRead) {
					/* we have the inode open somewhere else
					   no need to discard cache data */
				} else {
					if(buf) {
					/* BB need same check in cifs_create too? */

					/* if not oplocked, invalidate inode pages if mtime 
					   or file size changed */
						struct timespec temp;
						temp = cifs_NTtimeToUnix(le64_to_cpu(buf->LastWriteTime));
						if(timespec_equal(&file->f_dentry->d_inode->i_mtime,&temp) && 
							(file->f_dentry->d_inode->i_size == (loff_t)le64_to_cpu(buf->EndOfFile))) {
							cFYI(1,("inode unchanged on server"));
						} else {
							if(file->f_dentry->d_inode->i_mapping) {
							/* BB no need to lock inode until after invalidate*/
							/* since namei code should already have it locked?*/
								filemap_fdatawrite(file->f_dentry->d_inode->i_mapping);
								filemap_fdatawait(file->f_dentry->d_inode->i_mapping);
							}
							cFYI(1,("invalidating remote inode since open detected it changed"));
							invalidate_remote_inode(file->f_dentry->d_inode);
						}
					}
				}
				if (pTcon->ses->capabilities & CAP_UNIX)
					rc = cifs_get_inode_info_unix(&file->f_dentry->d_inode,
						full_path, inode->i_sb);
				else
					rc = cifs_get_inode_info(&file->f_dentry->d_inode,
						full_path, buf, inode->i_sb);

				if(oplock == OPLOCK_EXCLUSIVE) {
					pCifsInode->clientCanCacheAll = TRUE;
					pCifsInode->clientCanCacheRead = TRUE;
					cFYI(1,("Exclusive Oplock granted on inode %p",file->f_dentry->d_inode));
				} else if(oplock == OPLOCK_READ)
					pCifsInode->clientCanCacheRead = TRUE;
			} else {
				write_unlock(&GlobalSMBSeslock);
				write_unlock(&file->f_owner.lock);
			}
			if(file->f_flags & O_CREAT) {           
				/* time to set mode which we can not set earlier due
				 to problems creating new read-only files */
				if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)                
					CIFSSMBUnixSetPerms(xid, pTcon, full_path, inode->i_mode,
						(__u64)-1, 
						(__u64)-1,
						0 /* dev */,
						cifs_sb->local_nls);
				else {/* BB implement via Windows security descriptors */
			/* eg CIFSSMBWinSetPerms(xid,pTcon,full_path,mode,-1,-1,local_nls);*/
			/* in the meantime could set r/o dos attribute when perms are eg:
					mode & 0222 == 0 */
				}
			}
		}
	}

	if (buf)
		kfree(buf);
	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

/* Try to reaquire byte range locks that were released when session */
/* to server was lost */
static int cifs_relock_file(struct cifsFileInfo * cifsFile)
{
	int rc = 0;

/* BB list all locks open on this file and relock */

	return rc;
}

static int cifs_reopen_file(struct inode *inode, struct file *file)
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
	FILE_ALL_INFO * buf = NULL;

	if(inode == NULL)
		return -EBADF;
	if (file->private_data) {
		pCifsFile = (struct cifsFileInfo *) file->private_data;
	} else
		return -EBADF;

	xid = GetXid();
	down(&pCifsFile->fh_sem);
	if(pCifsFile->invalidHandle == FALSE) {
		up(&pCifsFile->fh_sem);
		FreeXid(xid);
		return 0;
	}


	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(file->f_dentry);

	cFYI(1, (" inode = 0x%p file flags are 0x%x for %s", inode, file->f_flags,full_path));
	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		desiredAccess = GENERIC_READ;
	else if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		desiredAccess = GENERIC_WRITE;
	else if ((file->f_flags & O_ACCMODE) == O_RDWR) {
		/* GENERIC_ALL is too much permission to request */
		/* can cause unnecessary access denied on create */
		/* desiredAccess = GENERIC_ALL; */
		desiredAccess = GENERIC_READ | GENERIC_WRITE;
	}

	if (oplockEnabled)
		oplock = REQ_OPLOCK;
	else
		oplock = FALSE;

		/* BB pass O_SYNC flag through on file attributes .. BB */

		/* Also refresh inode by passing in file_info buf returned by SMBOpen
		   and calling get_inode_info with returned buf (at least
		   helps non-Unix server case */
	buf = kmalloc(sizeof(FILE_ALL_INFO),GFP_KERNEL);
	if(buf==0) {
		up(&pCifsFile->fh_sem);
		if (full_path)
			kfree(full_path);
		FreeXid(xid);
		return -ENOMEM;
	}
	rc = CIFSSMBOpen(xid, pTcon, full_path, disposition, desiredAccess,
				CREATE_NOT_DIR, &netfid, &oplock, buf, cifs_sb->local_nls);
	if (rc) {
		up(&pCifsFile->fh_sem);
		cFYI(1, ("cifs_open returned 0x%x ", rc));
		cFYI(1, ("oplock: %d ", oplock));
	} else {
		pCifsFile->netfid = netfid;
		pCifsFile->invalidHandle = FALSE;
		up(&pCifsFile->fh_sem);
		pCifsInode = CIFS_I(inode);
		if(pCifsInode) {
			if (pTcon->ses->capabilities & CAP_UNIX)
				rc = cifs_get_inode_info_unix(&inode,
						full_path, inode->i_sb);
			else
				rc = cifs_get_inode_info(&inode,
						full_path, buf, inode->i_sb);

			if(oplock == OPLOCK_EXCLUSIVE) {
				pCifsInode->clientCanCacheAll =  TRUE;
				pCifsInode->clientCanCacheRead = TRUE;
				cFYI(1,("Exclusive Oplock granted on inode %p",file->f_dentry->d_inode));
			} else if(oplock == OPLOCK_READ) {
				pCifsInode->clientCanCacheRead = TRUE;
				pCifsInode->clientCanCacheAll =  FALSE;
			} else {
				pCifsInode->clientCanCacheRead = FALSE;
				pCifsInode->clientCanCacheAll =  FALSE;
			}
			cifs_relock_file(pCifsFile);
		}
	}

	if (buf)
		kfree(buf);
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

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;
	if (pSMBFile) {
		pSMBFile->closePend    = TRUE;
		write_lock(&file->f_owner.lock);
		if(pTcon) {
			/* no sense reconnecting to close a file that is
				already closed */
			if (pTcon->tidStatus != CifsNeedReconnect) {
				write_unlock(&file->f_owner.lock);
				rc = CIFSSMBClose(xid,pTcon,pSMBFile->netfid);
				write_lock(&file->f_owner.lock);
			}
		}
		list_del(&pSMBFile->flist);
		list_del(&pSMBFile->tlist);
		write_unlock(&file->f_owner.lock);
		if(pSMBFile->search_resume_name)
			kfree(pSMBFile->search_resume_name);
		kfree(file->private_data);
		file->private_data = NULL;
	} else
		rc = -EBADF;

	if(list_empty(&(CIFS_I(inode)->openFileList))) {
		cFYI(1,("closing last open instance for inode %p",inode));
		/* if the file is not open we do not know if we can cache
		info on this inode, much less write behind and read ahead */
		CIFS_I(inode)->clientCanCacheRead = FALSE;
		CIFS_I(inode)->clientCanCacheAll  = FALSE;
	}
	if((rc ==0) && CIFS_I(inode)->write_behind_rc)
		rc = CIFS_I(inode)->write_behind_rc;
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

	cFYI(1, ("Closedir inode = 0x%p with ", inode));

	xid = GetXid();

	if (pSMBFileStruct) {
		cFYI(1, ("Freeing private data in close dir"));
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
	int wait_flag = FALSE;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	length = 1 + pfLock->fl_end - pfLock->fl_start;

	rc = -EACCES;

	xid = GetXid();

	cFYI(1,
	     ("Lock parm: 0x%x flockflags: 0x%x flocktype: 0x%x start: %lld end: %lld",
	      cmd, pfLock->fl_flags, pfLock->fl_type, pfLock->fl_start,
	      pfLock->fl_end));

	if (pfLock->fl_flags & FL_POSIX)
		cFYI(1, ("Posix "));
	if (pfLock->fl_flags & FL_FLOCK)
		cFYI(1, ("Flock "));
	if (pfLock->fl_flags & FL_SLEEP) {
		cFYI(1, ("Blocking lock "));
		wait_flag = TRUE;
	}
	if (pfLock->fl_flags & FL_ACCESS)
		cFYI(1, ("Process suspended by mandatory locking - not implemented yet "));
	if (pfLock->fl_flags & FL_LEASE)
		cFYI(1, ("Lease on file - not implemented yet"));
	if (pfLock->fl_flags & (~(FL_POSIX | FL_FLOCK | FL_SLEEP | FL_ACCESS | FL_LEASE)))
		cFYI(1, ("Unknown lock flags 0x%x",pfLock->fl_flags));

	if (pfLock->fl_type == F_WRLCK) {
		cFYI(1, ("F_WRLCK "));
		numLock = 1;
	} else if (pfLock->fl_type == F_UNLCK) {
		cFYI(1, ("F_UNLCK "));
		numUnlock = 1;
	} else if (pfLock->fl_type == F_RDLCK) {
		cFYI(1, ("F_RDLCK "));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else if (pfLock->fl_type == F_EXLCK) {
		cFYI(1, ("F_EXLCK "));
		numLock = 1;
	} else if (pfLock->fl_type == F_SHLCK) {
		cFYI(1, ("F_SHLCK "));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else
		cFYI(1, ("Unknown type of lock "));

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
					("Error unlocking previously locked range %d during test of lock ",
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
			 wait_flag);
	FreeXid(xid);
	return rc;
}

ssize_t
cifs_write(struct file * file, const char *write_data,
	   size_t write_size, loff_t * poffset)
{
	int rc = 0;
	unsigned int bytes_written = 0;
	unsigned int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid, long_op;
	struct cifsFileInfo * open_file;

	xid = GetXid();

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	/*cFYI(1,
	   (" write %d bytes to offset %lld of %s", write_size,
	   *poffset, file->f_dentry->d_name.name)); */

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *) file->private_data;


	if (*poffset > file->f_dentry->d_inode->i_size)
		long_op = 2;  /* writes past end of file can take a long time */
	else
		long_op = 1;

	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = -EAGAIN;
		while(rc == -EAGAIN) {
			if ((open_file->invalidHandle) && (!open_file->closePend)) {
				rc = cifs_reopen_file(file->f_dentry->d_inode,file);
				if(rc != 0)
					break;
			}

			rc = CIFSSMBWrite(xid, pTcon,
				   open_file->netfid,
				  write_size - total_written, *poffset,
				  &bytes_written,
				  write_data + total_written, long_op);
		}
		if (rc || (bytes_written == 0)) {
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
	file->f_dentry->d_inode->i_ctime = file->f_dentry->d_inode->i_mtime =
		CURRENT_TIME;
	if (bytes_written > 0) {
		if (*poffset > file->f_dentry->d_inode->i_size)
			i_size_write(file->f_dentry->d_inode, *poffset);
	}
	mark_inode_dirty_sync(file->f_dentry->d_inode);
	FreeXid(xid);
	return total_written;
}

static int
cifs_partialpagewrite(struct page *page,unsigned from, unsigned to)
{
	struct address_space *mapping = page->mapping;
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	char * write_data;
	int rc = -EFAULT;
	int bytes_written = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct inode *inode = page->mapping->host;
	struct cifsInodeInfo *cifsInode;
	struct cifsFileInfo *open_file = NULL;
	struct list_head *tmp;
	struct list_head *tmp1;

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	/* figure out which file struct to use 
	if (file->private_data == NULL) {
		return -EBADF;
	}     
	 */
	if (!mapping) {
		return -EFAULT;
	} else if(!mapping->host) {
		return -EFAULT;
	}

	offset += (loff_t)from;
	write_data = kmap(page);
	write_data += from;

	if((to > PAGE_CACHE_SIZE) || (from > to)) {
		kunmap(page);
		return -EIO;
	}

	/* racing with truncate? */
	if(offset > mapping->host->i_size) {
		kunmap(page);
		return 0; /* don't care */
	}

	/* check to make sure that we are not extending the file */
	if(mapping->host->i_size - offset < (loff_t)to)
		to = (unsigned)(mapping->host->i_size - offset); 
		

	cifsInode = CIFS_I(mapping->host);
	read_lock(&GlobalSMBSeslock); 
	list_for_each_safe(tmp, tmp1, &cifsInode->openFileList) {            
		open_file = list_entry(tmp,struct cifsFileInfo, flist);
		/* We check if file is open for writing first */
		if((open_file->pfile) && 
		   ((open_file->pfile->f_flags & O_RDWR) || 
			(open_file->pfile->f_flags & O_WRONLY))) {
			read_unlock(&GlobalSMBSeslock);
			bytes_written = cifs_write(open_file->pfile, write_data,
					to-from, &offset);
			read_lock(&GlobalSMBSeslock);
		/* Does mm or vfs already set times? */
			inode->i_atime = inode->i_mtime = CURRENT_TIME;
			if ((bytes_written > 0) && (offset)) {
				rc = 0;
			} else if(bytes_written < 0) {
				rc = bytes_written;
			}
			break;  /* now that we found a valid file handle
				and tried to write to it we are done, no
				sense continuing to loop looking for another */
		}
		if(tmp->next == NULL) {
			cFYI(1,("File instance %p removed",tmp));
			break;
		}
	}
	read_unlock(&GlobalSMBSeslock);
	if(open_file == NULL) {
		cFYI(1,("No writeable filehandles for inode"));
		rc = -EIO;
	}

	kunmap(page);
	return rc;
}

#if 0
static int
cifs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	int rc = -EFAULT;
	int xid;

	xid = GetXid();
/* call 16K write then Setpageuptodate */
	FreeXid(xid);
	return rc;
}
#endif

static int
cifs_writepage(struct page* page, struct writeback_control *wbc)
{
	int rc = -EFAULT;
	int xid;

	xid = GetXid();
/* BB add check for wbc flags */
	page_cache_get(page);
	
	rc = cifs_partialpagewrite(page,0,PAGE_CACHE_SIZE);
	SetPageUptodate(page); /* BB add check for error and Clearuptodate? */
	unlock_page(page);
	page_cache_release(page);	
	FreeXid(xid);
	return rc;
}

static int
cifs_commit_write(struct file *file, struct page *page, unsigned offset,
		  unsigned to)
{
	int xid;
	int rc = 0;
	struct inode *inode = page->mapping->host;
	loff_t position = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;
	/* struct cifsFileInfo *open_file;
	struct cifs_sb_info *cifs_sb; */

	xid = GetXid();
	cFYI(1,("commit write for page %p up to position %lld for %d",page,position,to));
	if (position > inode->i_size){
		i_size_write(inode, position);
		/*if (file->private_data == NULL) {
			rc = -EBADF;
		} else {
			open_file = (struct cifsFileInfo *)file->private_data;
			cifs_sb = CIFS_SB(inode->i_sb);
			rc = -EAGAIN;
			while(rc == -EAGAIN) {
				if((open_file->invalidHandle) && 
				  (!open_file->closePend)) {
					rc = cifs_reopen_file(file->f_dentry->d_inode,file);
					if(rc != 0)
						break;
				}
				if(!open_file->closePend) {
					rc = CIFSSMBSetFileSize(xid, cifs_sb->tcon, 
						position, open_file->netfid,
						open_file->pid,FALSE);
				} else {
					rc = -EBADF;
					break;
				}
			}
			cFYI(1,(" SetEOF (commit write) rc = %d",rc));
		}*/
	}
	set_page_dirty(page);

	FreeXid(xid);
	return rc;
}

int
cifs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int xid;
	int rc = 0;
	struct inode * inode = file->f_dentry->d_inode;

	xid = GetXid();

	cFYI(1, ("Sync file - name: %s datasync: 0x%x ", 
		dentry->d_name.name, datasync));
	
	rc = filemap_fdatawrite(inode->i_mapping);
	if(rc == 0)
		CIFS_I(inode)->write_behind_rc = 0;
	FreeXid(xid);
	return rc;
}

/* static int
cifs_sync_page(struct page *page)
{
	struct address_space *mapping;
	struct inode *inode;
	unsigned long index = page->index;
	unsigned int rpages = 0;
	int rc = 0;

	cFYI(1,("sync page %p",page));
	mapping = page->mapping;
	if (!mapping)
		return 0;
	inode = mapping->host;
	if (!inode)
		return 0;*/

/*	fill in rpages then 
    result = cifs_pagein_inode(inode, index, rpages); *//* BB finish */

/*   cFYI(1, ("rpages is %d for sync page of Index %ld ", rpages, index));

	if (rc < 0)
		return rc;
	return 0;
} */

/*
 * As file closes, flush all cached write data for this inode checking
 * for write behind errors.
 *
 */
int cifs_flush(struct file *file)
{
	struct inode * inode = file->f_dentry->d_inode;
	int rc = 0;

	/* Rather than do the steps manually: */
	/* lock the inode for writing */
	/* loop through pages looking for write behind data (dirty pages) */
	/* coalesce into contiguous 16K (or smaller) chunks to write to server */
	/* send to server (prefer in parallel) */
	/* deal with writebehind errors */
	/* unlock inode for writing */
	/* filemapfdatawrite appears easier for the time being */

	rc = filemap_fdatawrite(inode->i_mapping);
	if(rc == 0) /* reset wb rc if we were able to write out dirty pages */
		CIFS_I(inode)->write_behind_rc = 0;
		
	cFYI(1,("Flush inode %p file %p rc %d",inode,file,rc));

	return rc;
}


ssize_t
cifs_read(struct file * file, char *read_data, size_t read_size,
	  loff_t * poffset)
{
	int rc = -EACCES;
	unsigned int bytes_read = 0;
	unsigned int total_read;
	unsigned int current_read_size;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;
	char * current_offset;
	struct cifsFileInfo * open_file;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;

	for (total_read = 0,current_offset=read_data; read_size > total_read;
				total_read += bytes_read,current_offset+=bytes_read) {
		current_read_size = min_t(const int,read_size - total_read,cifs_sb->rsize);
		rc = -EAGAIN;
		while(rc == -EAGAIN) {
			if ((open_file->invalidHandle) && (!open_file->closePend)) {
				rc = cifs_reopen_file(file->f_dentry->d_inode,file);
				if(rc != 0)
					break;
			}

			rc = CIFSSMBRead(xid, pTcon,
				 open_file->netfid,
				 current_read_size, *poffset,
				 &bytes_read, &current_offset);
		}
		if (rc || (bytes_read == 0)) {
			if (total_read) {
				break;
			} else {
				FreeXid(xid);
				return rc;
			}
		} else {
			*poffset += bytes_read;
		}
	}

	FreeXid(xid);
	return total_read;
}

int cifs_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct dentry * dentry = file->f_dentry;
	int	rc, xid;

	xid = GetXid();
	rc = cifs_revalidate(dentry);
	if (rc) {
		cFYI(1,("Validation prior to mmap failed, error=%d", rc));
		FreeXid(xid);
		return rc;
	}
	rc = generic_file_mmap(file, vma);
	FreeXid(xid);
	return rc;
}

static void cifs_copy_cache_pages(struct address_space *mapping, 
		struct list_head *pages, int bytes_read, 
		char *data,struct pagevec * plru_pvec)
{
	struct page *page;
	char * target;

	while (bytes_read > 0) {
		if(list_empty(pages))
			break;

		page = list_entry(pages->prev, struct page, lru);
		list_del(&page->lru);

		if (add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
			page_cache_release(page);
			cFYI(1,("Add page cache failed"));
			continue;
		}

		target = kmap_atomic(page,KM_USER0);

		if(PAGE_CACHE_SIZE > bytes_read) {
			memcpy(target,data,bytes_read);
			/* zero the tail end of this partial page */
			memset(target+bytes_read,0,PAGE_CACHE_SIZE-bytes_read);
			bytes_read = 0;
		} else {
			memcpy(target,data,PAGE_CACHE_SIZE);
			bytes_read -= PAGE_CACHE_SIZE;
		}
		kunmap_atomic(target,KM_USER0);

		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);
		if (!pagevec_add(plru_pvec, page))
			__pagevec_lru_add(plru_pvec);
		data += PAGE_CACHE_SIZE;
	}
	return;
}


static int
cifs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *page_list, unsigned num_pages)
{
	int rc = -EACCES;
	int xid;
	loff_t offset;
	struct page * page;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int bytes_read = 0;
	unsigned int read_size,i;
	char * smb_read_data = 0;
	struct smb_com_read_rsp * pSMBr;
	struct pagevec lru_pvec;
	struct cifsFileInfo * open_file;

	xid = GetXid();
	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	pagevec_init(&lru_pvec, 0);

	for(i = 0;i<num_pages;) {
		unsigned contig_pages;
		struct page * tmp_page;
		unsigned long expected_index;

		if(list_empty(page_list)) {
			break;
		}
		page = list_entry(page_list->prev, struct page, lru);
		offset = (loff_t)page->index << PAGE_CACHE_SHIFT;

		/* count adjacent pages that we will read into */
		contig_pages = 0;
		expected_index = list_entry(page_list->prev,struct page,lru)->index;
		list_for_each_entry_reverse(tmp_page,page_list,lru) {
			if(tmp_page->index == expected_index) {
				contig_pages++;
				expected_index++;
			} else {
				break; 
			}
		}
		if(contig_pages + i >  num_pages) {
			contig_pages = num_pages - i;
		}

		/* for reads over a certain size could initiate async read ahead */

		read_size = contig_pages * PAGE_CACHE_SIZE;
		/* Read size needs to be in multiples of one page */
		read_size = min_t(const unsigned int,read_size,cifs_sb->rsize & PAGE_CACHE_MASK);

		rc = -EAGAIN;
		while(rc == -EAGAIN) {
			if ((open_file->invalidHandle) && (!open_file->closePend)) {
				rc = cifs_reopen_file(file->f_dentry->d_inode,file);
				if(rc != 0)
					break;
			}

			rc = CIFSSMBRead(xid, pTcon,
				open_file->netfid,
				read_size, offset,
				&bytes_read, &smb_read_data);
			/* BB need to check return code here */
			if(rc== -EAGAIN) {
				if(smb_read_data) {
					cifs_buf_release(smb_read_data);
					smb_read_data = 0;
				}
			}
		}
		if ((rc < 0) || (smb_read_data == NULL)) {
			cFYI(1,("Read error in readpages: %d",rc));
			/* clean up remaing pages off list */
			while (!list_empty(page_list) && (i < num_pages)) {
				page = list_entry(page_list->prev, struct page, lru);
				list_del(&page->lru);
				page_cache_release(page);
			}
			break;
		} else if (bytes_read > 0) {
			pSMBr = (struct smb_com_read_rsp *)smb_read_data;
			cifs_copy_cache_pages(mapping, page_list, bytes_read,
				smb_read_data + 4 /* RFC1000 hdr */ +
				le16_to_cpu(pSMBr->DataOffset), &lru_pvec);

			i +=  bytes_read >> PAGE_CACHE_SHIFT;

			if((int)(bytes_read & PAGE_CACHE_MASK) != bytes_read) {
				cFYI(1,("Partial page %d of %d read to cache",i++,num_pages));

				i++; /* account for partial page */

				/* server copy of file can have smaller size than client */
				/* BB do we need to verify this common case ? this case is ok - 
				if we are at server EOF we will hit it on next read */

			/* while(!list_empty(page_list) && (i < num_pages)) {
					page = list_entry(page_list->prev,struct page, list);
					list_del(&page->list);
					page_cache_release(page);
				}
				break; */
			}
		} else {
			cFYI(1,("No bytes read (%d) at offset %lld . Cleaning remaining pages from readahead list",bytes_read,offset)); 
			/* BB turn off caching and do new lookup on file size at server? */
			while (!list_empty(page_list) && (i < num_pages)) {
				page = list_entry(page_list->prev, struct page, lru);
				list_del(&page->lru);
				page_cache_release(page); /* BB removeme - replace with zero of page? */
			}
			break;
		}
		if(smb_read_data) {
			cifs_buf_release(smb_read_data);
			smb_read_data = 0;
		}
		bytes_read = 0;
	}

	pagevec_lru_add(&lru_pvec);

/* need to free smb_read_data buf before exit */
	if(smb_read_data) {
		cifs_buf_release(smb_read_data);
		smb_read_data = 0;
	} 

	FreeXid(xid);
	return rc;
}

static int
cifs_readpage(struct file *file, struct page *page)
{
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	char * read_data;
	int rc = -EACCES;
	int xid;

	xid = GetXid();

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	cFYI(0,("readpage %p at offset %d 0x%x\n",page,(int)offset,(int)offset));

	page_cache_get(page);
	read_data = kmap(page);
	/* for reads over a certain size could initiate async read ahead */

	rc = cifs_read(file, read_data, PAGE_CACHE_SIZE, &offset);

	if (rc < 0)
		goto io_error;
	else {
		cFYI(1,("Bytes read %d ",rc));
	}

	file->f_dentry->d_inode->i_atime = CURRENT_TIME;

	if(PAGE_CACHE_SIZE > rc) {
		memset(read_data+rc, 0, PAGE_CACHE_SIZE - rc);
	}
	flush_dcache_page(page);
	SetPageUptodate(page);
	rc = 0;

io_error:
	kunmap(page);
	unlock_page(page);

	page_cache_release(page);
	FreeXid(xid);
	return rc;
}

void
fill_in_inode(struct inode *tmp_inode,
	      FILE_DIRECTORY_INFO * pfindData, int *pobject_type)
{
	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(tmp_inode->i_sb);

	pfindData->ExtFileAttributes =
	    le32_to_cpu(pfindData->ExtFileAttributes);
	pfindData->AllocationSize = le64_to_cpu(pfindData->AllocationSize);
	pfindData->EndOfFile = le64_to_cpu(pfindData->EndOfFile);
	cifsInfo->cifsAttrs = pfindData->ExtFileAttributes;
	cifsInfo->time = jiffies;
	atomic_inc(&cifsInfo->inUse);	/* inc on every refresh of inode info */

	/* Linux can not store file creation time unfortunately so ignore it */
	tmp_inode->i_atime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastAccessTime));
	tmp_inode->i_mtime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastWriteTime));
	tmp_inode->i_ctime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->ChangeTime));
	/* treat dos attribute of read-only as read-only mode bit e.g. 555? */
	/* 2767 perms - indicate mandatory locking */
		/* BB fill in uid and gid here? with help from winbind? 
			or retrieve from NTFS stream extended attribute */
	tmp_inode->i_uid = cifs_sb->mnt_uid;
	tmp_inode->i_gid = cifs_sb->mnt_gid;
	/* set default mode. will override for dirs below */
	tmp_inode->i_mode = cifs_sb->mnt_file_mode;

	cFYI(0,
	     ("CIFS FFIRST: Attributes came in as 0x%x",
	      pfindData->ExtFileAttributes));
	if (pfindData->ExtFileAttributes & ATTR_REPARSE) {
		*pobject_type = DT_LNK;
		/* BB can this and S_IFREG or S_IFDIR be set as in Windows? */
		tmp_inode->i_mode |= S_IFLNK;
	} else if (pfindData->ExtFileAttributes & ATTR_DIRECTORY) {
		*pobject_type = DT_DIR;
		/* override default perms since we do not lock dirs */
		tmp_inode->i_mode = cifs_sb->mnt_dir_mode;
		tmp_inode->i_mode |= S_IFDIR;
	} else {
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
		if(pfindData->ExtFileAttributes & ATTR_READONLY)
			tmp_inode->i_mode &= ~(S_IWUGO);

	}/* could add code here - to validate if device or weird share type? */

	/* can not fill in nlink here as in qpathinfo version and Unx search */
	i_size_write(tmp_inode,pfindData->EndOfFile);
	tmp_inode->i_blocks =
		(tmp_inode->i_blksize - 1 + pfindData->AllocationSize) >> tmp_inode->i_blkbits;
	if (pfindData->AllocationSize < pfindData->EndOfFile)
		cFYI(1, ("Possible sparse file: allocation size less than end of file "));
	cFYI(1,
	     ("File Size %ld and blocks %ld and blocksize %ld",
	      (unsigned long) tmp_inode->i_size, tmp_inode->i_blocks,
	      tmp_inode->i_blksize));
	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, (" File inode "));
		tmp_inode->i_op = &cifs_file_inode_ops;
		tmp_inode->i_fop = &cifs_file_ops;
		tmp_inode->i_data.a_ops = &cifs_addr_ops;
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, (" Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, (" Symbolic Link inode "));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
	} else {
		cFYI(1, (" Init special inode "));
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   tmp_inode->i_rdev);
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
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastAccessTime));
	tmp_inode->i_mtime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastModificationTime));
	tmp_inode->i_ctime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastStatusChange));

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
	i_size_write(tmp_inode,pfindData->EndOfFile);
	tmp_inode->i_blocks =
                (tmp_inode->i_blksize - 1 + pfindData->NumOfBytes) >> tmp_inode->i_blkbits;

	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, ("File inode"));
		tmp_inode->i_op = &cifs_file_inode_ops;
		tmp_inode->i_fop = &cifs_file_ops;
		tmp_inode->i_data.a_ops = &cifs_addr_ops;
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, ("Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, ("Symbolic Link inode"));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
/* tmp_inode->i_fop = *//* do not need to set to anything */
	} else {
		cFYI(1, ("Special inode")); 
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   tmp_inode->i_rdev);
	}
}

static void
construct_dentry(struct qstr *qstring, struct file *file,
		 struct inode **ptmp_inode, struct dentry **pnew_dentry)
{
	struct dentry *tmp_dentry;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	cFYI(1, ("For %s ", qstring->name));
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	qstring->hash = full_name_hash(qstring->name, qstring->len);
	tmp_dentry = d_lookup(file->f_dentry, qstring);
	if (tmp_dentry) {
		cFYI(0, (" existing dentry with inode 0x%p", tmp_dentry->d_inode));
		*ptmp_inode = tmp_dentry->d_inode;
		/* BB overwrite the old name? i.e. tmp_dentry->d_name and tmp_dentry->d_name.len ?? */
		if(*ptmp_inode == NULL) {
	                *ptmp_inode = new_inode(file->f_dentry->d_sb);
			d_instantiate(tmp_dentry, *ptmp_inode);
		}
	} else {
		tmp_dentry = d_alloc(file->f_dentry, qstring);
		if(tmp_dentry == NULL) {
			cERROR(1,("Failed allocating dentry"));
			return;
		}
			
		*ptmp_inode = new_inode(file->f_dentry->d_sb);
		tmp_dentry->d_op = &cifs_dentry_ops;
		cFYI(0, (" instantiate dentry 0x%p with inode 0x%p ",
			 tmp_dentry, *ptmp_inode));
		d_instantiate(tmp_dentry, *ptmp_inode);
		d_rehash(tmp_dentry);
	}

	tmp_dentry->d_time = jiffies;
	*pnew_dentry = tmp_dentry;
}

static void reset_resume_key(struct file * dir_file, 
				unsigned char * filename, 
				unsigned int len,int Unicode,struct nls_table * nls_tab) {
	struct cifsFileInfo *cifsFile;

	cifsFile = (struct cifsFileInfo *)dir_file->private_data;
	if(cifsFile == NULL)
		return;
	if(cifsFile->search_resume_name) {
		kfree(cifsFile->search_resume_name);
	}

	if(Unicode) 
		len *= 2;
	cifsFile->resume_name_length = len;

	cifsFile->search_resume_name = 
		kmalloc(cifsFile->resume_name_length, GFP_KERNEL);

	if(Unicode)
		cifs_strtoUCS((wchar_t *) cifsFile->search_resume_name,
			filename, len, nls_tab);
	else
		memcpy(cifsFile->search_resume_name, filename, 
		   cifsFile->resume_name_length);
	cFYI(1,("Reset resume key to: %s with len %d",filename,len));
	return;
}



static int
cifs_filldir(struct qstr *pqstring, FILE_DIRECTORY_INFO * pfindData,
	     struct file *file, filldir_t filldir, void *direntry)
{
	struct inode *tmp_inode;
	struct dentry *tmp_dentry;
	int object_type,rc;

	pqstring->name = pfindData->FileName;
	pqstring->len = pfindData->FileNameLength;

	construct_dentry(pqstring, file, &tmp_inode, &tmp_dentry);

	fill_in_inode(tmp_inode, pfindData, &object_type);
	rc = filldir(direntry, pfindData->FileName, pqstring->len, file->f_pos,
		tmp_inode->i_ino, object_type);
	if(rc) {
		/* due to readdir error we need to recalculate resume 
		key so next readdir will restart on right entry */
		cFYI(1,("Error %d on filldir of %s",rc ,pfindData->FileName));
	}
	dput(tmp_dentry);
	return rc;
}

static int
cifs_filldir_unix(struct qstr *pqstring,
		  FILE_UNIX_INFO * pUnixFindData, struct file *file,
		  filldir_t filldir, void *direntry)
{
	struct inode *tmp_inode;
	struct dentry *tmp_dentry;
	int object_type, rc;

	pqstring->name = pUnixFindData->FileName;
	pqstring->len = strnlen(pUnixFindData->FileName, MAX_PATHCONF);

	construct_dentry(pqstring, file, &tmp_inode, &tmp_dentry);

	unix_fill_in_inode(tmp_inode, pUnixFindData, &object_type);
	rc = filldir(direntry, pUnixFindData->FileName, pqstring->len,
		file->f_pos, tmp_inode->i_ino, object_type);
	if(rc) {
		/* due to readdir error we need to recalculate resume 
			key so next readdir will restart on right entry */
		cFYI(1,("Error %d on filldir of %s",rc ,pUnixFindData->FileName));
	}
	dput(tmp_dentry);
	return rc;
}

int
cifs_readdir(struct file *file, void *direntry, filldir_t filldir)
{
	int rc = 0;
	int xid;
	int Unicode = FALSE;
	int UnixSearch = FALSE;
	unsigned int bufsize, i;
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
	FILE_DIRECTORY_INFO *lastFindData;
	FILE_UNIX_INFO *pfindDataUnix;

	xid = GetXid();

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;
	bufsize = pTcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE;
	if(bufsize > CIFS_MAX_MSGSIZE) {
		FreeXid(xid);
		return -EIO;
	}
	data = kmalloc(bufsize, GFP_KERNEL);
	pfindData = (FILE_DIRECTORY_INFO *) data;

	full_path = build_wildcard_path_from_dentry(file->f_dentry);

	cFYI(1, ("Full path: %s start at: %lld ", full_path, file->f_pos));

	switch ((int) file->f_pos) {
	case 0:
		if (filldir(direntry, ".", 1, file->f_pos,
		     file->f_dentry->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for current dir failed "));
			break;
		}
		file->f_pos++;
		/* fallthrough */
	case 1:
		if (filldir(direntry, "..", 2, file->f_pos,
		     file->f_dentry->d_parent->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for parent dir failed "));
			break;
		}
		file->f_pos++;
		/* fallthrough */
	case 2:
		if (file->private_data != NULL) {
			cifsFile =
				(struct cifsFileInfo *) file->private_data;
			if (cifsFile->endOfSearch) {
				if(cifsFile->emptyDir) {
					cFYI(1, ("End of search, empty dir"));
					rc = 0;
					break;
				}
			} else {
				cifsFile->invalidHandle = TRUE;
				CIFSFindClose(xid, pTcon, cifsFile->netfid);
			}
			if(cifsFile->search_resume_name) {
				kfree(cifsFile->search_resume_name);
				cifsFile->search_resume_name = NULL;
			}
		}
		rc = CIFSFindFirst(xid, pTcon, full_path, pfindData,
				&findParms, cifs_sb->local_nls,
				&Unicode, &UnixSearch);
		cFYI(1, ("Count: %d  End: %d ", findParms.SearchCount,
			findParms.EndofSearch));
 
		if (rc == 0) {
			searchHandle = findParms.SearchHandle;
			if(file->private_data == NULL)
				file->private_data =
					kmalloc(sizeof(struct cifsFileInfo),GFP_KERNEL);
			if (file->private_data) {
				memset(file->private_data, 0,
				       sizeof (struct cifsFileInfo));
				cifsFile =
				    (struct cifsFileInfo *) file->private_data;
				cifsFile->netfid = searchHandle;
				cifsFile->invalidHandle = FALSE;
				init_MUTEX(&cifsFile->fh_sem);
			} else {
				rc = -ENOMEM;
				break;
			}

			renew_parental_timestamps(file->f_dentry);
			lastFindData = 
				(FILE_DIRECTORY_INFO *) ((char *) pfindData + 
					findParms.LastNameOffset);
			if((char *)lastFindData > (char *)pfindData + bufsize) {
				cFYI(1,("last search entry past end of packet"));
				rc = -EIO;
				break;
			}
			/* Offset of resume key same for levels 257 and 514 */
			cifsFile->resume_key = lastFindData->FileIndex;
			if(UnixSearch == FALSE) {
				cifsFile->resume_name_length = 
					le32_to_cpu(lastFindData->FileNameLength);
				if(cifsFile->resume_name_length > bufsize - 64) {
					cFYI(1,("Illegal resume file name length %d",
						cifsFile->resume_name_length));
					rc = -ENOMEM;
					break;
				}
				cifsFile->search_resume_name = 
					kmalloc(cifsFile->resume_name_length, GFP_KERNEL);
				cFYI(1,("Last file: %s with name %d bytes long",
					lastFindData->FileName,
					cifsFile->resume_name_length));
				memcpy(cifsFile->search_resume_name,
					lastFindData->FileName, 
					cifsFile->resume_name_length);
			} else {
				pfindDataUnix = (FILE_UNIX_INFO *)lastFindData;
				if (Unicode == TRUE) {
					for(i=0;(pfindDataUnix->FileName[i] 
						    | pfindDataUnix->FileName[i+1]);
						i+=2) {
						if(i > bufsize-64)
							break;
					}
					cifsFile->resume_name_length = i + 2;
				} else {
					cifsFile->resume_name_length = 
						strnlen(pfindDataUnix->FileName,
							bufsize-63);
				}
				if(cifsFile->resume_name_length > bufsize - 64) {
					cFYI(1,("Illegal resume file name length %d",
						cifsFile->resume_name_length));
					rc = -ENOMEM;
					break;
				}
				cifsFile->search_resume_name = 
					kmalloc(cifsFile->resume_name_length, GFP_KERNEL);
				cFYI(1,("Last file: %s with name %d bytes long",
					pfindDataUnix->FileName,
					cifsFile->resume_name_length));
				memcpy(cifsFile->search_resume_name,
					pfindDataUnix->FileName, 
					cifsFile->resume_name_length);
			}
			for (i = 2; i < (unsigned int)findParms.SearchCount + 2; i++) {
				if (UnixSearch == FALSE) {
					pfindData->FileNameLength =
					  le32_to_cpu(pfindData->FileNameLength);
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
						if(cifs_filldir(&qstring,
							     pfindData,
							     file, filldir,
							     direntry)) {
							/* do not end search if
								kernel not ready to take
								remaining entries yet */
							reset_resume_key(file, pfindData->FileName,qstring.len,
								Unicode, cifs_sb->local_nls);
							findParms.EndofSearch = 0;
							break;
						}
						file->f_pos++;
					}
				} else {	/* UnixSearch */
					pfindDataUnix =
					    (FILE_UNIX_INFO *) pfindData;
					if (Unicode == TRUE)
						qstring.len =
							cifs_strfromUCS_le
							(pfindDataUnix->FileName,
							(wchar_t *)
							pfindDataUnix->FileName,
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
						if(cifs_filldir_unix(&qstring,
								  pfindDataUnix,
								  file,
								  filldir,
								  direntry)) {
							/* do not end search if
								kernel not ready to take
								remaining entries yet */
							findParms.EndofSearch = 0;
							reset_resume_key(file, pfindDataUnix->FileName,
								qstring.len,Unicode,cifs_sb->local_nls);
							break;
						}
						file->f_pos++;
					}
				}
				/* works also for Unix ff struct since first field of both */
				pfindData = 
					(FILE_DIRECTORY_INFO *) ((char *) pfindData
						 + le32_to_cpu(pfindData->NextEntryOffset));
				/* BB also should check to make sure that pointer is not beyond the end of the SMB */
				/* if(pfindData > lastFindData) rc = -EIO; break; */
			}	/* end for loop */
			if ((findParms.EndofSearch != 0) && cifsFile) {
				cifsFile->endOfSearch = TRUE;
				if(findParms.SearchCount == 2)
					cifsFile->emptyDir = TRUE;
			}
		} else {
			if (cifsFile)
				cifsFile->endOfSearch = TRUE;
			/* unless parent directory gone do not return error */
			rc = 0;
		}
		break;
	default:
		if (file->private_data == NULL) {
			rc = -EBADF;
			cFYI(1,
			     ("Readdir on closed srch, pos = %lld",
			      file->f_pos));
		} else {
			cifsFile = (struct cifsFileInfo *) file->private_data;
			if (cifsFile->endOfSearch) {
				rc = 0;
				cFYI(1, ("End of search "));
				break;
			}
			searchHandle = cifsFile->netfid;
			rc = CIFSFindNext(xid, pTcon, pfindData,
				&findNextParms, searchHandle, 
				cifsFile->search_resume_name,
				cifsFile->resume_name_length,
				cifsFile->resume_key,
				&Unicode, &UnixSearch);
			cFYI(1,("Count: %d  End: %d ",
			      findNextParms.SearchCount,
			      findNextParms.EndofSearch));
			if ((rc == 0) && (findNextParms.SearchCount != 0)) {
			/* BB save off resume key, key name and name length  */
				lastFindData = 
					(FILE_DIRECTORY_INFO *) ((char *) pfindData 
						+ findNextParms.LastNameOffset);
				if((char *)lastFindData > (char *)pfindData + bufsize) {
					cFYI(1,("last search entry past end of packet"));
					rc = -EIO;
					break;
				}
				/* Offset of resume key same for levels 257 and 514 */
				cifsFile->resume_key = lastFindData->FileIndex;

				if(UnixSearch == FALSE) {
					cifsFile->resume_name_length = 
						le32_to_cpu(lastFindData->FileNameLength);
					if(cifsFile->resume_name_length > bufsize - 64) {
						cFYI(1,("Illegal resume file name length %d",
							cifsFile->resume_name_length));
						rc = -ENOMEM;
						break;
					}
					/* Free the memory allocated by previous findfirst 
					or findnext call - we can not reuse the memory since
					the resume name may not be same string length */
					if(cifsFile->search_resume_name)
						kfree(cifsFile->search_resume_name);
					cifsFile->search_resume_name = 
						kmalloc(cifsFile->resume_name_length, GFP_KERNEL);
					cFYI(1,("Last file: %s with name %d bytes long",
						lastFindData->FileName,
						cifsFile->resume_name_length));
					memcpy(cifsFile->search_resume_name,
						lastFindData->FileName, 
						cifsFile->resume_name_length);
				} else {
					pfindDataUnix = (FILE_UNIX_INFO *)lastFindData;
					if (Unicode == TRUE) {
						for(i=0;(pfindDataUnix->FileName[i] 
								| pfindDataUnix->FileName[i+1]);
							i+=2) {
							if(i > bufsize-64)
								break;
						}
						cifsFile->resume_name_length = i + 2;
					} else {
						cifsFile->resume_name_length = 
							strnlen(pfindDataUnix->
							 FileName,
							 MAX_PATHCONF);
					}
					if(cifsFile->resume_name_length > bufsize - 64) {
						cFYI(1,("Illegal resume file name length %d",
								cifsFile->resume_name_length));
						rc = -ENOMEM;
						break;
					}
					/* Free the memory allocated by previous findfirst 
					or findnext call - we can not reuse the memory since
					the resume name may not be same string length */
					if(cifsFile->search_resume_name)
						kfree(cifsFile->search_resume_name);
					cifsFile->search_resume_name = 
						kmalloc(cifsFile->resume_name_length, GFP_KERNEL);
					cFYI(1,("fnext last file: %s with name %d bytes long",
						lastFindData->FileName,
						cifsFile->resume_name_length));
					memcpy(cifsFile->search_resume_name,
						lastFindData->FileName, 
						cifsFile->resume_name_length);
				}

				for (i = 0; i < findNextParms.SearchCount; i++) {
					pfindData->FileNameLength =
					    le32_to_cpu(pfindData->
							FileNameLength);
					if (UnixSearch == FALSE) {
						if (Unicode == TRUE)
							pfindData->FileNameLength =
							  cifs_strfromUCS_le
							  (pfindData->FileName,
							  (wchar_t *)
							  pfindData->FileName,
							  (pfindData->FileNameLength)/ 2,
							  cifs_sb->local_nls);
						qstring.len = 
							pfindData->FileNameLength;
						if (((qstring.len != 1)
						    || (pfindData->FileName[0] != '.'))
						    && ((qstring.len != 2)
							|| (pfindData->FileName[0] != '.')
							|| (pfindData->FileName[1] !=
							    '.'))) {
							if(cifs_filldir
							    (&qstring,
							     pfindData,
							     file, filldir,
							     direntry)) {
							/* do not end search if
								kernel not ready to take
								remaining entries yet */
								findNextParms.EndofSearch = 0;
								reset_resume_key(file, pfindData->FileName,qstring.len,
									Unicode,cifs_sb->local_nls);
								break;
							}
							file->f_pos++;
						}
					} else {	/* UnixSearch */
						pfindDataUnix =
						    (FILE_UNIX_INFO *)
						    pfindData;
						if (Unicode == TRUE)
							qstring.len =
							  cifs_strfromUCS_le
							  (pfindDataUnix->FileName,
							  (wchar_t *)
							  pfindDataUnix->FileName,
							  MAX_PATHCONF,
							  cifs_sb->local_nls);
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
							if(cifs_filldir_unix
							    (&qstring,
							     pfindDataUnix,
							     file, filldir,
							     direntry)) {
								/* do not end search if
								kernel not ready to take
								remaining entries yet */
								findNextParms.EndofSearch = 0;
								reset_resume_key(file, pfindDataUnix->FileName,qstring.len,
									Unicode,cifs_sb->local_nls);
								break;
							}
							file->f_pos++;
						}
					}
					pfindData = (FILE_DIRECTORY_INFO *) ((char *) pfindData + le32_to_cpu(pfindData->NextEntryOffset));	/* works also for Unix find struct since this is the first field of both */
					/* BB also should check to make sure that pointer is not beyond the end of the SMB */
				} /* end for loop */
				if (findNextParms.EndofSearch != 0) {
					cifsFile->endOfSearch = TRUE;
				}
			} else {
				cifsFile->endOfSearch = TRUE;
				rc = 0;	/* unless parent directory disappeared - do not return error here (eg Access Denied or no more files) */
			}
		}
	} /* end switch */
	if (data)
		kfree(data);
	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}
int cifs_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	cFYI(1,("prepare write for page %p from %d to %d",page,from,to));
	if (!PageUptodate(page)) {
		if (to - from != PAGE_CACHE_SIZE) {
			void *kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr, 0, from);
			memset(kaddr + to, 0, PAGE_CACHE_SIZE - to);
			flush_dcache_page(page);
			kunmap_atomic(kaddr, KM_USER0);
		}
		SetPageUptodate(page);
	}
	return 0;
}


struct address_space_operations cifs_addr_ops = {
	.readpage = cifs_readpage,
	.readpages = cifs_readpages,
	.writepage = cifs_writepage,
	.prepare_write = simple_prepare_write, /* BB fixme BB */
/*	.prepare_write = cifs_prepare_write, */  /* BB removeme BB */
	.commit_write = cifs_commit_write,
   /* .sync_page = cifs_sync_page, */
	/*.direct_IO = */
};
