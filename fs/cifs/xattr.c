/*
 *   fs/cifs/xattr.c
 *
 *   Copyright (c) International Business Machines  Corp., 2003
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
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

int cifs_removexattr(struct dentry * direntry, const char * name)
{
	int rc = -EOPNOTSUPP;
	return rc;
}

int cifs_setxattr(struct dentry * direntry, const char * name,
        const void * value, size_t size, int flags)
{
	int rc = -EOPNOTSUPP;
	return rc;
}

ssize_t cifs_getxattr(struct dentry * direntry, const char * name,
         void * value, size_t size)
{
	ssize_t rc = -EOPNOTSUPP;
	return rc;
}

ssize_t cifs_listxattr(struct dentry * direntry, char * ea_data, size_t ea_size)
{
	ssize_t rc = -EOPNOTSUPP;
#ifdef CONFIG_CIFS_XATTR
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct super_block * sb;
	char * full_path;
	if(direntry == NULL)
		return -EIO;
	if(direntry->d_inode)
		return -EIO;
	sb = direntry->d_inode->i_sb;
	if(sb == NULL)
		return -EIO;
	xid = GetXid();

	cifs_sb = CIFS_SB(sb);
	pTcon = cifs_sb->tcon;

	down(&sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(direntry);
	up(&sb->s_vfs_rename_sem);
	if(full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}
	/* return dosattributes as pseudo xattr */
	/* return alt name if available as pseudo attr */

	/* if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to 
		returns as xattrs */
	rc = CIFSSMBQAllEAs(xid,pTcon,full_path,ea_data,ea_size,cifs_sb->local_nls);
	FreeXid(xid);
#endif
	return rc;
}
