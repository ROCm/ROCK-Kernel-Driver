/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
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

/* Data Migration API (DMAPI)
 */


/* We're using MISC_MAJOR / MISC_DYNAMIC_MINOR. */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

#include "dmapi.h"
#include "dmapi_kern.h"
#include "dmapi_private.h"

struct kmem_cache	*dm_fsreg_cachep = NULL;
struct kmem_cache	*dm_tokdata_cachep = NULL;
struct kmem_cache	*dm_session_cachep = NULL;
struct kmem_cache	*dm_fsys_map_cachep = NULL;
struct kmem_cache	*dm_fsys_vptr_cachep = NULL;

static int
dmapi_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	    unsigned long arg)
{
	sys_dmapi_args_t kargs;
	sys_dmapi_args_t *uap = &kargs;
	int error = 0;
	int rvp = -ENOSYS;
	int use_rvp = 0;

	if (!capable(CAP_MKNOD))
		return -EPERM;

	if( copy_from_user( &kargs, (sys_dmapi_args_t __user *)arg,
			   sizeof(sys_dmapi_args_t) ) )
		return -EFAULT;

	unlock_kernel();

	switch (cmd) {
	case DM_CLEAR_INHERIT:
		error = dm_clear_inherit(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_attrname_t __user *) DM_Parg(uap,5));/* attrnamep */
		break;
	case DM_CREATE_BY_HANDLE:
		error = dm_create_by_handle(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* dirhanp */
				(size_t)	DM_Uarg(uap,3), /* dirhlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(void __user *)	DM_Parg(uap,5), /* hanp */
				(size_t)	DM_Uarg(uap,6), /* hlen */
				(char __user *)	DM_Parg(uap,7));/* cname */
		break;
	case DM_CREATE_SESSION:
		error = dm_create_session(
				(dm_sessid_t)	DM_Uarg(uap,1), /* oldsid */
				(char __user *)	DM_Parg(uap,2), /* sessinfop */
				(dm_sessid_t __user *) DM_Parg(uap,3));/* newsidp */
		break;
	case DM_CREATE_USEREVENT:
		error = dm_create_userevent(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(size_t)	DM_Uarg(uap,2), /* msglen */
				(void __user *)	DM_Parg(uap,3), /* msgdatap */
				(dm_token_t __user *) DM_Parg(uap,4));/* tokenp */
		break;
	case DM_DESTROY_SESSION:
		error = dm_destroy_session(
				(dm_sessid_t)	DM_Uarg(uap,1));/* sid */
		break;
	case DM_DOWNGRADE_RIGHT:
		error = dm_downgrade_right(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4));/* token */
		break;
	case DM_FD_TO_HANDLE:
		error = dm_fd_to_hdl(
				(int)		DM_Uarg(uap,1), /* fd */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t __user *) DM_Parg(uap,3));/* hlenp */
		break;
	case DM_FIND_EVENTMSG:
		error = dm_find_eventmsg(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(dm_token_t)	DM_Uarg(uap,2), /* token */
				(size_t)	DM_Uarg(uap,3), /* buflen */
				(void __user *)	DM_Parg(uap,4), /* bufp */
				(size_t __user *) DM_Parg(uap,5));/* rlenp */
		break;
	case DM_GET_ALLOCINFO:
		use_rvp = 1;
		error = dm_get_allocinfo_rvp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_off_t __user *) DM_Parg(uap,5), /* offp */
				(u_int)		DM_Uarg(uap,6), /* nelem */
				(dm_extent_t __user *) DM_Parg(uap,7), /* extentp */
				(u_int __user *) DM_Parg(uap,8), /* nelemp */
						&rvp);
		break;
	case DM_GET_BULKALL:
		use_rvp = 1;
		error = dm_get_bulkall_rvp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* mask */
				(dm_attrname_t __user *) DM_Parg(uap,6),/* attrnamep */
				(dm_attrloc_t __user *) DM_Parg(uap,7),/* locp */
				(size_t)	DM_Uarg(uap,8), /* buflen */
				(void __user *)	DM_Parg(uap,9), /* bufp */
				(size_t __user *) DM_Parg(uap,10),/* rlenp */
						&rvp);
		break;
	case DM_GET_BULKATTR:
		use_rvp = 1;
		error = dm_get_bulkattr_rvp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* mask */
				(dm_attrloc_t __user *)DM_Parg(uap,6), /* locp */
				(size_t)	DM_Uarg(uap,7), /* buflen */
				(void __user *)	DM_Parg(uap,8), /* bufp */
				(size_t __user *) DM_Parg(uap,9), /* rlenp */
						&rvp);
		break;
	case DM_GET_CONFIG:
		error = dm_get_config(
				(void __user *)	DM_Parg(uap,1), /* hanp */
				(size_t)	DM_Uarg(uap,2), /* hlen */
				(dm_config_t)	DM_Uarg(uap,3), /* flagname */
				(dm_size_t __user *)DM_Parg(uap,4));/* retvalp */
		break;
	case DM_GET_CONFIG_EVENTS:
		error = dm_get_config_events(
				(void __user *)	DM_Parg(uap,1), /* hanp */
				(size_t)	DM_Uarg(uap,2), /* hlen */
				(u_int)		DM_Uarg(uap,3), /* nelem */
				(dm_eventset_t __user *) DM_Parg(uap,4),/* eventsetp */
				(u_int __user *) DM_Parg(uap,5));/* nelemp */
		break;
	case DM_GET_DIOINFO:
		error = dm_get_dioinfo(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_dioinfo_t __user *)DM_Parg(uap,5));/* diop */
		break;
	case DM_GET_DIRATTRS:
		use_rvp = 1;
		error = dm_get_dirattrs_rvp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* mask */
				(dm_attrloc_t __user *)DM_Parg(uap,6), /* locp */
				(size_t)	DM_Uarg(uap,7), /* buflen */
				(void __user *)	DM_Parg(uap,8), /* bufp */
				(size_t __user *) DM_Parg(uap,9), /* rlenp */
						&rvp);
		break;
	case DM_GET_DMATTR:
		error = dm_get_dmattr(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_attrname_t __user *) DM_Parg(uap,5),/* attrnamep */
				(size_t)	DM_Uarg(uap,6), /* buflen */
				(void __user *)	DM_Parg(uap,7), /* bufp */
				(size_t __user *)	DM_Parg(uap,8));/* rlenp */

		break;
	case DM_GET_EVENTLIST:
		error = dm_get_eventlist(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* nelem */
				(dm_eventset_t __user *) DM_Parg(uap,6),/* eventsetp */
				(u_int __user *) DM_Parg(uap,7));/* nelemp */
		break;
	case DM_GET_EVENTS:
		error = dm_get_events(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(u_int)		DM_Uarg(uap,2), /* maxmsgs */
				(u_int)		DM_Uarg(uap,3), /* flags */
				(size_t)	DM_Uarg(uap,4), /* buflen */
				(void __user *)	DM_Parg(uap,5), /* bufp */
				(size_t __user *) DM_Parg(uap,6));/* rlenp */
		break;
	case DM_GET_FILEATTR:
		error = dm_get_fileattr(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* mask */
				(dm_stat_t __user *) DM_Parg(uap,6));/* statp */
		break;
	case DM_GET_MOUNTINFO:
		error = dm_get_mountinfo(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(size_t)	DM_Uarg(uap,5), /* buflen */
				(void __user *)	DM_Parg(uap,6), /* bufp */
				(size_t __user *) DM_Parg(uap,7));/* rlenp */
		break;
	case DM_GET_REGION:
		error = dm_get_region(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* nelem */
				(dm_region_t __user *) DM_Parg(uap,6), /* regbufp */
				(u_int __user *) DM_Parg(uap,7));/* nelemp */
		break;
	case DM_GETALL_DISP:
		error = dm_getall_disp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(size_t)	DM_Uarg(uap,2), /* buflen */
				(void __user *)	DM_Parg(uap,3), /* bufp */
				(size_t __user *) DM_Parg(uap,4));/* rlenp */
		break;
	case DM_GETALL_DMATTR:
		error = dm_getall_dmattr(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(size_t)	DM_Uarg(uap,5), /* buflen */
				(void __user *)	DM_Parg(uap,6), /* bufp */
				(size_t __user *) DM_Parg(uap,7));/* rlenp */
		break;
	case DM_GETALL_INHERIT:
		error = dm_getall_inherit(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* nelem */
				(dm_inherit_t __user *)DM_Parg(uap,6), /* inheritbufp*/
				(u_int __user *) DM_Parg(uap,7));/* nelemp */
		break;
	case DM_GETALL_SESSIONS:
		error = dm_getall_sessions(
				(u_int)		DM_Uarg(uap,1), /* nelem */
				(dm_sessid_t __user *) DM_Parg(uap,2), /* sidbufp */
				(u_int __user *) DM_Parg(uap,3));/* nelemp */
		break;
	case DM_GETALL_TOKENS:
		error = dm_getall_tokens(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(u_int)		DM_Uarg(uap,2), /* nelem */
				(dm_token_t __user *) DM_Parg(uap,3), /* tokenbufp */
				(u_int __user *) DM_Parg(uap,4));/* nelemp */
		break;
	case DM_INIT_ATTRLOC:
		error = dm_init_attrloc(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_attrloc_t __user *) DM_Parg(uap,5));/* locp */
		break;
	case DM_MKDIR_BY_HANDLE:
		error = dm_mkdir_by_handle(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* dirhanp */
				(size_t)	DM_Uarg(uap,3), /* dirhlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(void __user *)	DM_Parg(uap,5), /* hanp */
				(size_t)	DM_Uarg(uap,6), /* hlen */
				(char __user *)	DM_Parg(uap,7));/* cname */
		break;
	case DM_MOVE_EVENT:
		error = dm_move_event(
				(dm_sessid_t)	DM_Uarg(uap,1), /* srcsid */
				(dm_token_t)	DM_Uarg(uap,2), /* token */
				(dm_sessid_t)	DM_Uarg(uap,3), /* targetsid */
				(dm_token_t __user *) DM_Parg(uap,4));/* rtokenp */
		break;
	case DM_OBJ_REF_HOLD:
		error = dm_obj_ref_hold(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(dm_token_t)	DM_Uarg(uap,2), /* token */
				(void __user *)	DM_Parg(uap,3), /* hanp */
				(size_t)	DM_Uarg(uap,4));/* hlen */
		break;
	case DM_OBJ_REF_QUERY:
		use_rvp = 1;
		error = dm_obj_ref_query_rvp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(dm_token_t)	DM_Uarg(uap,2), /* token */
				(void __user *)	DM_Parg(uap,3), /* hanp */
				(size_t)	DM_Uarg(uap,4), /* hlen */
						&rvp);
		break;
	case DM_OBJ_REF_RELE:
		error = dm_obj_ref_rele(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(dm_token_t)	DM_Uarg(uap,2), /* token */
				(void __user *)	DM_Parg(uap,3), /* hanp */
				(size_t)	DM_Uarg(uap,4));/* hlen */
		break;
	case DM_PATH_TO_FSHANDLE:
		error = dm_path_to_fshdl(
				(char __user *)	DM_Parg(uap,1), /* path */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t __user *) DM_Parg(uap,3));/* hlenp */
		break;
	case DM_PATH_TO_HANDLE:
		error = dm_path_to_hdl(
				(char __user *)	DM_Parg(uap,1), /* path */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t __user *) DM_Parg(uap,3));/* hlenp */
		break;
	case DM_PENDING:
		error = dm_pending(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(dm_token_t)	DM_Uarg(uap,2), /* token */
				(dm_timestruct_t __user *) DM_Parg(uap,3));/* delay */
		break;
	case DM_PROBE_HOLE:
		error = dm_probe_hole(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_off_t)	DM_Uarg(uap,5), /* off */
				(dm_size_t)	DM_Uarg(uap,6), /* len */
				(dm_off_t __user *) DM_Parg(uap,7), /* roffp */
				(dm_size_t __user *) DM_Parg(uap,8));/* rlenp */
		break;
	case DM_PUNCH_HOLE:
		error = dm_punch_hole(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_off_t)	DM_Uarg(uap,5), /* off */
				(dm_size_t)	DM_Uarg(uap,6));/* len */
		break;
	case DM_QUERY_RIGHT:
		error = dm_query_right(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_right_t __user *) DM_Parg(uap,5));/* rightp */
		break;
	case DM_QUERY_SESSION:
		error = dm_query_session(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(size_t)	DM_Uarg(uap,2), /* buflen */
				(void __user *)	DM_Parg(uap,3), /* bufp */
				(size_t __user *) DM_Parg(uap,4));/* rlenp */
		break;
	case DM_READ_INVIS:
		use_rvp = 1;
		error = dm_read_invis_rvp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_off_t)	DM_Uarg(uap,5), /* off */
				(dm_size_t)	DM_Uarg(uap,6), /* len */
				(void __user *)	DM_Parg(uap,7), /* bufp */
						&rvp);
		break;
	case DM_RELEASE_RIGHT:
		error = dm_release_right(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4));/* token */
		break;
	case DM_REMOVE_DMATTR:
		error = dm_remove_dmattr(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(int)		DM_Uarg(uap,5), /* setdtime */
				(dm_attrname_t __user *) DM_Parg(uap,6));/* attrnamep */
		break;
	case DM_REQUEST_RIGHT:
		error = dm_request_right(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* flags */
				(dm_right_t)	DM_Uarg(uap,6));/* right */
		break;
	case DM_RESPOND_EVENT:
		error = dm_respond_event(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(dm_token_t)	DM_Uarg(uap,2), /* token */
				(dm_response_t) DM_Uarg(uap,3), /* response */
				(int)		DM_Uarg(uap,4), /* reterror */
				(size_t)	DM_Uarg(uap,5), /* buflen */
				(void __user *)	DM_Parg(uap,6));/* respbufp */
		break;
	case DM_SEND_MSG:
		error = dm_send_msg(
				(dm_sessid_t)	DM_Uarg(uap,1), /* targetsid */
				(dm_msgtype_t)	DM_Uarg(uap,2), /* msgtype */
				(size_t)	DM_Uarg(uap,3), /* buflen */
				(void __user *)	DM_Parg(uap,4));/* bufp */
		break;
	case DM_SET_DISP:
		error = dm_set_disp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_eventset_t __user *) DM_Parg(uap,5),/* eventsetp */
				(u_int)		DM_Uarg(uap,6));/* maxevent */
		break;
	case DM_SET_DMATTR:
		error = dm_set_dmattr(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_attrname_t __user *) DM_Parg(uap,5),/* attrnamep */
				(int)		DM_Uarg(uap,6), /* setdtime */
				(size_t)	DM_Uarg(uap,7), /* buflen */
				(void __user *)	DM_Parg(uap,8));/* bufp */
		break;
	case DM_SET_EVENTLIST:
		error = dm_set_eventlist(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_eventset_t __user *) DM_Parg(uap,5),/* eventsetp */
				(u_int)		DM_Uarg(uap,6));/* maxevent */
		break;
	case DM_SET_FILEATTR:
		error = dm_set_fileattr(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* mask */
				(dm_fileattr_t __user *)DM_Parg(uap,6));/* attrp */
		break;
	case DM_SET_INHERIT:
		error = dm_set_inherit(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_attrname_t __user *)DM_Parg(uap,5),/* attrnamep */
				(mode_t)	DM_Uarg(uap,6));/* mode */
		break;
	case DM_SET_REGION:
		error = dm_set_region(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(u_int)		DM_Uarg(uap,5), /* nelem */
				(dm_region_t __user *) DM_Parg(uap,6), /* regbufp */
				(dm_boolean_t __user *) DM_Parg(uap,7));/* exactflagp */
		break;
	case DM_SET_RETURN_ON_DESTROY:
		error = dm_set_return_on_destroy(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(dm_attrname_t __user *) DM_Parg(uap,5),/* attrnamep */
				(dm_boolean_t)	DM_Uarg(uap,6));/* enable */
		break;
	case DM_SYMLINK_BY_HANDLE:
		error = dm_symlink_by_handle(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* dirhanp */
				(size_t)	DM_Uarg(uap,3), /* dirhlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(void __user *)	DM_Parg(uap,5), /* hanp */
				(size_t)	DM_Uarg(uap,6), /* hlen */
				(char __user *)	DM_Parg(uap,7), /* cname */
				(char __user *)	DM_Parg(uap,8));/* path */
		break;
	case DM_SYNC_BY_HANDLE:
		error = dm_sync_by_handle(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4));/* token */
		break;
	case DM_UPGRADE_RIGHT:
		error = dm_upgrade_right(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4));/* token */
		break;
	case DM_WRITE_INVIS:
		use_rvp = 1;
		error = dm_write_invis_rvp(
				(dm_sessid_t)	DM_Uarg(uap,1), /* sid */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(dm_token_t)	DM_Uarg(uap,4), /* token */
				(int)		DM_Uarg(uap,5), /* flags */
				(dm_off_t)	DM_Uarg(uap,6), /* off */
				(dm_size_t)	DM_Uarg(uap,7), /* len */
				(void __user *)	DM_Parg(uap,8), /* bufp */
						&rvp);
		break;
	case DM_OPEN_BY_HANDLE:
		use_rvp = 1;
		error = dm_open_by_handle_rvp(
				(unsigned int)	DM_Uarg(uap,1), /* fd */
				(void __user *)	DM_Parg(uap,2), /* hanp */
				(size_t)	DM_Uarg(uap,3), /* hlen */
				(int)		DM_Uarg(uap,4), /* flags */
						&rvp);
		break;
	default:
		error = -ENOSYS;
		break;
	}

	lock_kernel();

	/* If it was an *_rvp() function, then
		if error==0, return |rvp|
	*/
	if( use_rvp && (error == 0) )
		return rvp;
	else
		return error;
}



