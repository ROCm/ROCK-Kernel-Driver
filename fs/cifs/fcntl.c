/*
 *   fs/cifs/fcntl.c
 *
 *   vfs operations that deal with the file control API
 * 
 *   Copyright (C) International Business Machines  Corp., 2003,2004
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
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"

int cifs_dir_notify(struct file * file, unsigned long arg)
{
	int xid;
	int rc = -EINVAL;
	int oplock = FALSE;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	__u32 filter = FILE_NOTIFY_CHANGE_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES;
    __u16 netfid;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	down(&file->f_dentry->d_sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(file->f_dentry);
	up(&file->f_dentry->d_sb->s_vfs_rename_sem);

	if(full_path == NULL) {
		rc = -ENOMEM;
	} else {
		cFYI(1,("cifs dir notify on file %s",full_path));
		rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN, 
			GENERIC_READ | SYNCHRONIZE, 0 /* create options */,
			&netfid, &oplock,NULL, cifs_sb->local_nls);
		/* BB fixme - add this handle to a notify handle list */
		if(rc) {
			cFYI(1,("Could not open directory for notify"));
		} else {
			rc = CIFSSMBNotify(xid, pTcon, 1 /* subdirs */, netfid, 
				filter, cifs_sb->local_nls);
			/* BB add code to close file eventually (at unmount
			it would close automatically but may be a way
			to do it easily when inode freed or when
			notify info is cleared/changed */
		}
	}
	
	FreeXid(xid);
	return rc;
}
