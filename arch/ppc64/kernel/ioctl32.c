/* 
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 * 
 * Based on sparc64 ioctl32.c by:
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 * ppc64 changes:
 *
 * Copyright (C) 2000  Ken Aaker (kdaaker@rchland.vnet.ibm.com)
 * Copyright (C) 2001  Anton Blanchard (antonb@au.ibm.com)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define INCLUDES
#include "compat_ioctl.c"
#include <linux/ncp_fs.h>
#include <asm/ppc32.h>

#define CODE
#include "compat_ioctl.c"

struct ncp_ioctl_request_32 {
	unsigned int function;
	unsigned int size;
	compat_caddr_t data;
};

struct ncp_fs_info_v2_32 {
	int version;
	unsigned int mounted_uid;
	unsigned int connection;
	unsigned int buffer_size;

	unsigned int volume_number;
	__u32 directory_id;

	__u32 dummy1;
	__u32 dummy2;
	__u32 dummy3;
};

struct ncp_objectname_ioctl_32
{
	int		auth_type;
	unsigned int	object_name_len;
	compat_caddr_t	object_name;	/* an userspace data, in most cases user name */
};

struct ncp_privatedata_ioctl_32
{
	unsigned int	len;
	compat_caddr_t	data;		/* ~1000 for NDS */
};

#define	NCP_IOC_NCPREQUEST_32		_IOR('n', 1, struct ncp_ioctl_request_32)

#define NCP_IOC_GETMOUNTUID2_32		_IOW('n', 2, unsigned int)

#define NCP_IOC_GET_FS_INFO_V2_32	_IOWR('n', 4, struct ncp_fs_info_v2_32)

#define NCP_IOC_GETOBJECTNAME_32	_IOWR('n', 9, struct ncp_objectname_ioctl_32)
#define NCP_IOC_SETOBJECTNAME_32	_IOR('n', 9, struct ncp_objectname_ioctl_32)
#define NCP_IOC_GETPRIVATEDATA_32	_IOWR('n', 10, struct ncp_privatedata_ioctl_32)
#define NCP_IOC_SETPRIVATEDATA_32	_IOR('n', 10, struct ncp_privatedata_ioctl_32)

static int do_ncp_ncprequest(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_ioctl_request_32 n32;
	struct ncp_ioctl_request n;
	mm_segment_t old_fs;
	int err;

	if (copy_from_user(&n32, (struct ncp_ioctl_request_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.function = n32.function;
	n.size = n32.size;
	if (n.size > 65536)
		return -EINVAL;
	n.data = vmalloc(65536);	/* 65536 must be same as NCP_PACKET_SIZE_INTERNAL in ncpfs */
	if (!n.data)
		return -ENOMEM;
	err = -EFAULT;
	if (copy_from_user(n.data, (void *)A(n32.data), n.size))
		goto out;

	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_NCPREQUEST, (unsigned long)&n);
	set_fs (old_fs);
        if(err <= 0)
		goto out;
	if (err > 65536) {
		err = -EINVAL;
		goto out;
	}
	if (copy_to_user((void *)A(n32.data), n.data, err)) {
		err = -EFAULT;
		goto out;
	}
 out:
	vfree(n.data);
	return err;
}

static int do_ncp_getmountuid2(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	__kernel_uid_t kuid;
	int err;

	cmd = NCP_IOC_GETMOUNTUID2;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&kuid);
	set_fs(old_fs);

	if (!err)
		err = put_user(kuid, (unsigned int*)arg);

	return err;
}

static int do_ncp_getfsinfo2(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct ncp_fs_info_v2_32 n32;
	struct ncp_fs_info_v2 n;
	int err;

	if (copy_from_user(&n32, (struct ncp_fs_info_v2_32*)arg, sizeof(n32)))
		return -EFAULT;
	if (n32.version != NCP_GET_FS_INFO_VERSION_V2)
		return -EINVAL;
	n.version = NCP_GET_FS_INFO_VERSION_V2;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, NCP_IOC_GET_FS_INFO_V2, (unsigned long)&n);
	set_fs(old_fs);

	if (!err) {
		n32.version = n.version;
		n32.mounted_uid = n.mounted_uid;
		n32.connection = n.connection;
		n32.buffer_size = n.buffer_size;
		n32.volume_number = n.volume_number;
		n32.directory_id = n.directory_id;
		n32.dummy1 = n.dummy1;
		n32.dummy2 = n.dummy2;
		n32.dummy3 = n.dummy3;
		err = copy_to_user((struct ncp_fs_info_v2_32*)arg, &n32, sizeof(n32)) ? -EFAULT : 0;
	}
	return err;
}

