/*
 * IA32 Architecture-specific ioctl shim code
 *
 * Copyright (C) 2000 VA Linux Co
 * Copyright (C) 2000 Don Dugger <n0ano@valinux.com>
 * Copyright (C) 2001-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/signal.h>	/* argh, msdos_fs.h isn't self-contained... */
#include "ia32priv.h"
  
#define	INCLUDES
#include "compat_ioctl.c"
#include <asm/ioctl32.h>

#define IOCTL_NR(a)	((a) & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT))

#define DO_IOCTL(fd, cmd, arg) ({			\
	int _ret;					\
	mm_segment_t _old_fs = get_fs();		\
							\
	set_fs(KERNEL_DS);				\
	_ret = sys_ioctl(fd, cmd, (unsigned long)arg);	\
	set_fs(_old_fs);				\
	_ret;						\
})

#define P(i)	((void *)(unsigned long)(i))

asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

#define CODE
#include "compat_ioctl.c"

#define	VFAT_IOCTL_READDIR_BOTH32	_IOR('r', 1, struct linux32_dirent[2])
#define	VFAT_IOCTL_READDIR_SHORT32	_IOR('r', 2, struct linux32_dirent[2])

static long
put_dirent32 (struct dirent *d, struct linux32_dirent *d32)
{
	size_t namelen = strlen(d->d_name);

	return (put_user(d->d_ino, &d32->d_ino)
		|| put_user(d->d_off, &d32->d_off)
		|| put_user(d->d_reclen, &d32->d_reclen)
		|| copy_to_user(d32->d_name, d->d_name, namelen + 1));
}

static int vfat_ioctl32(unsigned fd, unsigned cmd,  void *ptr) 
{
	int ret;
	mm_segment_t oldfs = get_fs();
	struct dirent d[2]; 

	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd,cmd,(unsigned long)&d); 
	set_fs(oldfs); 
	if (!ret) { 
		ret |= put_dirent32(&d[0], (struct linux32_dirent *)ptr); 
		ret |= put_dirent32(&d[1], ((struct linux32_dirent *)ptr) + 1); 
	}
	return ret; 
} 

typedef int (* ioctl32_handler_t)(unsigned int, unsigned int, unsigned long, struct file *);

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl32_handler_t)(handler), NULL },
#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START
HANDLE_IOCTL(VFAT_IOCTL_READDIR_BOTH32, vfat_ioctl32)
HANDLE_IOCTL(VFAT_IOCTL_READDIR_SHORT32, vfat_ioctl32)
#define DECLARES
#include "compat_ioctl.c"
#include <linux/compat_ioctl.h>
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
