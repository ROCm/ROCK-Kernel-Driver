/*
 * Copyright (c) 2000, 2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <xfs.h>

int
xfs_attr_fetch(xfs_inode_t *ip, char *name, char *value, int valuelen)
{
	xfs_da_args_t args;
	int error;

	if (XFS_IFORK_Q(ip) == 0)
		return ENOATTR;
	/*
	 * Do the argument setup for the xfs_attr routines.
	 */
	memset((char *)&args, 0, sizeof(args));
	args.dp = ip;
	args.flags = ATTR_ROOT;
	args.whichfork = XFS_ATTR_FORK;
	args.name = name;
	args.namelen = strlen(name);
	args.value = value;
	args.valuelen = valuelen;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.oknoent = 1;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (args.dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL)
		error = xfs_attr_shortform_getvalue(&args);
	else if (xfs_bmap_one_block(args.dp, XFS_ATTR_FORK))
		error = xfs_attr_leaf_get(&args);
	else
		error = xfs_attr_node_get(&args);

	if (error == EEXIST)
		error = 0;

	return(error);
}
