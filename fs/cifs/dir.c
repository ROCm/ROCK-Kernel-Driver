/*
 *   fs/cifs/dir.c
 *
 *   vfs operations that deal with dentries
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
#include <linux/slab.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

void
renew_parental_timestamps(struct dentry *direntry)
{
	/* BB check if there is a way to get the kernel to do this or if we really need this */
	do {
		direntry->d_time = jiffies;
		direntry = direntry->d_parent;
	} while (!IS_ROOT(direntry));	/* BB for DFS case should stop at the root of share which could be lower than root of this mount due to implicit dfs connections */
}

/* Note: caller must free return buffer */
char *
build_path_from_dentry(struct dentry *direntry)
{
	struct dentry *temp;
	int namelen = 0;
	char *full_path;

	for (temp = direntry; !IS_ROOT(temp);) {
		namelen += (1 + temp->d_name.len);
		cFYI(1, (" len %d ", namelen));
		temp = temp->d_parent;
	}
	namelen += 1;		/* allow for trailing null */
	full_path = kmalloc(namelen, GFP_KERNEL);
	namelen--;
	full_path[namelen] = 0;	/* trailing null */
	for (temp = direntry; !IS_ROOT(temp);) {
		namelen -= 1 + temp->d_name.len;
		if (namelen < 0) {
			break;
		} else {
			full_path[namelen] = '\\';
			strncpy(full_path + namelen + 1, temp->d_name.name,
				temp->d_name.len);
			cFYI(0, (" name: %s ", full_path + namelen));
		}
		temp = temp->d_parent;
	}
	if (namelen != 0)
		cERROR(1,
		       ("We did not end path lookup where we expected namelen is %d",
			namelen));

	return full_path;
}

char *
build_wildcard_path_from_dentry(struct dentry *direntry)
{
	struct dentry *temp;
	int namelen = 0;
	char *full_path;

	for (temp = direntry; !IS_ROOT(temp);) {
		namelen += (1 + temp->d_name.len);
		temp = temp->d_parent;
	}
	namelen += 3;		/* allow for trailing null and wildcard (slash and *) */
	full_path = kmalloc(namelen, GFP_KERNEL);
	namelen--;
	full_path[namelen] = 0;	/* trailing null */
	namelen--;
	full_path[namelen] = '*';
	namelen--;
	full_path[namelen] = '\\';

	for (temp = direntry; !IS_ROOT(temp);) {
		namelen -= 1 + temp->d_name.len;
		if (namelen < 0) {
			break;
		} else {
			full_path[namelen] = '\\';
			strncpy(full_path + namelen + 1, temp->d_name.name,
				temp->d_name.len);
		}
		temp = temp->d_parent;
	}
	if (namelen != 0)
		cERROR(1,
		       ("We did not end path lookup where we expected namelen is %d",
			namelen));

	return full_path;
}

/* Inode operations in similar order to how they appear in the Linux file fs.h */

int
cifs_create(struct inode *inode, struct dentry *direntry, int mode,
		struct nameidata *nd)
{
	int rc = -ENOENT;
	int xid;
	int oplock = REQ_OPLOCK;
	__u16 fileHandle;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);

	/* BB add processing for setting the equivalent of mode - e.g. via CreateX with ACLs */

	rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OVERWRITE_IF, GENERIC_ALL
			 /* 0x20197 was used previously */ , CREATE_NOT_DIR,
			 &fileHandle, &oplock, cifs_sb->local_nls);
	if (rc) {
		cFYI(1, ("cifs_create returned 0x%x ", rc));
	} else {
		if (pTcon->ses->capabilities & CAP_UNIX)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb);
		else
			rc = cifs_get_inode_info(&newinode, full_path,
						 inode->i_sb);

		if (rc != 0) {
			cFYI(1,("Create worked but get_inode_info failed with rc = %d",
			      rc));
			/* close handle */
		} else {
			direntry->d_op = &cifs_dentry_ops;
			d_instantiate(direntry, newinode);
		}
		/* BB check oplock state before deciding to call following */
/*        if(*oplock)
            save off handle in inode and dontdoclose */
        
		CIFSSMBClose(xid, pTcon, fileHandle);	
        /* BB In the future chain close with the NTCreateX to narrow window */
        
        if(newinode)
            newinode->i_mode = mode;
	}
	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}

struct dentry *
cifs_lookup(struct inode *parent_dir_inode, struct dentry *direntry, struct nameidata *nd)
{
	int rc, xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct inode *newInode = NULL;
	char *full_path = NULL;

	xid = GetXid();

	cFYI(1,
	     (" parent inode = 0x%p name is: %s and dentry = 0x%p",
	      parent_dir_inode, direntry->d_name.name, direntry));

	/* BB Add check of incoming data - e.g. frame not longer than maximum SMB - let server check the namelen BB */

	/* check whether path exists */

	cifs_sb = CIFS_SB(parent_dir_inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	if (direntry->d_inode != NULL) {
		cFYI(1, (" non-NULL inode in lookup"));
	} else {
		cFYI(1, (" NULL inode in lookup"));
	}
	cFYI(1,
	     (" Full path: %s inode = 0x%p", full_path, direntry->d_inode));
	if (pTcon->ses->capabilities & CAP_UNIX)
		rc = cifs_get_inode_info_unix(&newInode, full_path,
					      parent_dir_inode->i_sb);
	else
		rc = cifs_get_inode_info(&newInode, full_path,
					 parent_dir_inode->i_sb);

	if ((rc == 0) && (newInode != NULL)) {
		direntry->d_op = &cifs_dentry_ops;
		d_add(direntry, newInode);

		/* since paths are not looked up by component - the parent directories are presumed to be good here */
		renew_parental_timestamps(direntry);

	} else if (rc == -ENOENT) {
		rc = 0;
		d_add(direntry, NULL);
	} else {
		cERROR(1,
		       ("Error 0x%x or (%d decimal) on cifs_get_inode_info in lookup",
			rc, rc));
		/* BB special case check for Access Denied - watch security exposure of returning dir info implicitly via different rc if file exists or not but no access BB */
	}

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return ERR_PTR(rc);
}

int
cifs_dir_open(struct inode *inode, struct file *file)
{				/* NB: currently unused since searches are opened in readdir */
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_wildcard_path_from_dentry(file->f_dentry);

	cFYI(1, (" inode = 0x%p and full path is %s", inode, full_path));

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

static int
cifs_d_revalidate(struct dentry *direntry, struct nameidata *nd)
{
	int isValid = 1;

/*	lock_kernel(); *//* surely we do not want to lock the kernel for a whole network round trip which could take seconds */

	if (direntry->d_inode) {
		if (cifs_revalidate(direntry)) {
			/* unlock_kernel(); */
			return 0;
		}
	} else {
		cFYI(1,
		     ("In cifs_d_revalidate with no inode but name = %s and dentry 0x%p",
		      direntry->d_name.name, direntry));
	}

/*    unlock_kernel(); */

	return isValid;
}

/* static int cifs_d_delete(struct dentry *direntry)
{
	int rc = 0;

	cFYI(1, ("In cifs d_delete, name = %s", direntry->d_name.name));

	return rc;
}     */

struct dentry_operations cifs_dentry_ops = {
	.d_revalidate = cifs_d_revalidate,
/* d_delete:       cifs_d_delete,       *//* not needed except for debugging */
	/* no need for d_hash, d_compare, d_release, d_iput ... yet. BB confirm this BB */
};
