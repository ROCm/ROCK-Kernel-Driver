/*
 *   fs/cifs/inode.c
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
#include <linux/buffer_head.h>
#include <linux/stat.h>
#include <linux/pagemap.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

int
cifs_get_inode_info_unix(struct inode **pinode,
			 const unsigned char *search_path,
			 struct super_block *sb)
{
	int xid;
	int rc = 0;
	FILE_UNIX_BASIC_INFO findData;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *tmp_path;

/* BB add caching check so we do not go to server to overwrite inode info to cached file
	where the local file sizes are correct and the server info is stale  BB */

	xid = GetXid();

	pTcon = cifs_sb->tcon;
	cFYI(1, (" Getting info on %s ", search_path));
	/* we could have done a find first instead but this returns more info */
	rc = CIFSSMBUnixQPathInfo(xid, pTcon, search_path, &findData,
				  cifs_sb->local_nls);
	/* dump_mem("\nUnixQPathInfo return data", &findData, sizeof(findData)); */
	if (rc) {
		if (rc == -EREMOTE) {
			tmp_path =
			    kmalloc(strnlen
				    (pTcon->treeName,
				     MAX_TREE_SIZE + 1) +
				    strnlen(search_path, MAX_PATHCONF) + 1,
				    GFP_KERNEL);
			if (tmp_path == NULL) {
				FreeXid(xid);
				return -ENOMEM;
			}
        /* have to skip first of the double backslash of UNC name */
			strncpy(tmp_path, pTcon->treeName, MAX_TREE_SIZE);	
			strncat(tmp_path, search_path, MAX_PATHCONF);
			rc = connect_to_dfs_path(xid, pTcon->ses,
						 /* treename + */ tmp_path,
						 cifs_sb->local_nls);
			kfree(tmp_path);

			/* BB fix up inode etc. */
		} else if (rc) {
			FreeXid(xid);
			return rc;
		}

	} else {
		struct cifsInodeInfo *cifsInfo;

		/* get new inode */
		if (*pinode == NULL) {
			*pinode = new_inode(sb);
		}
		inode = *pinode;

		cifsInfo = CIFS_I(inode);

		cFYI(1, (" Old time %ld ", cifsInfo->time));
		cifsInfo->time = jiffies;
		cFYI(1, (" New time %ld ", cifsInfo->time));
		atomic_inc(&cifsInfo->inUse);	/* inc on every refresh of inode */

		inode->i_atime =
		    cifs_NTtimeToUnix(le64_to_cpu(findData.LastAccessTime));
		inode->i_mtime =
		    cifs_NTtimeToUnix(le64_to_cpu
				(findData.LastModificationTime));
		inode->i_ctime =
		    cifs_NTtimeToUnix(le64_to_cpu(findData.LastStatusChange));
		inode->i_mode = le64_to_cpu(findData.Permissions);
		findData.Type = le32_to_cpu(findData.Type);
		if (findData.Type == UNIX_FILE) {
			inode->i_mode |= S_IFREG;
		} else if (findData.Type == UNIX_SYMLINK) {
			inode->i_mode |= S_IFLNK;
		} else if (findData.Type == UNIX_DIR) {
			inode->i_mode |= S_IFDIR;
		} else if (findData.Type == UNIX_CHARDEV) {
			inode->i_mode |= S_IFCHR;
		} else if (findData.Type == UNIX_BLOCKDEV) {
			inode->i_mode |= S_IFBLK;
		} else if (findData.Type == UNIX_FIFO) {
			inode->i_mode |= S_IFIFO;
		} else if (findData.Type == UNIX_SOCKET) {
			inode->i_mode |= S_IFSOCK;
		}
		inode->i_uid = le64_to_cpu(findData.Uid);
		inode->i_gid = le64_to_cpu(findData.Gid);
		inode->i_nlink = le64_to_cpu(findData.Nlinks);
		findData.NumOfBytes = le64_to_cpu(findData.NumOfBytes);
		findData.EndOfFile = le64_to_cpu(findData.EndOfFile);
		inode->i_size = findData.EndOfFile;
/* blksize needs to be multiple of two. So safer to default to blksize
	and blkbits set in superblock so 2**blkbits and blksize will match */
/*		inode->i_blksize =
		    (pTcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFE00;*/
		inode->i_blocks = 
	                (inode->i_blksize - 1 + findData.NumOfBytes) >> inode->i_blkbits;

		if (findData.NumOfBytes < findData.EndOfFile)
			cFYI(1, ("Server inconsistency Error: it says allocation size less than end of file "));
		cFYI(1,
		     ("Size %ld and blocks %ld ",
		      (unsigned long) inode->i_size, inode->i_blocks));
		if (S_ISREG(inode->i_mode)) {
			cFYI(1, (" File inode "));
			inode->i_op = &cifs_file_inode_ops;
			inode->i_fop = &cifs_file_ops;
			inode->i_data.a_ops = &cifs_addr_ops;
		} else if (S_ISDIR(inode->i_mode)) {
			cFYI(1, (" Directory inode"));
			inode->i_op = &cifs_dir_inode_ops;
			inode->i_fop = &cifs_dir_ops;
		} else if (S_ISLNK(inode->i_mode)) {
			cFYI(1, (" Symbolic Link inode "));
			inode->i_op = &cifs_symlink_inode_ops;
/* tmp_inode->i_fop = *//* do not need to set to anything */
		} else {
			cFYI(1, (" Init special inode "));
			init_special_inode(inode, inode->i_mode,
					   inode->i_rdev);
		}
	}
	FreeXid(xid);
	return rc;
}

