/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <linux/string.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include <xfs_types.h>
#include "debug.h"
#include "move.h"

/* Read from kernel buffer at src to user/kernel buffer defined
 * by the uio structure. Advance the pointer in the uio struct
 * as we go.
 */
int
uio_read(caddr_t src, size_t len, struct uio *uio)
{
	struct iovec *iov;
	u_int cnt;
	int error;

	if (len > 0 && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = (u_int)iov->iov_len;
		if (cnt == 0)
			return 0;
		if (cnt > len)
			cnt = (u_int)len;
		if (uio->uio_segflg == UIO_USERSPACE) {
			error = copy_to_user(iov->iov_base, src, cnt);
			if (error)
				return EFAULT;
		} else if (uio->uio_segflg == UIO_SYSSPACE) {
			memcpy(iov->iov_base, src, cnt);
		} else {
			ASSERT(0);
		}
		iov->iov_base = (void *)((char *)iov->iov_base + cnt);
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_offset += cnt;
	}
	return 0;
}
