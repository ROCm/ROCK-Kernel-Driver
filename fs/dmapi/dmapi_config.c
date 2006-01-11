/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
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
 * or the like.	 Any license provided herein, whether implied or
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

#include <asm/uaccess.h>
#include "dmapi.h"
#include "dmapi_kern.h"
#include "dmapi_private.h"

int
dm_get_config(
	void		__user *hanp,
	size_t		hlen,
	dm_config_t	flagname,
	dm_size_t	__user *retvalp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	dm_size_t	retval;
	int		system = 1;
	int		error;

	/* Trap and process configuration parameters which are system-wide. */

	switch (flagname) {
	case DM_CONFIG_LEGACY:
	case DM_CONFIG_PENDING:
	case DM_CONFIG_OBJ_REF:
		retval = DM_TRUE;
		break;
	case DM_CONFIG_MAX_MESSAGE_DATA:
		retval = DM_MAX_MSG_DATA;
		break;
	default:
		system = 0;
		break;
	}
	if (system) {
		if (copy_to_user(retvalp, &retval, sizeof(retval)))
			return(-EFAULT);
		return(0);
	}

	/* Must be filesystem-specific.	 Convert the handle into an inode. */

	if ((error = dm_get_config_tdp(hanp, hlen, &tdp)) != 0)
		return(error);

	/* Now call the filesystem-specific routine to determine the
	   value of the configuration option for that filesystem.
	*/

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->get_config(tdp->td_ip, tdp->td_right,
		flagname, retvalp);

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_get_config_events(
	void		__user *hanp,
	size_t		hlen,
	u_int		nelem,
	dm_eventset_t	__user *eventsetp,
	u_int		__user *nelemp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	/* Convert the handle into an inode. */

	if ((error = dm_get_config_tdp(hanp, hlen, &tdp)) != 0)
		return(error);

	/* Now call the filesystem-specific routine to determine the
	   events supported by that filesystem.
	*/

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->get_config_events(tdp->td_ip, tdp->td_right,
		nelem, eventsetp, nelemp);

	dm_app_put_tdp(tdp);
	return(error);
}