int
cifs_get_inode_info(struct inode **pinode, const unsigned char *search_path, 
		FILE_ALL_INFO * pfindData, struct super_block *sb)
{
	int xid;
	int rc = 0;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *tmp_path;
	char *buf = NULL;

	xid = GetXid();

	pTcon = cifs_sb->tcon;
	cFYI(1,("Getting info on %s ", search_path));

	if((pfindData == NULL) && (*pinode != NULL)) {
		if(CIFS_I(*pinode)->clientCanCacheRead) {
			cFYI(1,("No need to revalidate inode sizes on cached file "));
			FreeXid(xid);
			return rc;
		}
	}

	/* if file info not passed in then get it from server */
	if(pfindData == NULL) {
		buf = kmalloc(sizeof(FILE_ALL_INFO),GFP_KERNEL);
		pfindData = (FILE_ALL_INFO *)buf;
	/* could do find first instead but this returns more info */
		rc = CIFSSMBQPathInfo(xid, pTcon, search_path, pfindData,
			      cifs_sb->local_nls);
	}
	/* dump_mem("\nQPathInfo return data",&findData, sizeof(findData)); */
	if (rc) {
		if (rc == -EREMOTE) {
			tmp_path =
			    kmalloc(strnlen
				    (pTcon->treeName,
				     MAX_TREE_SIZE + 1) +
				    strnlen(search_path, MAX_PATHCONF) + 1,
				    GFP_KERNEL);
			if (tmp_path == NULL) {
			    if(buf)
				kfree(buf);
			    FreeXid(xid);
			    return -ENOMEM;
			}

			strncpy(tmp_path, pTcon->treeName, MAX_TREE_SIZE);
			strncat(tmp_path, search_path, MAX_PATHCONF);
			rc = connect_to_dfs_path(xid, pTcon->ses,
						 /* treename + */ tmp_path,
						 cifs_sb->local_nls);
			kfree(tmp_path);
			/* BB fix up inode etc. */
		} else if (rc) {
		    if(buf)
			kfree(buf);
		    FreeXid(xid);
		    return rc;
		}
	} else {
		struct cifsInodeInfo *cifsInfo;

		/* get new inode */
		if (*pinode == NULL) {
			*pinode = new_inode(sb);
		}

		inode = *pinode;
		cifsInfo = CIFS_I(inode);
		pfindData->Attributes = le32_to_cpu(pfindData->Attributes);
		cifsInfo->cifsAttrs = pfindData->Attributes;
		cFYI(1, (" Old time %ld ", cifsInfo->time));
		cifsInfo->time = jiffies;
		cFYI(1, (" New time %ld ", cifsInfo->time));
		atomic_inc(&cifsInfo->inUse);	/* inc on every refresh of inode */

/* blksize needs to be multiple of two. So safer to default to blksize
        and blkbits set in superblock so 2**blkbits and blksize will match */
/*		inode->i_blksize =
		    (pTcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFE00;*/

		/* Linux can not store file creation time unfortunately so we ignore it */
		inode->i_atime =
		    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastAccessTime));
		inode->i_mtime =
		    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastWriteTime));
		inode->i_ctime =
		    cifs_NTtimeToUnix(le64_to_cpu(pfindData->ChangeTime));
		cFYI(0,
		     (" Attributes came in as 0x%x ", pfindData->Attributes));

		/* set default mode. will override for dirs below */
		inode->i_mode = cifs_sb->mnt_file_mode;

		if (pfindData->Attributes & ATTR_REPARSE) {
	/* Can IFLNK be set as it basically is on windows with IFREG or IFDIR? */
			inode->i_mode |= S_IFLNK;
		} else if (pfindData->Attributes & ATTR_DIRECTORY) {
	/* override default perms since we do not do byte range locking on dirs */
			inode->i_mode = cifs_sb->mnt_dir_mode;
			inode->i_mode |= S_IFDIR;
		} else {
			inode->i_mode |= S_IFREG;
			/* treat the dos attribute of read-only as read-only mode e.g. 555 */
			if(cifsInfo->cifsAttrs & ATTR_READONLY)
				inode->i_mode &= ~(S_IWUGO);
   /* BB add code here - validate if device or weird share or device type? */
		}
		inode->i_size = le64_to_cpu(pfindData->EndOfFile);
		pfindData->AllocationSize = le64_to_cpu(pfindData->AllocationSize);
		inode->i_blocks =
	                (inode->i_blksize - 1 + pfindData->AllocationSize) >> inode->i_blkbits;

		inode->i_nlink = le32_to_cpu(pfindData->NumberOfLinks);

		/* BB fill in uid and gid here? with help from winbind? 
			or retrieve from NTFS stream extended attribute */
		inode->i_uid = cifs_sb->mnt_uid;
		inode->i_gid = cifs_sb->mnt_gid;
		
		if (S_ISREG(inode->i_mode)) {
			cFYI(1, (" File inode "));
			inode->i_op = &cifs_file_inode_ops;
			inode->i_fop = &cifs_file_ops;
			inode->i_data.a_ops = &cifs_addr_ops;
		} else if (S_ISDIR(inode->i_mode)) {
			cFYI(1, (" Directory inode "));
			inode->i_op = &cifs_dir_inode_ops;
			inode->i_fop = &cifs_dir_ops;
		} else if (S_ISLNK(inode->i_mode)) {
			cFYI(1, (" Symbolic Link inode "));
			inode->i_op = &cifs_symlink_inode_ops;
		} else {
			init_special_inode(inode, inode->i_mode,
					   inode->i_rdev);
		}
	}
	if(buf)
	    kfree(buf);
	FreeXid(xid);
	return rc;
}

