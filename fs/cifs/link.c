/*
 *   fs/cifs/link.c
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
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

int
cifs_hardlink(struct dentry *old_file, struct inode *inode,
	      struct dentry *direntry)
{
	int rc = -EACCES;
	int xid;
	char *fromName = NULL;
	char *toName = NULL;
	struct cifs_sb_info *cifs_sb_target;
	struct cifsTconInfo *pTcon;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cifs_sb_target = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb_target->tcon;

/* No need to check for cross device links since server will do that - BB note DFS case in future though (when we may have to check) */

	fromName = build_path_from_dentry(old_file);
	toName = build_path_from_dentry(direntry);
	if (cifs_sb_target->tcon->ses->capabilities & CAP_UNIX)
		rc = CIFSUnixCreateHardLink(xid, pTcon, fromName, toName,
					    cifs_sb_target->local_nls);
	else
		rc = CIFSCreateHardLink(xid, pTcon, fromName, toName,
					cifs_sb_target->local_nls);

/* if (!rc)     */
	{
		/*   renew_parental_timestamps(old_file);
		   inode->i_nlink++;
		   mark_inode_dirty(inode);
		   d_instantiate(direntry, inode); */
		/* BB add call to either mark inode dirty or refresh its data and timestamp to current time */
	}
	d_drop(direntry);	/* force new lookup from server */
	cifsInode = CIFS_I(old_file->d_inode);
	cifsInode->time = 0;	/* will force revalidate to go get info when needed */

	if (fromName)
		kfree(fromName);
	if (toName)
		kfree(toName);
	FreeXid(xid);
	return rc;
}

int
cifs_follow_link(struct dentry *direntry, struct nameidata *nd)
{
	struct inode *inode = direntry->d_inode;
	int rc = -EACCES;
	int xid;
	char *full_path = NULL;
	char target_path[257];
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	xid = GetXid();
	full_path = build_path_from_dentry(direntry);
	cFYI(1, ("\nFull path: %s inode = 0x%p\n", full_path, inode));
	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	/* can not call the following line due to EFAULT in vfs_readlink which is presumably expecting a user space buffer */
	/* length = cifs_readlink(direntry,target_path, sizeof(target_path) - 1);    */

/* BB add read reparse point symlink code and Unix extensions symlink code here BB */
	if (pTcon->ses->capabilities & CAP_UNIX)
		rc = CIFSSMBUnixQuerySymLink(xid, pTcon, full_path,
					     target_path,
					     sizeof (target_path) - 1,
					     cifs_sb->local_nls);
	else {
		/* rc = CIFSSMBQueryReparseLinkInfo */
		/* BB Add code to Query ReparsePoint info */
	}
	/* BB Anything else to do to handle recursive links? */
	/* BB Should we be using page symlink ops here? */

	if (rc == 0) {
		target_path[256] = 0;
		rc = vfs_follow_link(nd, target_path);
	}
	/* else EACCESS */

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_symlink(struct inode *inode, struct dentry *direntry, const char *symname)
{
	int rc = -EOPNOTSUPP;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	cFYI(1, ("\nFull path: %s ", full_path));
	cFYI(1, (" symname is %s\n", symname));

	/* BB what if DFS and this volume is on different share? BB */
	if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)
		rc = CIFSUnixCreateSymLink(xid, pTcon, full_path, symname,
					   cifs_sb->local_nls);
	/* else
	   rc = CIFSCreateReparseSymLink(xid, pTcon, fromName, toName,cifs_sb_target->local_nls); */

	if (rc == 0) {
		if (pTcon->ses->capabilities & CAP_UNIX)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb);
		else
			rc = cifs_get_inode_info(&newinode, full_path,
						 inode->i_sb);

		if (rc != 0) {
			cFYI(1,
			     ("\nCreate symlink worked but get_inode_info failed with rc = %d ",
			      rc));
		} else {
			direntry->d_op = &cifs_dentry_ops;
			d_instantiate(direntry, newinode);
		}
	}

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_readlink(struct dentry *direntry, char *pBuffer, int buflen)
{
	struct inode *inode = direntry->d_inode;
	int rc = -EACCES;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	char tmpbuffer[256];

	xid = GetXid();
	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;
	full_path = build_path_from_dentry(direntry);
	cFYI(1,
	     ("\nFull path: %s inode = 0x%p pBuffer = 0x%p buflen = %d\n",
	      full_path, inode, pBuffer, buflen));

/* BB add read reparse point symlink code and Unix extensions symlink code here BB */
	if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)
		rc = CIFSSMBUnixQuerySymLink(xid, pTcon, full_path,
					     tmpbuffer,
					     sizeof (tmpbuffer) - 1,
					     cifs_sb->local_nls);
	else {
		/* rc = CIFSSMBQueryReparseLinkInfo */
		/* BB Add code to Query ReparsePoint info */
	}
	/* BB Anything else to do to handle recursive links? */
	/* BB Should we be using page ops here? */

	/* BB null terminate returned string in pBuffer? BB */
	if (buflen > sizeof (tmpbuffer))
		buflen = sizeof (tmpbuffer);
	if (rc == 0) {
		rc = vfs_readlink(direntry, pBuffer, buflen, tmpbuffer);
		cFYI(1,
		     ("vfs_readlink called from cifs_readlink returned %d",
		      rc));
	}

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}
