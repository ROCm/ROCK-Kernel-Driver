/*
 * Copyright (c) 2004 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <linux/config.h>
#include <linux/compat.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/ioctl32.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <asm/uaccess.h>

#include "xfs_types.h"
#include "xfs_fs.h"
#include "xfs_dfrag.h"

#if defined(CONFIG_IA64) || defined(CONFIG_X86_64)
#define BROKEN_X86_ALIGNMENT
#else

typedef struct xfs_fsop_bulkreq32 {
	compat_uptr_t	lastip;		/* last inode # pointer		*/
	__s32		icount;		/* count of entries in buffer	*/
	compat_uptr_t	ubuffer;	/* user buffer for inode desc.	*/
	__s32		ocount;		/* output count pointer		*/
} xfs_fsop_bulkreq32_t;

static int
xfs_ioctl32_bulkstat(
	unsigned int		fd,
	unsigned int		cmd,
	unsigned long		arg,
	struct file *		file)
{
	xfs_fsop_bulkreq32_t	__user *p32 = (void __user *)arg;
	xfs_fsop_bulkreq_t	__user *p = compat_alloc_user_space(sizeof(*p));
	u32			addr;

	if (get_user(addr, &p32->lastip) ||
	    put_user(compat_ptr(addr), &p->lastip) ||
	    copy_in_user(&p->icount, &p32->icount, sizeof(s32)) ||
	    get_user(addr, &p32->ubuffer) ||
	    put_user(compat_ptr(addr), &p->ubuffer) ||
	    get_user(addr, &p32->ocount) ||
	    put_user(compat_ptr(addr), &p->ocount))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long)p);
}
#endif

struct ioctl_trans xfs_ioctl32_trans[] = {
	{ XFS_IOC_DIOINFO, },
	{ XFS_IOC_FSGEOMETRY_V1, },
	{ XFS_IOC_FSGEOMETRY, },
	{ XFS_IOC_GETVERSION, },
	{ XFS_IOC_GETXFLAGS, },
	{ XFS_IOC_SETXFLAGS, },
	{ XFS_IOC_FSGETXATTR, },
	{ XFS_IOC_FSSETXATTR, },
	{ XFS_IOC_FSGETXATTRA, },
	{ XFS_IOC_FSSETDM, },
	{ XFS_IOC_GETBMAP, },
	{ XFS_IOC_GETBMAPA, },
	{ XFS_IOC_GETBMAPX, },
/* not handled
	{ XFS_IOC_FD_TO_HANDLE, },
	{ XFS_IOC_PATH_TO_HANDLE, },
	{ XFS_IOC_PATH_TO_HANDLE, },
	{ XFS_IOC_PATH_TO_FSHANDLE, },
	{ XFS_IOC_OPEN_BY_HANDLE, },
	{ XFS_IOC_FSSETDM_BY_HANDLE, },
	{ XFS_IOC_READLINK_BY_HANDLE, },
	{ XFS_IOC_ATTRLIST_BY_HANDLE, },
	{ XFS_IOC_ATTRMULTI_BY_HANDLE, },
*/
	{ XFS_IOC_FSCOUNTS, NULL, },
	{ XFS_IOC_SET_RESBLKS, NULL, },
	{ XFS_IOC_GET_RESBLKS, NULL, },
	{ XFS_IOC_FSGROWFSDATA, NULL, },
	{ XFS_IOC_FSGROWFSLOG, NULL, },
	{ XFS_IOC_FSGROWFSRT, NULL, },
	{ XFS_IOC_FREEZE, NULL, },
	{ XFS_IOC_THAW, NULL, },
	{ XFS_IOC_GOINGDOWN, NULL, },
	{ XFS_IOC_ERROR_INJECTION, NULL, },
	{ XFS_IOC_ERROR_CLEARALL, NULL, },
#ifndef BROKEN_X86_ALIGNMENT
	/* xfs_flock_t and xfs_bstat_t have wrong u32 vs u64 alignment */
	{ XFS_IOC_ALLOCSP, },
	{ XFS_IOC_FREESP, },
	{ XFS_IOC_RESVSP, },
	{ XFS_IOC_UNRESVSP, },
	{ XFS_IOC_ALLOCSP64, },
	{ XFS_IOC_FREESP64, },
	{ XFS_IOC_RESVSP64, },
	{ XFS_IOC_UNRESVSP64, },
	{ XFS_IOC_SWAPEXT, },
	{ XFS_IOC_FSBULKSTAT_SINGLE, xfs_ioctl32_bulkstat },
	{ XFS_IOC_FSBULKSTAT, xfs_ioctl32_bulkstat},
	{ XFS_IOC_FSINUMBERS, xfs_ioctl32_bulkstat},
#endif
	{ 0, },
};

int __init
xfs_ioctl32_init(void)
{
	int error, i;

	for (i = 0; xfs_ioctl32_trans[i].cmd != 0; i++) {
		error = register_ioctl32_conversion(xfs_ioctl32_trans[i].cmd,
				xfs_ioctl32_trans[i].handler);
		if (error)
			goto fail;
	}

	return 0;

 fail:
	while (--i)
		unregister_ioctl32_conversion(xfs_ioctl32_trans[i].cmd);
	return error;
}

void
xfs_ioctl32_exit(void)
{
	int i;

	for (i = 0; xfs_ioctl32_trans[i].cmd != 0; i++)
		unregister_ioctl32_conversion(xfs_ioctl32_trans[i].cmd);
}