void
cifs_read_inode(struct inode *inode)
{				/* gets root inode */

	struct cifs_sb_info *cifs_sb;

	cifs_sb = CIFS_SB(inode->i_sb);

	if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)
		cifs_get_inode_info_unix(&inode, "", inode->i_sb);
	else
		cifs_get_inode_info(&inode, "", NULL, inode->i_sb);
}

int
cifs_unlink(struct inode *inode, struct dentry *direntry)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct cifsInodeInfo *cifsInode;
	FILE_BASIC_INFO * pinfo_buf;

	cFYI(1, (" cifs_unlink, inode = 0x%p with ", inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);

	rc = CIFSSMBDelFile(xid, pTcon, full_path, cifs_sb->local_nls);

	if (!rc) {
		direntry->d_inode->i_nlink--;
	} else if (rc == -ENOENT) {
		d_drop(direntry);
	} else if (rc == -ETXTBSY) {
		int oplock = FALSE;
		__u16 netfid;

		rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN, DELETE, 
				CREATE_NOT_DIR | CREATE_DELETE_ON_CLOSE,
				&netfid, &oplock, NULL, cifs_sb->local_nls);
		if(rc==0) {
			CIFSSMBRenameOpenFile(xid,pTcon,netfid,
				NULL, cifs_sb->local_nls);
			CIFSSMBClose(xid, pTcon, netfid);
			direntry->d_inode->i_nlink--;
		}
	} else if (rc == -EACCES) {
		/* try only if r/o attribute set in local lookup data? */
		pinfo_buf = (FILE_BASIC_INFO *)kmalloc(sizeof(FILE_BASIC_INFO),GFP_KERNEL);
		if(pinfo_buf) {
			memset(pinfo_buf,0,sizeof(FILE_BASIC_INFO));        
		/* ATTRS set to normal clears r/o bit */
			pinfo_buf->Attributes = cpu_to_le32(ATTR_NORMAL);
			rc = CIFSSMBSetTimes(xid, pTcon, full_path, pinfo_buf,
				cifs_sb->local_nls);
			kfree(pinfo_buf);
		}
		if(rc==0) {
			rc = CIFSSMBDelFile(xid, pTcon, full_path, cifs_sb->local_nls);
			if (!rc) {
				direntry->d_inode->i_nlink--;
			} else if (rc == -ETXTBSY) {
				int oplock = FALSE;
				__u16 netfid;

				rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN, DELETE,
                                	CREATE_NOT_DIR | CREATE_DELETE_ON_CLOSE,
	                                &netfid, &oplock, NULL, cifs_sb->local_nls);
				if(rc==0) {
					CIFSSMBRenameOpenFile(xid,pTcon,netfid,NULL,cifs_sb->local_nls);
					CIFSSMBClose(xid, pTcon, netfid);
		                        direntry->d_inode->i_nlink--;
				}
			/* BB if rc = -ETXTBUSY goto the rename logic BB */
			}
		}
	}
	cifsInode = CIFS_I(direntry->d_inode);
	cifsInode->time = 0;	/* will force revalidate to get info when needed */
	direntry->d_inode->i_ctime = inode->i_ctime = inode->i_mtime =
	    CURRENT_TIME;
	cifsInode = CIFS_I(inode);
	cifsInode->time = 0;	/* force revalidate of dir as well */

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_mkdir(struct inode *inode, struct dentry *direntry, int mode)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;

	cFYI(1, ("In cifs_mkdir, mode = 0x%x inode = 0x%p ", mode, inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	/* BB add setting the equivalent of mode via CreateX w/ACLs */
	rc = CIFSSMBMkDir(xid, pTcon, full_path, cifs_sb->local_nls);
	if (rc) {
		cFYI(1, ("cifs_mkdir returned 0x%x ", rc));
	} else {
		inode->i_nlink++;
		if (pTcon->ses->capabilities & CAP_UNIX)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb);
		else
			rc = cifs_get_inode_info(&newinode, full_path,NULL,
						 inode->i_sb);

		direntry->d_op = &cifs_dentry_ops;
		d_instantiate(direntry, newinode);
		if(direntry->d_inode)
			direntry->d_inode->i_nlink = 2;
		if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)                
			CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode,
				(__u64)-1,  
				(__u64)-1,
				0 /* dev_t */,
				cifs_sb->local_nls);
		else { /* BB to be implemented via Windows secrty descriptors*/
		/* eg CIFSSMBWinSetPerms(xid,pTcon,full_path,mode,-1,-1,local_nls);*/
		}
	}
	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}