static int
dmapi_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int
dmapi_release(struct inode *inode, struct file *file)
{
	return 0;
}


/* say hello, and let me know the device is hooked up */
static ssize_t
dmapi_dump(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char tmp[50];
	int len;
	if( *ppos == 0 ){
		len = sprintf( tmp, "# " DM_VER_STR_CONTENTS "\n" );
		if( copy_to_user(buf, tmp, len) )
			return -EFAULT;
		*ppos += 1;
		return len;
	}
	return 0;
}

static struct file_operations dmapi_fops = {
	.open		= dmapi_open,
	.ioctl		= dmapi_ioctl,
	.read		= dmapi_dump,
	.release	= dmapi_release
};

static struct miscdevice dmapi_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "dmapi",
	.fops		= &dmapi_fops
};



#ifdef CONFIG_PROC_FS
static int
dmapi_summary(char *buffer, char **start, off_t offset,
		 int count, int *eof, void *data)
{
	int len;

	extern u_int dm_sessions_active;
	extern dm_sessid_t dm_next_sessid;
	extern dm_token_t dm_next_token;
	extern dm_sequence_t dm_next_sequence;
	extern int dm_fsys_cnt;

#define CHKFULL if(len >= count) break;
#define ADDBUF(a,b)	len += sprintf(buffer + len, a, b); CHKFULL;

	len=0;
	while(1){
		ADDBUF("dm_sessions_active=%u\n", dm_sessions_active);
		ADDBUF("dm_next_sessid=%d\n", (int)dm_next_sessid);
		ADDBUF("dm_next_token=%d\n", (int)dm_next_token);
		ADDBUF("dm_next_sequence=%u\n", (u_int)dm_next_sequence);
		ADDBUF("dm_fsys_cnt=%d\n", dm_fsys_cnt);

		break;
	}

	if (offset >= len) {
		*start = buffer;
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;

	return len;
}
#endif


static void __init
dmapi_init_procfs(int dmapi_minor)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *entry;

	if ((entry = proc_mkdir( DMAPI_DBG_PROCFS, NULL)) == NULL )
		return;
	entry->owner = THIS_MODULE;
	entry->mode = S_IFDIR | S_IRUSR | S_IXUSR;

	if ((entry = proc_mkdir( DMAPI_DBG_PROCFS "/fsreg", NULL)) == NULL )
		return;
	entry->owner = THIS_MODULE;

	if ((entry = proc_mkdir( DMAPI_DBG_PROCFS "/sessions", NULL)) == NULL )
		return;
	entry->owner = THIS_MODULE;

	entry = create_proc_read_entry( DMAPI_DBG_PROCFS "/summary",
			0, NULL, dmapi_summary, NULL);
	entry->owner = THIS_MODULE;
#endif
}

