/*
 *   fs/cifs/readdir.c
 *
 *   Directory search handling
 * 
 *   Copyright (C) International Business Machines  Corp., 2004
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
#include <linux/smp_lock.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

#ifdef CIFS_EXPERIMENTAL
extern int CIFSFindFirst2(const int xid, struct cifsTconInfo *tcon,
            const char *searchName, const struct nls_table *nls_codepage,
            char ** ppfindData /* first search entries */,
            char ** ppbuf /* beginning of smb response */,
            int  *searchCount, __u16 *searchHandle,
            int *pUnicodeFlag, int *pEndOfSearchFlag, int *level);

static int initiate_cifs_search(const int xid, struct file * file, char * full_path)
{
	int rc = 0;
	struct cifsFileInfo * cifsFile;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char * pfindData;
	char * pbuf;
	int Unicode;
	int EndOfSearch;

	if(file->private_data == NULL) {
		file->private_data = 
			kmalloc(sizeof(struct cifsFileInfo),GFP_KERNEL);

		if(file->private_data == NULL) {
			return -ENOMEM;
		} else {
			memset(file->private_data,0,sizeof(struct cifsFileInfo));
		}
	}
	/* if not end of search do we have to close it first? */
	cifsFile = (struct cifsFileInfo *)file->private_data;
	cifsFile->invalidHandle = TRUE;
     
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	if(cifs_sb == NULL)
		return -EINVAL;

	pTcon = cifs_sb->tcon;
	if(pTcon == NULL)
		return -EINVAL;

	rc = CIFSFindFirst2(xid, pTcon,full_path,cifs_sb->local_nls,
              &pfindData /* first search entries */,
              &pbuf /* beginning of smb response */,
              &cifsFile->entries_in_buffer, &cifsFile->netfid,
              &EndOfSearch,&Unicode,&cifsFile->info_level);
 
	if(EndOfSearch)
		cifsFile->endOfSearch = TRUE;

	return rc;
}

/* find the corresponding entry in the search */
static int find_cifs_entry(loff_t index_to_find, struct cifsFileInfo * cifsFile /* BB add missing parm */) 
{
	int rc = 0;

	return rc;
}

static int cifs_filldir_with_entries(loff_t index_to_find, struct cifsFileInfo * cifsFile)
{
	int rc = 0;

	return rc;
}


int cifs_readdir2(struct file *file, void *direntry, filldir_t filldir)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *cifsFile = NULL;
	char *full_path = NULL;

/*	int Unicode = FALSE;
	int UnixSearch = FALSE;
	unsigned int bufsize, i;
	__u16 searchHandle;
	char *data;
	struct qstr qstring;
	T2_FFIRST_RSP_PARMS findParms;
	T2_FNEXT_RSP_PARMS findNextParms;
	FILE_DIRECTORY_INFO *pfindData;
	FILE_DIRECTORY_INFO *lastFindData;
	FILE_UNIX_INFO *pfindDataUnix;*/

	xid = GetXid();

	if(file->f_dentry == NULL) {
		FreeXid(xid);
		return -EIO;
	}

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;
	if(pTcon == NULL)
		return -EINVAL;

	down(&file->f_dentry->d_sb->s_vfs_rename_sem);
	full_path = build_wildcard_path_from_dentry(file->f_dentry);
	up(&file->f_dentry->d_sb->s_vfs_rename_sem);

	if(full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}
	cFYI(1, ("Full path: %s start at: %lld ", full_path, file->f_pos));

	switch ((int) file->f_pos) {
	case 0:
		if (filldir(direntry, ".", 1, file->f_pos,
		     file->f_dentry->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for current dir failed "));
			rc = -ENOMEM;
			break;
		}
		file->f_pos++;
		/* fallthrough */
	case 1:
		if (filldir(direntry, "..", 2, file->f_pos,
		     file->f_dentry->d_parent->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for parent dir failed "));
			rc = -ENOMEM;
			break;
		}
		file->f_pos++;
		/* fallthrough */
	default:
		/* 1) If search is active, 
			is in current search buffer? 
			if it before then restart search
			if after then keep searching till find it */

		if(file->private_data == NULL) {
			rc = initiate_cifs_search(xid,file,full_path);
			if(rc || (file->private_data == NULL)) {
				FreeXid(xid);
				if(full_path)
					kfree(full_path);
				return rc;
			}
		}

		cifsFile = (struct cifsFileInfo *) file->private_data;
		if (cifsFile->endOfSearch) {
			if(cifsFile->emptyDir) {
				cFYI(1, ("End of search, empty dir"));
				rc = 0;
				break;
			}
		} /* else {
			cifsFile->invalidHandle = TRUE;
			CIFSFindClose(xid, pTcon, cifsFile->netfid);
		} 
		if(cifsFile->search_resume_name) {
			kfree(cifsFile->search_resume_name);
			cifsFile->search_resume_name = NULL;
		} */

		find_cifs_entry(file->f_pos, cifsFile);
		cifs_filldir_with_entries(file->f_pos, cifsFile);
		/* 2) initiate search, */
		/* 3) seek into search buffer */
		/* 4) if not found && later - FindNext */
		/* else if earlier in search, close search and 
				restart, continuing search till found or EndOfSearch */
	}

	if (full_path)
		kfree(full_path);

	FreeXid(xid);
	return rc;
}
#endif /* CIFS_EXPERIMENTAL */