int
cifs_rmdir(struct inode *inode, struct dentry *direntry)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct cifsInodeInfo *cifsInode;

	cFYI(1, (" cifs_rmdir, inode = 0x%p with ", inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);

	rc = CIFSSMBRmDir(xid, pTcon, full_path, cifs_sb->local_nls);

	if (!rc) {
		inode->i_nlink--;
		direntry->d_inode->i_size = 0;
		direntry->d_inode->i_nlink = 0;
	}

	cifsInode = CIFS_I(direntry->d_inode);
	cifsInode->time = 0;	/* force revalidate to go get info when needed */
	direntry->d_inode->i_ctime = inode->i_ctime = inode->i_mtime =
	    CURRENT_TIME;

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_rename(struct inode *source_inode, struct dentry *source_direntry,
	    struct inode *target_inode, struct dentry *target_direntry)
{
	char *fromName;
	char *toName;
	struct cifs_sb_info *cifs_sb_source;
	struct cifs_sb_info *cifs_sb_target;
	struct cifsTconInfo *pTcon;
	int xid;
	int rc = 0;

	xid = GetXid();

	cifs_sb_target = CIFS_SB(target_inode->i_sb);
	cifs_sb_source = CIFS_SB(source_inode->i_sb);
	pTcon = cifs_sb_source->tcon;

	if (pTcon != cifs_sb_target->tcon) {
		FreeXid(xid);    
		return -EXDEV;	/* BB actually could be allowed if same server, but
                     different share. Might eventually add support for this */
	}

	fromName = build_path_from_dentry(source_direntry);
	toName = build_path_from_dentry(target_direntry);

	rc = CIFSSMBRename(xid, pTcon, fromName, toName,
			   cifs_sb_source->local_nls);
	if(rc == -EEXIST) {
		cifs_unlink(target_inode, target_direntry);
		rc = CIFSSMBRename(xid, pTcon, fromName, toName,
				   cifs_sb_source->local_nls);
	}

	if((rc == -EIO)||(rc == -EEXIST)) {
                int oplock = FALSE;
                __u16 netfid;

                rc = CIFSSMBOpen(xid, pTcon, fromName, FILE_OPEN, GENERIC_READ,
                                CREATE_NOT_DIR,
                                &netfid, &oplock, NULL, cifs_sb_source->local_nls);
                if(rc==0) {
                        CIFSSMBRenameOpenFile(xid,pTcon,netfid,
                                toName, cifs_sb_source->local_nls);
                        CIFSSMBClose(xid, pTcon, netfid);
                }
	}
	if (fromName)
		kfree(fromName);
	if (toName)
		kfree(toName);

	FreeXid(xid);
	return rc;
}

int
cifs_revalidate(struct dentry *direntry)
{
	int xid;
	int rc = 0;
	char *full_path;
	struct cifs_sb_info *cifs_sb;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cifs_sb = CIFS_SB(direntry->d_sb);

	full_path = build_path_from_dentry(direntry);
	cFYI(1,
	     ("Revalidate full path: %s for inode 0x%p with count %d dentry: 0x%p d_time %ld at time %ld ",
	      full_path, direntry->d_inode,
	      direntry->d_inode->i_count.counter, direntry,
	      direntry->d_time, jiffies));


	cifsInode = CIFS_I(direntry->d_inode);
	/* BB add check - do not need to revalidate oplocked files */

	if (time_before(jiffies, cifsInode->time + HZ) && lookupCacheEnabled) {
	    if((S_ISREG(direntry->d_inode->i_mode) == 0) || 
			(direntry->d_inode->i_nlink == 1)) {  
			if (full_path)
				kfree(full_path);
			FreeXid(xid);
			return rc;
		} else {
			cFYI(1,("Have to revalidate file due to hardlinks"));
		}            
	}

	if (cifs_sb->tcon->ses->capabilities & CAP_UNIX) {
		rc = cifs_get_inode_info_unix(&direntry->d_inode, full_path,
					 direntry->d_sb);
		if(rc) {
			cFYI(1,("error on getting revalidate info %d",rc));
/*			if(rc != -ENOENT)
				rc = 0; */ /* BB should we cache info on certain errors? */
		}
	} else {
		rc = cifs_get_inode_info(&direntry->d_inode, full_path, NULL,
				    direntry->d_sb);
		if(rc) {
			cFYI(1,("error on getting revalidate info %d",rc));
/*			if(rc != -ENOENT)
				rc = 0; */  /* BB should we cache info on certain errors? */
		}
	}
	/* should we remap certain errors, access denied?, to zero */

	/* BB if not oplocked, invalidate inode pages if mtime has changed */

	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}

int cifs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	int err = cifs_revalidate(dentry);
	if (!err)
		generic_fillattr(dentry->d_inode, stat);
	return err;
}