static int do_ncp_getobjectname(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_objectname_ioctl_32 n32;
	struct ncp_objectname_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_objectname_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.object_name_len = tl = n32.object_name_len;
	if (tl) {
		n.object_name = kmalloc(tl, GFP_KERNEL);
		if (!n.object_name)
			return -ENOMEM;
	} else {
		n.object_name = NULL;
	}

	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_GETOBJECTNAME, (unsigned long)&n);
	set_fs (old_fs);
        if(err)
		goto out;
		
	if (tl > n.object_name_len)
		tl = n.object_name_len;

	err = -EFAULT;
	if (tl && copy_to_user((void *)A(n32.object_name), n.object_name, tl))
		goto out;

	n32.auth_type = n.auth_type;
	n32.object_name_len = n.object_name_len;
	
	if (copy_to_user((struct ncp_objectname_ioctl_32*)arg, &n32, sizeof(n32)))
		goto out;
	
	err = 0;
 out:
 	if (n.object_name)
		kfree(n.object_name);

	return err;
}

static int do_ncp_setobjectname(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_objectname_ioctl_32 n32;
	struct ncp_objectname_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_objectname_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.auth_type = n32.auth_type;
	n.object_name_len = tl = n32.object_name_len;
	if (tl) {
		n.object_name = kmalloc(tl, GFP_KERNEL);
		if (!n.object_name)
			return -ENOMEM;
		err = -EFAULT;
		if (copy_from_user(n.object_name, (void *)A(n32.object_name), tl))
			goto out;
	} else {
		n.object_name = NULL;
	}
	
	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_SETOBJECTNAME, (unsigned long)&n);
	set_fs (old_fs);
		
 out:
	if (n.object_name)
		kfree(n.object_name);

	return err;
}

static int do_ncp_getprivatedata(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_privatedata_ioctl_32 n32;
	struct ncp_privatedata_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_privatedata_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.len = tl = n32.len;
	if (tl) {
		n.data = kmalloc(tl, GFP_KERNEL);
		if (!n.data)
			return -ENOMEM;
	} else {
		n.data = NULL;
	}

	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_GETPRIVATEDATA, (unsigned long)&n);
	set_fs (old_fs);
        if(err)
		goto out;
		
	if (tl > n.len)
		tl = n.len;

	err = -EFAULT;
	if (tl && copy_to_user((void *)A(n32.data), n.data, tl))
		goto out;

	n32.len = n.len;
	
	if (copy_to_user((struct ncp_privatedata_ioctl_32*)arg, &n32, sizeof(n32)))
		goto out;
	
	err = 0;
 out:
 	if (n.data)
		kfree(n.data);

	return err;
}

static int do_ncp_setprivatedata(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_privatedata_ioctl_32 n32;
	struct ncp_privatedata_ioctl n;
	mm_segment_t old_fs;
	int err;
	size_t tl;

	if (copy_from_user(&n32, (struct ncp_privatedata_ioctl_32*)arg,
	    sizeof(n32)))
		return -EFAULT;

	n.len = tl = n32.len;
	if (tl) {
		n.data = kmalloc(tl, GFP_KERNEL);
		if (!n.data)
			return -ENOMEM;
		err = -EFAULT;
		if (copy_from_user(n.data, (void *)A(n32.data), tl))
			goto out;
	} else {
		n.data = NULL;
	}
	
	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, NCP_IOC_SETPRIVATEDATA, (unsigned long)&n);
	set_fs (old_fs);
		
 out:
	if (n.data)
		kfree(n.data);

	return err;
}


#define HANDLE_IOCTL(cmd,handler) { cmd, (ioctl_trans_handler_t)handler, 0 },
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd,sys_ioctl)

#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
COMPATIBLE_IOCTL(TCSBRKP)
COMPATIBLE_IOCTL(TIOCSTART)
COMPATIBLE_IOCTL(TIOCSTOP)
COMPATIBLE_IOCTL(TIOCSLTC)
#if 0
COMPATIBLE_IOCTL(FBIOBLANK)
#endif
/* Little p (/dev/rtc, /dev/envctrl, etc.) */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7])) /* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7])) /* RTCSET */

/* And these ioctls need translation */

/* NCPFS */
HANDLE_IOCTL(NCP_IOC_NCPREQUEST_32, do_ncp_ncprequest)
HANDLE_IOCTL(NCP_IOC_GETMOUNTUID2_32, do_ncp_getmountuid2)
HANDLE_IOCTL(NCP_IOC_GET_FS_INFO_V2_32, do_ncp_getfsinfo2)
HANDLE_IOCTL(NCP_IOC_GETOBJECTNAME_32, do_ncp_getobjectname)
HANDLE_IOCTL(NCP_IOC_SETOBJECTNAME_32, do_ncp_setobjectname)
HANDLE_IOCTL(NCP_IOC_GETPRIVATEDATA_32, do_ncp_getprivatedata)
HANDLE_IOCTL(NCP_IOC_SETPRIVATEDATA_32, do_ncp_setprivatedata)

IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
