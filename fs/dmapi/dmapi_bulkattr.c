/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
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

#include "dmapi.h"
#include "dmapi_kern.h"
#include "dmapi_private.h"


int
dm_init_attrloc(
	dm_sessid_t	sid,
	void		__user  *hanp,
	size_t		hlen,
	dm_token_t	token,
	dm_attrloc_t	__user *locp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_VFS|DM_TDT_DIR,
		DM_RIGHT_SHARED, &tdp);
	if (error != 0)
		return(error);

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->init_attrloc(tdp->td_ip, tdp->td_right, locp);

	dm_app_put_tdp(tdp);
	return(error);
}


/*
 * Retrieves both standard and DM specific file attributes for the file
 * system indicated by the handle. (The FS has to be mounted).
 * Syscall returns 1 to indicate SUCCESS and more information is available.
 * -1 is returned on error, and errno will be set appropriately.
 * 0 is returned upon successful completion.
 */

int
dm_get_bulkattr_rvp(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token,
	u_int		mask,
	dm_attrloc_t	__user *locp,
	size_t		buflen,
	void		__user *bufp,
	size_t		__user *rlenp,
	int		*rvp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_VFS,
		DM_RIGHT_SHARED, &tdp);
	if (error != 0)
		return(error);

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->get_bulkattr_rvp(tdp->td_ip, tdp->td_right,
			mask, locp, buflen, bufp, rlenp, rvp);

	dm_app_put_tdp(tdp);
	return(error);
}


/*
 * Retrieves attributes of directory entries given a handle to that
 * directory. Iterative.
 * Syscall returns 1 to indicate SUCCESS and more information is available.
 * -1 is returned on error, and errno will be set appropriately.
 * 0 is returned upon successful completion.
 */

int
dm_get_dirattrs_rvp(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token,
	u_int		mask,
	dm_attrloc_t	__user *locp,
	size_t		buflen,
	void		__user *bufp,
	size_t		__user *rlenp,
	int		*rvp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_DIR,
		DM_RIGHT_SHARED, &tdp);
	if (error != 0)
		return(error);

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->get_dirattrs_rvp(tdp->td_ip, tdp->td_right,
		mask, locp, buflen, bufp, rlenp, rvp);

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_get_bulkall_rvp(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token,
	u_int		mask,
	dm_attrname_t	__user *attrnamep,
	dm_attrloc_t	__user *locp,
	size_t		buflen,
	void		__user *bufp,
	size_t		__user *rlenp,
	int		*rvp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_VFS,
		DM_RIGHT_SHARED, &tdp);
	if (error != 0)
		return(error);

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->get_bulkall_rvp(tdp->td_ip, tdp->td_right,
		mask, attrnamep, locp, buflen, bufp, rlenp, rvp);

	dm_app_put_tdp(tdp);
	return(error);
}