void
cifs_truncate_file(struct inode *inode)
{				/* BB remove - may not need this function after all BB */
	int xid;
	int rc = 0;
	struct cifsFileInfo *open_file = NULL;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsInodeInfo *cifsInode;
	struct dentry *dirent;
	char *full_path = NULL;   

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	if (list_empty(&inode->i_dentry)) {
		cERROR(1,
		       ("Can not get pathname from empty dentry in inode 0x%p ",
			inode));
		FreeXid(xid);
		return;
	}
	dirent = list_entry(inode->i_dentry.next, struct dentry, d_alias);
	if (dirent) {
		full_path = build_path_from_dentry(dirent);
		rc = CIFSSMBSetEOF(xid, pTcon, full_path, inode->i_size,FALSE,
				   cifs_sb->local_nls);
		cFYI(1,(" SetEOF (truncate) rc = %d",rc));
		if(rc == -ETXTBSY) {        
			cifsInode = CIFS_I(inode);
			if(!list_empty(&(cifsInode->openFileList))) {            
				open_file = list_entry(cifsInode->openFileList.next,
					struct cifsFileInfo, flist);           
            /* We could check if file is open for writing first */
				 rc = CIFSSMBSetFileSize(xid, pTcon, inode->i_size,
					open_file->netfid,open_file->pid,FALSE);
			} else {
				  cFYI(1,(" No open files to get file handle from"));
			}
		}
		if (!rc)
			CIFSSMBSetEOF(xid,pTcon,full_path,inode->i_size,TRUE,cifs_sb->local_nls);
           /* allocation size setting seems optional so ignore return code */
	}
	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return;
}