#if 0
static void __exit
dmapi_cleanup_procfs(void)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry( DMAPI_DBG_PROCFS "/summary", NULL);
	remove_proc_entry( DMAPI_DBG_PROCFS "/fsreg", NULL);
	remove_proc_entry( DMAPI_DBG_PROCFS "/sessions", NULL);
	remove_proc_entry( DMAPI_DBG_PROCFS, NULL);
#endif
}
#endif

int __init dmapi_init(void)
{
	int ret;

	dm_tokdata_cachep = kmem_cache_create("dm_tokdata",
				sizeof(struct dm_tokdata), 0, 0, NULL);
	if (dm_tokdata_cachep == NULL)
		goto out;

	dm_fsreg_cachep = kmem_cache_create("dm_fsreg",
				sizeof(struct dm_fsreg), 0, 0, NULL);
	if (dm_fsreg_cachep == NULL)
		goto out_free_tokdata_cachep;

	dm_session_cachep = kmem_cache_create("dm_session",
				sizeof(struct dm_session), 0, 0, NULL);
	if (dm_session_cachep == NULL)
		goto out_free_fsreg_cachep;

	dm_fsys_map_cachep = kmem_cache_create("dm_fsys_map",
				sizeof(dm_vector_map_t), 0, 0, NULL);
	if (dm_fsys_map_cachep == NULL)
		goto out_free_session_cachep;
	dm_fsys_vptr_cachep = kmem_cache_create("dm_fsys_vptr",
				sizeof(dm_fsys_vector_t), 0, 0, NULL);
	if (dm_fsys_vptr_cachep == NULL)
		goto out_free_fsys_map_cachep;

	ret = misc_register(&dmapi_dev);
	if (ret) {
		printk(KERN_ERR "dmapi_init: misc_register returned %d\n", ret);
		goto out_free_fsys_vptr_cachep;
	}

	dmapi_init_procfs(dmapi_dev.minor);
	return 0;

 out_free_fsys_vptr_cachep:
	kmem_cache_destroy(dm_fsys_vptr_cachep);
 out_free_fsys_map_cachep:
	kmem_cache_destroy(dm_fsys_map_cachep);
 out_free_session_cachep:
	kmem_cache_destroy(dm_session_cachep);
 out_free_fsreg_cachep:
	kmem_cache_destroy(dm_fsreg_cachep);
 out_free_tokdata_cachep:
	kmem_cache_destroy(dm_tokdata_cachep);
 out:
	return -ENOMEM;
}

#if 0
void __exit dmapi_uninit(void)
{
	misc_deregister(&dmapi_dev);
	dmapi_cleanup_procfs();
	kmem_cache_destroy(dm_tokdata_cachep);
	kmem_cache_destroy(dm_fsreg_cachep);
	kmem_cache_destroy(dm_session_cachep);
	kmem_cache_destroy(dm_fsys_map_cachep);
	kmem_cache_destroy(dm_fsys_vptr_cachep);
}
#endif

module_init(dmapi_init);
/*module_exit(dmapi_uninit);*/ /* Some other day */

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("SGI Data Migration Subsystem");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(dm_send_mount_event);
EXPORT_SYMBOL(dm_send_namesp_event);
EXPORT_SYMBOL(dm_send_unmount_event);
EXPORT_SYMBOL(dm_send_data_event);
EXPORT_SYMBOL(dm_send_destroy_event);
EXPORT_SYMBOL(dm_ip_to_handle);
EXPORT_SYMBOL(dmapi_register);
EXPORT_SYMBOL(dmapi_unregister);
EXPORT_SYMBOL(dmapi_registered);
EXPORT_SYMBOL(dm_release_threads);
