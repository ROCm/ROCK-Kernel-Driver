/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) IBM Corp. 2006
 *
 * Authors: Jimi Xenidis <jimix@watson.ibm.com>
 */

#include <linux/compat.h>
#include <linux/ioctl.h>
#include <linux/syscalls.h>
#include <asm/hypervisor.h>
#include <asm/uaccess.h>
#include <xen/public/privcmd.h>
#include <xen/compat_ioctl.h>

int privcmd_ioctl_32(int fd, unsigned int cmd, unsigned long arg)
{
	int ret;

	switch (cmd) {
	case IOCTL_PRIVCMD_MMAP_32: {
		struct privcmd_mmap *p;
		struct privcmd_mmap_32 *p32;
		struct privcmd_mmap_32 n32;

		p32 = compat_ptr(arg);
		p = compat_alloc_user_space(sizeof(*p));
		if (copy_from_user(&n32, p32, sizeof(n32)) ||
		    put_user(n32.num, &p->num) ||
		    put_user(n32.dom, &p->dom) ||
		    put_user(compat_ptr(n32.entry), &p->entry))
			return -EFAULT;
		
		ret = sys_ioctl(fd, IOCTL_PRIVCMD_MMAP, (unsigned long)p);
	}
		break;
	case IOCTL_PRIVCMD_MMAPBATCH_32: {
		struct privcmd_mmapbatch *p;
		struct privcmd_mmapbatch_32 *p32;
		struct privcmd_mmapbatch_32 n32;

		p32 = compat_ptr(arg);
		p = compat_alloc_user_space(sizeof(*p));
		if (copy_from_user(&n32, p32, sizeof(n32)) ||
		    put_user(n32.num, &p->num) ||
		    put_user(n32.dom, &p->dom) ||
		    put_user(n32.addr, &p->addr) ||
		    put_user(compat_ptr(n32.arr), &p->arr))
			return -EFAULT;
		
		ret = sys_ioctl(fd, IOCTL_PRIVCMD_MMAPBATCH, (unsigned long)p);
	}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