static int cifs_trunc_page(struct address_space *mapping, loff_t from)
{
        pgoff_t index = from >> PAGE_CACHE_SHIFT;
        unsigned offset = from & (PAGE_CACHE_SIZE-1);
        struct page *page;
        char *kaddr;
        int rc = 0;

        page = grab_cache_page(mapping, index);
        if (!page)
                return -ENOMEM;

        kaddr = kmap_atomic(page, KM_USER0);
        memset(kaddr + offset, 0, PAGE_CACHE_SIZE - offset);
        flush_dcache_page(page);
        kunmap_atomic(kaddr, KM_USER0);
        set_page_dirty(page);
        unlock_page(page);
        page_cache_release(page);
        return rc;
}

int
cifs_setattr(struct dentry *direntry, struct iattr *attrs)
{
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	int rc = -EACCES;
	struct cifsFileInfo *open_file = NULL;
	FILE_BASIC_INFO time_buf;
	int set_time = FALSE;
	__u64 mode = 0xFFFFFFFFFFFFFFFFULL;
	__u64 uid = 0xFFFFFFFFFFFFFFFFULL;
	__u64 gid = 0xFFFFFFFFFFFFFFFFULL;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cFYI(1,
	     (" In cifs_setattr, name = %s attrs->iavalid 0x%x ",
	      direntry->d_name.name, attrs->ia_valid));
	cifs_sb = CIFS_SB(direntry->d_inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	cifsInode = CIFS_I(direntry->d_inode);

	/* BB check if we need to refresh inode from server now ? BB */

	cFYI(1, (" Changing attributes 0x%x", attrs->ia_valid));

	if (attrs->ia_valid & ATTR_SIZE) {
		rc = CIFSSMBSetEOF(xid, pTcon, full_path, attrs->ia_size,FALSE,
				   cifs_sb->local_nls);
		cFYI(1,(" SetEOF (setattrs) rc = %d",rc));

		if(rc == -ETXTBSY) {
			if(!list_empty(&(cifsInode->openFileList))) {            
				open_file = list_entry(cifsInode->openFileList.next, 
					   struct cifsFileInfo, flist);           
    /* We could check if file is open for writing first */
				rc = CIFSSMBSetFileSize(xid, pTcon, attrs->ia_size,
					   open_file->netfid,open_file->pid,FALSE);           
			} else {
				cFYI(1,(" No open files to get file handle from"));
			}
		}
        /*  For Allocation Size - do not need to call the following
            it did not hurt if it fails but why bother */
	/*	CIFSSMBSetEOF(xid, pTcon, full_path, attrs->ia_size, TRUE, cifs_sb->local_nls);*/
		if (rc == 0) {
			rc = vmtruncate(direntry->d_inode, attrs->ia_size);
			cifs_trunc_page(direntry->d_inode->i_mapping, direntry->d_inode->i_size); 

/*          cFYI(1,("truncate_page to 0x%lx \n",direntry->d_inode->i_size)); */
		}
	}
	if (attrs->ia_valid & ATTR_UID) {
		cFYI(1, (" CIFS - UID changed to %d", attrs->ia_uid));
		uid = attrs->ia_uid;
		/*        entry->uid = cpu_to_le16(attr->ia_uid); */
	}
	if (attrs->ia_valid & ATTR_GID) {
		cFYI(1, (" CIFS - GID changed to %d", attrs->ia_gid));
		gid = attrs->ia_gid;
		/*      entry->gid = cpu_to_le16(attr->ia_gid); */
	}

	time_buf.Attributes = 0;
	if (attrs->ia_valid & ATTR_MODE) {
		cFYI(1, (" CIFS - Mode changed to 0x%x", attrs->ia_mode));
		mode = attrs->ia_mode;
		/* entry->mode = cpu_to_le16(attr->ia_mode); */
	}

	if ((cifs_sb->tcon->ses->capabilities & CAP_UNIX)
	    && (attrs->ia_valid & (ATTR_MODE | ATTR_GID | ATTR_UID)))
		rc = CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode, uid, gid,
				0 /* dev_t */, cifs_sb->local_nls);
	else if (attrs->ia_valid & ATTR_MODE) {
		if((mode & S_IWUGO) == 0) /* not writeable */ {
			if((cifsInode->cifsAttrs & ATTR_READONLY) == 0)
				time_buf.Attributes = 
					cpu_to_le32(cifsInode->cifsAttrs | ATTR_READONLY);
		} else if((mode & S_IWUGO) == S_IWUGO) {
			if(cifsInode->cifsAttrs & ATTR_READONLY)
				time_buf.Attributes = 
					cpu_to_le32(cifsInode->cifsAttrs & (~ATTR_READONLY));
		}
		/* BB to be implemented - via Windows security descriptors or streams */
		/* CIFSSMBWinSetPerms(xid,pTcon,full_path,mode,uid,gid,cifs_sb->local_nls);*/
	}

	if (attrs->ia_valid & ATTR_ATIME) {
		set_time = TRUE;
		time_buf.LastAccessTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_atime));
	} else
		time_buf.LastAccessTime = 0;

	if (attrs->ia_valid & ATTR_MTIME) {
		set_time = TRUE;
		time_buf.LastWriteTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_mtime));
	} else
		time_buf.LastWriteTime = 0;

	if (attrs->ia_valid & ATTR_CTIME) {
		set_time = TRUE;
		cFYI(1, (" CIFS - CTIME changed ")); /* BB probably do not need */
		time_buf.ChangeTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_ctime));
	} else
		time_buf.ChangeTime = 0;

	if (set_time | time_buf.Attributes) {
		/* BB what if setting one attribute fails  
			(such as size) but time setting works */
		time_buf.CreationTime = 0;	/* do not change */
		rc = CIFSSMBSetTimes(xid, pTcon, full_path, &time_buf,
				cifs_sb->local_nls);
	}

	/* do not  need local check to inode_check_ok since the server does that */
	inode_setattr(direntry->d_inode, attrs);
	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

void
cifs_delete_inode(struct inode *inode)
{
	/* Note: called without the big kernel filelock - remember spinlocks! */
	cFYI(1, ("In cifs_delete_inode, inode = 0x%p ", inode));
	/* may have to add back in when safe distributed caching of
             directories via e.g. FindNotify added */
}
