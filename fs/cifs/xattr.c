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

	/* return dosattributes as pseudo xattr */
	/* return alt name if available as pseudo attr */

	/* if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to 
		returns as xattrs */

        return rc;
}
