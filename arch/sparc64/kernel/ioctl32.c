/* $Id: ioctl32.c,v 1.136 2002/01/14 09:49:52 davem Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2003  Pavel Machek (pavel@suse.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#define INCLUDES
#include "compat_ioctl.c"
#include <linux/ncp_fs.h>
#include <asm/fbio.h>
#include <asm/kbio.h>
#include <asm/vuid_event.h>
#include <asm/envctrl.h>
#include <asm/display7seg.h>
#include <asm/openpromio.h>
#include <asm/audioio.h>
#include <asm/watchdog.h>

/* Use this to get at 32-bit user passed pointers. 
 * See sys_sparc32.c for description about it.
 */
#define A(__x) ((void __user *)(unsigned long)(__x))

static __inline__ void *alloc_user_space(long len)
{
	struct pt_regs *regs = current_thread_info()->kregs;
	unsigned long usp = regs->u_regs[UREG_I6];

	if (!(test_thread_flag(TIF_32BIT)))
		usp += STACK_BIAS;

	return (void *) (usp - len);
}

#define CODE
#include "compat_ioctl.c"

struct hd_big_geometry32 {
	unsigned char heads;
	unsigned char sectors;
	unsigned int cylinders;
	u32 start;
};
                        
static int hdio_getgeo_big(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct hd_big_geometry geo;
	int err;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&geo);
	set_fs (old_fs);
	if (!err) {
		struct hd_big_geometry32 *up = (struct hd_big_geometry32 *) arg;

		if (put_user(geo.heads, &up->heads) ||
		    __put_user(geo.sectors, &up->sectors) ||
		    __put_user(geo.cylinders, &up->cylinders) ||
		    __put_user(((u32) geo.start), &up->start))
			err = -EFAULT;
	}
	return err;
}

struct  fbcmap32 {
	int             index;          /* first element (0 origin) */
	int             count;
	u32		red;
	u32		green;
	u32		blue;
};

#define FBIOPUTCMAP32	_IOW('F', 3, struct fbcmap32)
#define FBIOGETCMAP32	_IOW('F', 4, struct fbcmap32)

static int fbiogetputcmap(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fbcmap f;
	int ret;
	char red[256], green[256], blue[256];
	u32 r, g, b;
	mm_segment_t old_fs = get_fs();
	
	ret = get_user(f.index, &(((struct fbcmap32 *)arg)->index));
	ret |= __get_user(f.count, &(((struct fbcmap32 *)arg)->count));
	ret |= __get_user(r, &(((struct fbcmap32 *)arg)->red));
	ret |= __get_user(g, &(((struct fbcmap32 *)arg)->green));
	ret |= __get_user(b, &(((struct fbcmap32 *)arg)->blue));
	if (ret)
		return -EFAULT;
	if ((f.index < 0) || (f.index > 255)) return -EINVAL;
	if (f.index + f.count > 256)
		f.count = 256 - f.index;
	if (cmd == FBIOPUTCMAP32) {
		ret = copy_from_user (red, A(r), f.count);
		ret |= copy_from_user (green, A(g), f.count);
		ret |= copy_from_user (blue, A(b), f.count);
		if (ret)
			return -EFAULT;
	}
	f.red = red; f.green = green; f.blue = blue;
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, (cmd == FBIOPUTCMAP32) ? FBIOPUTCMAP_SPARC : FBIOGETCMAP_SPARC, (long)&f);
	set_fs (old_fs);
	if (!ret && cmd == FBIOGETCMAP32) {
		ret = copy_to_user (A(r), red, f.count);
		ret |= copy_to_user (A(g), green, f.count);
		ret |= copy_to_user (A(b), blue, f.count);
	}
	return ret ? -EFAULT : 0;
}

struct fbcursor32 {
	short set;		/* what to set, choose from the list above */
	short enable;		/* cursor on/off */
	struct fbcurpos pos;	/* cursor position */
	struct fbcurpos hot;	/* cursor hot spot */
	struct fbcmap32 cmap;	/* color map info */
	struct fbcurpos size;	/* cursor bit map size */
	u32	image;		/* cursor image bits */
	u32	mask;		/* cursor mask bits */
};
	
#define FBIOSCURSOR32	_IOW('F', 24, struct fbcursor32)
#define FBIOGCURSOR32	_IOW('F', 25, struct fbcursor32)

static int fbiogscursor(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fbcursor f;
	int ret;
	char red[2], green[2], blue[2];
	char image[128], mask[128];
	u32 r, g, b;
	u32 m, i;
	mm_segment_t old_fs = get_fs();
	
	ret = copy_from_user (&f, (struct fbcursor32 *)arg, 2 * sizeof (short) + 2 * sizeof(struct fbcurpos));
	ret |= __get_user(f.size.x, &(((struct fbcursor32 *)arg)->size.x));
	ret |= __get_user(f.size.y, &(((struct fbcursor32 *)arg)->size.y));
	ret |= __get_user(f.cmap.index, &(((struct fbcursor32 *)arg)->cmap.index));
	ret |= __get_user(f.cmap.count, &(((struct fbcursor32 *)arg)->cmap.count));
	ret |= __get_user(r, &(((struct fbcursor32 *)arg)->cmap.red));
	ret |= __get_user(g, &(((struct fbcursor32 *)arg)->cmap.green));
	ret |= __get_user(b, &(((struct fbcursor32 *)arg)->cmap.blue));
	ret |= __get_user(m, &(((struct fbcursor32 *)arg)->mask));
	ret |= __get_user(i, &(((struct fbcursor32 *)arg)->image));
	if (ret)
		return -EFAULT;
	if (f.set & FB_CUR_SETCMAP) {
		if ((uint) f.size.y > 32)
			return -EINVAL;
		ret = copy_from_user (mask, A(m), f.size.y * 4);
		ret |= copy_from_user (image, A(i), f.size.y * 4);
		if (ret)
			return -EFAULT;
		f.image = image; f.mask = mask;
	}
	if (f.set & FB_CUR_SETCMAP) {
		ret = copy_from_user (red, A(r), 2);
		ret |= copy_from_user (green, A(g), 2);
		ret |= copy_from_user (blue, A(b), 2);
		if (ret)
			return -EFAULT;
		f.cmap.red = red; f.cmap.green = green; f.cmap.blue = blue;
	}
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, FBIOSCURSOR, (long)&f);
	set_fs (old_fs);
	return ret;
}

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
	if (copy_from_user(n.data, A(n32.data), n.size))
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
	if (copy_to_user(A(n32.data), n.data, err)) {
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
	if (tl && copy_to_user(A(n32.object_name), n.object_name, tl))
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
		if (copy_from_user(n.object_name, A(n32.object_name), tl))
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
	if (tl && copy_to_user(A(n32.data), n.data, tl))
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
		if (copy_from_user(n.data, A(n32.data), tl))
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

#if defined(CONFIG_DRM) || defined(CONFIG_DRM_MODULE)
/* This really belongs in include/linux/drm.h -DaveM */
#include "../../../drivers/char/drm/drm.h"

typedef struct drm32_version {
	int    version_major;	  /* Major version			    */
	int    version_minor;	  /* Minor version			    */
	int    version_patchlevel;/* Patch level			    */
	int    name_len;	  /* Length of name buffer		    */
	u32    name;		  /* Name of driver			    */
	int    date_len;	  /* Length of date buffer		    */
	u32    date;		  /* User-space buffer to hold date	    */
	int    desc_len;	  /* Length of desc buffer		    */
	u32    desc;		  /* User-space buffer to hold desc	    */
} drm32_version_t;
#define DRM32_IOCTL_VERSION    DRM_IOWR(0x00, drm32_version_t)

static int drm32_version(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_version_t *uversion = (drm32_version_t *)arg;
	char __user *name_ptr, *date_ptr, *desc_ptr;
	u32 tmp1, tmp2, tmp3;
	drm_version_t kversion;
	mm_segment_t old_fs;
	int ret;

	memset(&kversion, 0, sizeof(kversion));
	if (get_user(kversion.name_len, &uversion->name_len) ||
	    get_user(kversion.date_len, &uversion->date_len) ||
	    get_user(kversion.desc_len, &uversion->desc_len) ||
	    get_user(tmp1, &uversion->name) ||
	    get_user(tmp2, &uversion->date) ||
	    get_user(tmp3, &uversion->desc))
		return -EFAULT;

	name_ptr = A(tmp1);
	date_ptr = A(tmp2);
	desc_ptr = A(tmp3);

	ret = -ENOMEM;
	if (kversion.name_len && name_ptr) {
		kversion.name = kmalloc(kversion.name_len, GFP_KERNEL);
		if (!kversion.name)
			goto out;
	}
	if (kversion.date_len && date_ptr) {
		kversion.date = kmalloc(kversion.date_len, GFP_KERNEL);
		if (!kversion.date)
			goto out;
	}
	if (kversion.desc_len && desc_ptr) {
		kversion.desc = kmalloc(kversion.desc_len, GFP_KERNEL);
		if (!kversion.desc)
			goto out;
	}

        old_fs = get_fs();
	set_fs(KERNEL_DS);
        ret = sys_ioctl (fd, DRM_IOCTL_VERSION, (unsigned long)&kversion);
        set_fs(old_fs);

	if (!ret) {
		if ((kversion.name &&
		     copy_to_user(name_ptr, kversion.name, kversion.name_len)) ||
		    (kversion.date &&
		     copy_to_user(date_ptr, kversion.date, kversion.date_len)) ||
		    (kversion.desc &&
		     copy_to_user(desc_ptr, kversion.desc, kversion.desc_len)))
			ret = -EFAULT;
		if (put_user(kversion.version_major, &uversion->version_major) ||
		    put_user(kversion.version_minor, &uversion->version_minor) ||
		    put_user(kversion.version_patchlevel, &uversion->version_patchlevel) ||
		    put_user(kversion.name_len, &uversion->name_len) ||
		    put_user(kversion.date_len, &uversion->date_len) ||
		    put_user(kversion.desc_len, &uversion->desc_len))
			ret = -EFAULT;
	}

out:
	if (kversion.name)
		kfree(kversion.name);
	if (kversion.date)
		kfree(kversion.date);
	if (kversion.desc)
		kfree(kversion.desc);
	return ret;
}

typedef struct drm32_unique {
	int	unique_len;	  /* Length of unique			    */
	u32	unique;		  /* Unique name for driver instantiation   */
} drm32_unique_t;
#define DRM32_IOCTL_GET_UNIQUE DRM_IOWR(0x01, drm32_unique_t)
#define DRM32_IOCTL_SET_UNIQUE DRM_IOW( 0x10, drm32_unique_t)

static int drm32_getsetunique(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_unique_t *uarg = (drm32_unique_t *)arg;
	drm_unique_t karg;
	mm_segment_t old_fs;
	char __user *uptr;
	u32 tmp;
	int ret;

	if (get_user(karg.unique_len, &uarg->unique_len))
		return -EFAULT;
	karg.unique = NULL;

	if (get_user(tmp, &uarg->unique))
		return -EFAULT;

	uptr = A(tmp);

	if (uptr) {
		karg.unique = kmalloc(karg.unique_len, GFP_KERNEL);
		if (!karg.unique)
			return -ENOMEM;
		if (cmd == DRM32_IOCTL_SET_UNIQUE &&
		    copy_from_user(karg.unique, uptr, karg.unique_len)) {
			kfree(karg.unique);
			return -EFAULT;
		}
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	if (cmd == DRM32_IOCTL_GET_UNIQUE)
		ret = sys_ioctl (fd, DRM_IOCTL_GET_UNIQUE, (unsigned long)&karg);
	else
		ret = sys_ioctl (fd, DRM_IOCTL_SET_UNIQUE, (unsigned long)&karg);
        set_fs(old_fs);

	if (!ret) {
		if (cmd == DRM32_IOCTL_GET_UNIQUE &&
		    uptr != NULL &&
		    copy_to_user(uptr, karg.unique, karg.unique_len))
			ret = -EFAULT;
		if (put_user(karg.unique_len, &uarg->unique_len))
			ret = -EFAULT;
	}

	if (karg.unique != NULL)
		kfree(karg.unique);

	return ret;
}

typedef struct drm32_map {
	u32		offset;	 /* Requested physical address (0 for SAREA)*/
	u32		size;	 /* Requested physical size (bytes)	    */
	drm_map_type_t	type;	 /* Type of memory to map		    */
	drm_map_flags_t flags;	 /* Flags				    */
	u32		handle;  /* User-space: "Handle" to pass to mmap    */
				 /* Kernel-space: kernel-virtual address    */
	int		mtrr;	 /* MTRR slot used			    */
				 /* Private data			    */
} drm32_map_t;
#define DRM32_IOCTL_ADD_MAP    DRM_IOWR(0x15, drm32_map_t)

static int drm32_addmap(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_map_t *uarg = (drm32_map_t *) arg;
	drm_map_t karg;
	mm_segment_t old_fs;
	u32 tmp;
	int ret;

	ret  = get_user(karg.offset, &uarg->offset);
	ret |= get_user(karg.size, &uarg->size);
	ret |= get_user(karg.type, &uarg->type);
	ret |= get_user(karg.flags, &uarg->flags);
	ret |= get_user(tmp, &uarg->handle);
	ret |= get_user(karg.mtrr, &uarg->mtrr);
	if (ret)
		return -EFAULT;

	karg.handle = A(tmp);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_ADD_MAP, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		ret  = put_user(karg.offset, &uarg->offset);
		ret |= put_user(karg.size, &uarg->size);
		ret |= put_user(karg.type, &uarg->type);
		ret |= put_user(karg.flags, &uarg->flags);
		tmp = (u32) (long)karg.handle;
		ret |= put_user(tmp, &uarg->handle);
		ret |= put_user(karg.mtrr, &uarg->mtrr);
		if (ret)
			ret = -EFAULT;
	}

	return ret;
}

typedef struct drm32_buf_info {
	int	       count;	/* Entries in list			     */
	u32	       list;    /* (drm_buf_desc_t *) */ 
} drm32_buf_info_t;
#define DRM32_IOCTL_INFO_BUFS  DRM_IOWR(0x18, drm32_buf_info_t)

static int drm32_info_bufs(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_buf_info_t *uarg = (drm32_buf_info_t *)arg;
	drm_buf_desc_t __user *ulist;
	drm_buf_info_t karg;
	mm_segment_t old_fs;
	int orig_count, ret;
	u32 tmp;

	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp, &uarg->list))
		return -EFAULT;

	ulist = A(tmp);

	orig_count = karg.count;

	karg.list = kmalloc(karg.count * sizeof(drm_buf_desc_t), GFP_KERNEL);
	if (!karg.list)
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_INFO_BUFS, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		if (karg.count <= orig_count &&
		    (copy_to_user(ulist, karg.list,
				  karg.count * sizeof(drm_buf_desc_t))))
			ret = -EFAULT;
		if (put_user(karg.count, &uarg->count))
			ret = -EFAULT;
	}

	kfree(karg.list);

	return ret;
}

typedef struct drm32_buf_free {
	int	       count;
	u32	       list;	/* (int *) */
} drm32_buf_free_t;
#define DRM32_IOCTL_FREE_BUFS  DRM_IOW( 0x1a, drm32_buf_free_t)

static int drm32_free_bufs(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_buf_free_t *uarg = (drm32_buf_free_t *)arg;
	drm_buf_free_t karg;
	mm_segment_t old_fs;
	int __user *ulist;
	int ret;
	u32 tmp;

	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp, &uarg->list))
		return -EFAULT;

	ulist = A(tmp);

	karg.list = kmalloc(karg.count * sizeof(int), GFP_KERNEL);
	if (!karg.list)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(karg.list, ulist, (karg.count * sizeof(int))))
		goto out;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_FREE_BUFS, (unsigned long) &karg);
	set_fs(old_fs);

out:
	kfree(karg.list);

	return ret;
}

typedef struct drm32_buf_pub {
	int		  idx;	       /* Index into master buflist	     */
	int		  total;       /* Buffer size			     */
	int		  used;	       /* Amount of buffer in use (for DMA)  */
	u32		  address;     /* Address of buffer (void *)	     */
} drm32_buf_pub_t;

typedef struct drm32_buf_map {
	int	      count;	/* Length of buflist			    */
	u32	      virtual;	/* Mmaped area in user-virtual (void *)	    */
	u32 	      list;	/* Buffer information (drm_buf_pub_t *)	    */
} drm32_buf_map_t;
#define DRM32_IOCTL_MAP_BUFS   DRM_IOWR(0x19, drm32_buf_map_t)

static int drm32_map_bufs(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_buf_map_t *uarg = (drm32_buf_map_t *)arg;
	drm32_buf_pub_t __user *ulist;
	drm_buf_map_t karg;
	mm_segment_t old_fs;
	int orig_count, ret, i;
	u32 tmp1, tmp2;

	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp1, &uarg->virtual) ||
	    get_user(tmp2, &uarg->list))
		return -EFAULT;

	karg.virtual = A(tmp1);
	ulist = A(tmp2);

	orig_count = karg.count;

	karg.list = kmalloc(karg.count * sizeof(drm_buf_pub_t), GFP_KERNEL);
	if (!karg.list)
		return -ENOMEM;

	ret = -EFAULT;
	for (i = 0; i < karg.count; i++) {
		if (get_user(karg.list[i].idx, &ulist[i].idx) ||
		    get_user(karg.list[i].total, &ulist[i].total) ||
		    get_user(karg.list[i].used, &ulist[i].used) ||
		    get_user(tmp1, &ulist[i].address))
			goto out;

		karg.list[i].address = A(tmp1);
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_MAP_BUFS, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		for (i = 0; i < orig_count; i++) {
			tmp1 = (u32) (long) karg.list[i].address;
			if (put_user(karg.list[i].idx, &ulist[i].idx) ||
			    put_user(karg.list[i].total, &ulist[i].total) ||
			    put_user(karg.list[i].used, &ulist[i].used) ||
			    put_user(tmp1, &ulist[i].address)) {
				ret = -EFAULT;
				goto out;
			}
		}
		if (put_user(karg.count, &uarg->count))
			ret = -EFAULT;
	}

out:
	kfree(karg.list);
	return ret;
}

typedef struct drm32_dma {
				/* Indices here refer to the offset into
				   buflist in drm_buf_get_t.  */
	int		context;	  /* Context handle		    */
	int		send_count;	  /* Number of buffers to send	    */
	u32		send_indices;	  /* List of handles to buffers (int *) */
	u32		send_sizes;	  /* Lengths of data to send (int *) */
	drm_dma_flags_t flags;		  /* Flags			    */
	int		request_count;	  /* Number of buffers requested    */
	int		request_size;	  /* Desired size for buffers	    */
	u32		request_indices;  /* Buffer information (int *)	    */
	u32		request_sizes;    /* (int *) */
	int		granted_count;	  /* Number of buffers granted	    */
} drm32_dma_t;
#define DRM32_IOCTL_DMA	     DRM_IOWR(0x29, drm32_dma_t)

/* RED PEN	The DRM layer blindly dereferences the send/request
 * 		index/size arrays even though they are userland
 * 		pointers.  -DaveM
 */
static int drm32_dma(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_dma_t *uarg = (drm32_dma_t *) arg;
	int __user *u_si, *u_ss, *u_ri, *u_rs;
	drm_dma_t karg;
	mm_segment_t old_fs;
	int ret;
	u32 tmp1, tmp2, tmp3, tmp4;

	karg.send_indices = karg.send_sizes = NULL;
	karg.request_indices = karg.request_sizes = NULL;

	if (get_user(karg.context, &uarg->context) ||
	    get_user(karg.send_count, &uarg->send_count) ||
	    get_user(tmp1, &uarg->send_indices) ||
	    get_user(tmp2, &uarg->send_sizes) ||
	    get_user(karg.flags, &uarg->flags) ||
	    get_user(karg.request_count, &uarg->request_count) ||
	    get_user(karg.request_size, &uarg->request_size) ||
	    get_user(tmp3, &uarg->request_indices) ||
	    get_user(tmp4, &uarg->request_sizes) ||
	    get_user(karg.granted_count, &uarg->granted_count))
		return -EFAULT;

	u_si = A(tmp1);
	u_ss = A(tmp2);
	u_ri = A(tmp3);
	u_rs = A(tmp4);

	if (karg.send_count) {
		karg.send_indices = kmalloc(karg.send_count * sizeof(int), GFP_KERNEL);
		karg.send_sizes = kmalloc(karg.send_count * sizeof(int), GFP_KERNEL);

		ret = -ENOMEM;
		if (!karg.send_indices || !karg.send_sizes)
			goto out;

		ret = -EFAULT;
		if (copy_from_user(karg.send_indices, u_si,
				   (karg.send_count * sizeof(int))) ||
		    copy_from_user(karg.send_sizes, u_ss,
				   (karg.send_count * sizeof(int))))
			goto out;
	}

	if (karg.request_count) {
		karg.request_indices = kmalloc(karg.request_count * sizeof(int), GFP_KERNEL);
		karg.request_sizes = kmalloc(karg.request_count * sizeof(int), GFP_KERNEL);

		ret = -ENOMEM;
		if (!karg.request_indices || !karg.request_sizes)
			goto out;

		ret = -EFAULT;
		if (copy_from_user(karg.request_indices, u_ri,
				   (karg.request_count * sizeof(int))) ||
		    copy_from_user(karg.request_sizes, u_rs,
				   (karg.request_count * sizeof(int))))
			goto out;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_DMA, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		if (put_user(karg.context, &uarg->context) ||
		    put_user(karg.send_count, &uarg->send_count) ||
		    put_user(karg.flags, &uarg->flags) ||
		    put_user(karg.request_count, &uarg->request_count) ||
		    put_user(karg.request_size, &uarg->request_size) ||
		    put_user(karg.granted_count, &uarg->granted_count))
			ret = -EFAULT;

		if (karg.send_count) {
			if (copy_to_user(u_si, karg.send_indices,
					 (karg.send_count * sizeof(int))) ||
			    copy_to_user(u_ss, karg.send_sizes,
					 (karg.send_count * sizeof(int))))
				ret = -EFAULT;
		}
		if (karg.request_count) {
			if (copy_to_user(u_ri, karg.request_indices,
					 (karg.request_count * sizeof(int))) ||
			    copy_to_user(u_rs, karg.request_sizes,
					 (karg.request_count * sizeof(int))))
				ret = -EFAULT;
		}
	}

out:
	if (karg.send_indices)
		kfree(karg.send_indices);
	if (karg.send_sizes)
		kfree(karg.send_sizes);
	if (karg.request_indices)
		kfree(karg.request_indices);
	if (karg.request_sizes)
		kfree(karg.request_sizes);

	return ret;
}

typedef struct drm32_ctx_res {
	int		count;
	u32		contexts; /* (drm_ctx_t *) */
} drm32_ctx_res_t;
#define DRM32_IOCTL_RES_CTX    DRM_IOWR(0x26, drm32_ctx_res_t)

static int drm32_res_ctx(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	drm32_ctx_res_t *uarg = (drm32_ctx_res_t *) arg;
	drm_ctx_t __user *ulist;
	drm_ctx_res_t karg;
	mm_segment_t old_fs;
	int orig_count, ret;
	u32 tmp;

	karg.contexts = NULL;
	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp, &uarg->contexts))
		return -EFAULT;

	ulist = A(tmp);

	orig_count = karg.count;
	if (karg.count && ulist) {
		karg.contexts = kmalloc((karg.count * sizeof(drm_ctx_t)), GFP_KERNEL);
		if (!karg.contexts)
			return -ENOMEM;
		if (copy_from_user(karg.contexts, ulist,
				   (karg.count * sizeof(drm_ctx_t)))) {
			kfree(karg.contexts);
			return -EFAULT;
		}
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, DRM_IOCTL_RES_CTX, (unsigned long) &karg);
	set_fs(old_fs);

	if (!ret) {
		if (orig_count) {
			if (copy_to_user(ulist, karg.contexts,
					 (orig_count * sizeof(drm_ctx_t))))
				ret = -EFAULT;
		}
		if (put_user(karg.count, &uarg->count))
			ret = -EFAULT;
	}

	if (karg.contexts)
		kfree(karg.contexts);

	return ret;
}

#endif

/* HERE! */

struct usbdevfs_ctrltransfer32 {
	__u8 bRequestType;
	__u8 bRequest;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;
	__u32 timeout;  /* in milliseconds */
	__u32 data;
};

#define USBDEVFS_CONTROL32           _IOWR('U', 0, struct usbdevfs_ctrltransfer32)

static int do_usbdevfs_control(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_ctrltransfer kctrl;
	struct usbdevfs_ctrltransfer32 *uctrl;
	mm_segment_t old_fs;
	__u32 udata;
	void __user *uptr;
	void *kptr;
	int err;

	uctrl = (struct usbdevfs_ctrltransfer32 *) arg;

	if (copy_from_user(&kctrl, uctrl,
			   (sizeof(struct usbdevfs_ctrltransfer) -
			    sizeof(void *))))
		return -EFAULT;

	if (get_user(udata, &uctrl->data))
		return -EFAULT;
	uptr = A(udata);

	/* In usbdevice_fs, it limits the control buffer to a page,
	 * for simplicity so do we.
	 */
	if (!uptr || kctrl.wLength > PAGE_SIZE)
		return -EINVAL;

	kptr = (void *)__get_free_page(GFP_KERNEL);

	if ((kctrl.bRequestType & 0x80) == 0) {
		err = -EFAULT;
		if (copy_from_user(kptr, uptr, kctrl.wLength))
			goto out;
	}

	kctrl.data = kptr;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_CONTROL, (unsigned long)&kctrl);
	set_fs(old_fs);

	if (err >= 0 &&
	    ((kctrl.bRequestType & 0x80) != 0)) {
		if (copy_to_user(uptr, kptr, kctrl.wLength))
			err = -EFAULT;
	}

out:
	free_page((unsigned long) kptr);
	return err;
}

struct usbdevfs_bulktransfer32 {
	unsigned int ep;
	unsigned int len;
	unsigned int timeout; /* in milliseconds */
	__u32 data;
};

#define USBDEVFS_BULK32              _IOWR('U', 2, struct usbdevfs_bulktransfer32)

static int do_usbdevfs_bulk(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_bulktransfer kbulk;
	struct usbdevfs_bulktransfer32 *ubulk;
	mm_segment_t old_fs;
	__u32 udata;
	void __user *uptr;
	void *kptr;
	int err;

	ubulk = (struct usbdevfs_bulktransfer32 *) arg;

	if (get_user(kbulk.ep, &ubulk->ep) ||
	    get_user(kbulk.len, &ubulk->len) ||
	    get_user(kbulk.timeout, &ubulk->timeout) ||
	    get_user(udata, &ubulk->data))
		return -EFAULT;

	uptr = A(udata);

	/* In usbdevice_fs, it limits the control buffer to a page,
	 * for simplicity so do we.
	 */
	if (!uptr || kbulk.len > PAGE_SIZE)
		return -EINVAL;

	kptr = (void *) __get_free_page(GFP_KERNEL);

	if ((kbulk.ep & 0x80) == 0) {
		err = -EFAULT;
		if (copy_from_user(kptr, uptr, kbulk.len))
			goto out;
	}

	kbulk.data = kptr;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_BULK, (unsigned long) &kbulk);
	set_fs(old_fs);

	if (err >= 0 &&
	    ((kbulk.ep & 0x80) != 0)) {
		if (copy_to_user(uptr, kptr, kbulk.len))
			err = -EFAULT;
	}

out:
	free_page((unsigned long) kptr);
	return err;
}

/* This needs more work before we can enable it.  Unfortunately
 * because of the fancy asynchronous way URB status/error is written
 * back to userspace, we'll need to fiddle with USB devio internals
 * and/or reimplement entirely the frontend of it ourselves. -DaveM
 *
 * The issue is:
 *
 *	When an URB is submitted via usbdevicefs it is put onto an
 *	asynchronous queue.  When the URB completes, it may be reaped
 *	via another ioctl.  During this reaping the status is written
 *	back to userspace along with the length of the transfer.
 *
 *	We must translate into 64-bit kernel types so we pass in a kernel
 *	space copy of the usbdevfs_urb structure.  This would mean that we
 *	must do something to deal with the async entry reaping.  First we
 *	have to deal somehow with this transitory memory we've allocated.
 *	This is problematic since there are many call sites from which the
 *	async entries can be destroyed (and thus when we'd need to free up
 *	this kernel memory).  One of which is the close() op of usbdevicefs.
 *	To handle that we'd need to make our own file_operations struct which
 *	overrides usbdevicefs's release op with our own which runs usbdevicefs's
 *	real release op then frees up the kernel memory.
 *
 *	But how to keep track of these kernel buffers?  We'd need to either
 *	keep track of them in some table _or_ know about usbdevicefs internals
 *	(ie. the exact layout of its file private, which is actually defined
 *	in linux/usbdevice_fs.h, the layout of the async queues are private to
 *	devio.c)
 *
 * There is one possible other solution I considered, also involving knowledge
 * of usbdevicefs internals:
 *
 *	After an URB is submitted, we "fix up" the address back to the user
 *	space one.  This would work if the status/length fields written back
 *	by the async URB completion lines up perfectly in the 32-bit type with
 *	the 64-bit kernel type.  Unfortunately, it does not because the iso
 *	frame descriptors, at the end of the struct, can be written back.
 *
 * I think we'll just need to simply duplicate the devio URB engine here.
 */
#if 0
struct usbdevfs_urb32 {
	__u8 type;
	__u8 endpoint;
	__s32 status;
	__u32 flags;
	__u32 buffer;
	__s32 buffer_length;
	__s32 actual_length;
	__s32 start_frame;
	__s32 number_of_packets;
	__s32 error_count;
	__u32 signr;
	__u32 usercontext; /* unused */
	struct usbdevfs_iso_packet_desc iso_frame_desc[0];
};

#define USBDEVFS_SUBMITURB32       _IOR('U', 10, struct usbdevfs_urb32)

static int get_urb32(struct usbdevfs_urb *kurb,
		     struct usbdevfs_urb32 *uurb)
{
	if (get_user(kurb->type, &uurb->type) ||
	    __get_user(kurb->endpoint, &uurb->endpoint) ||
	    __get_user(kurb->status, &uurb->status) ||
	    __get_user(kurb->flags, &uurb->flags) ||
	    __get_user(kurb->buffer_length, &uurb->buffer_length) ||
	    __get_user(kurb->actual_length, &uurb->actual_length) ||
	    __get_user(kurb->start_frame, &uurb->start_frame) ||
	    __get_user(kurb->number_of_packets, &uurb->number_of_packets) ||
	    __get_user(kurb->error_count, &uurb->error_count) ||
	    __get_user(kurb->signr, &uurb->signr))
		return -EFAULT;

	kurb->usercontext = 0; /* unused currently */

	return 0;
}

/* Just put back the values which usbdevfs actually changes. */
static int put_urb32(struct usbdevfs_urb *kurb,
		     struct usbdevfs_urb32 *uurb)
{
	if (put_user(kurb->status, &uurb->status) ||
	    __put_user(kurb->actual_length, &uurb->actual_length) ||
	    __put_user(kurb->error_count, &uurb->error_count))
		return -EFAULT;

	if (kurb->number_of_packets != 0) {
		int i;

		for (i = 0; i < kurb->number_of_packets; i++) {
			if (__put_user(kurb->iso_frame_desc[i].actual_length,
				       &uurb->iso_frame_desc[i].actual_length) ||
			    __put_user(kurb->iso_frame_desc[i].status,
				       &uurb->iso_frame_desc[i].status))
				return -EFAULT;
		}
	}

	return 0;
}

static int get_urb32_isoframes(struct usbdevfs_urb *kurb,
			       struct usbdevfs_urb32 *uurb)
{
	unsigned int totlen;
	int i;

	if (kurb->type != USBDEVFS_URB_TYPE_ISO) {
		kurb->number_of_packets = 0;
		return 0;
	}

	if (kurb->number_of_packets < 1 ||
	    kurb->number_of_packets > 128)
		return -EINVAL;

	if (copy_from_user(&kurb->iso_frame_desc[0],
			   &uurb->iso_frame_desc[0],
			   sizeof(struct usbdevfs_iso_packet_desc) *
			   kurb->number_of_packets))
		return -EFAULT;

	totlen = 0;
	for (i = 0; i < kurb->number_of_packets; i++) {
		unsigned int this_len;

		this_len = kurb->iso_frame_desc[i].length;
		if (this_len > 1023)
			return -EINVAL;

		totlen += this_len;
	}

	if (totlen > 32768)
		return -EINVAL;

	kurb->buffer_length = totlen;

	return 0;
}

static int do_usbdevfs_urb(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_urb *kurb;
	struct usbdevfs_urb32 *uurb;
	mm_segment_t old_fs;
	__u32 udata;
	void __user *uptr;
	void *kptr;
	unsigned int buflen;
	int err;

	uurb = (struct usbdevfs_urb32 *) arg;

	err = -ENOMEM;
	kurb = kmalloc(sizeof(struct usbdevfs_urb) +
		       (sizeof(struct usbdevfs_iso_packet_desc) * 128),
		       GFP_KERNEL);
	if (!kurb)
		goto out;

	err = -EFAULT;
	if (get_urb32(kurb, uurb))
		goto out;

	err = get_urb32_isoframes(kurb, uurb);
	if (err)
		goto out;

	err = -EFAULT;
	if (__get_user(udata, &uurb->buffer))
		goto out;
	uptr = A(udata);

	err = -ENOMEM;
	buflen = kurb->buffer_length;
	kptr = kmalloc(buflen, GFP_KERNEL);
	if (!kptr)
		goto out;

	kurb->buffer = kptr;

	err = -EFAULT;
	if (copy_from_user(kptr, uptr, buflen))
		goto out_kptr;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_SUBMITURB, (unsigned long) kurb);
	set_fs(old_fs);

	if (err >= 0) {
		/* XXX Shit, this doesn't work for async URBs :-( XXX */
		if (put_urb32(kurb, uurb)) {
			err = -EFAULT;
		} else if ((kurb->endpoint & USB_DIR_IN) != 0) {
			if (copy_to_user(uptr, kptr, buflen))
				err = -EFAULT;
		}
	}

out_kptr:
	kfree(kptr);

out:
	kfree(kurb);
	return err;
}
#endif

#define USBDEVFS_REAPURB32         _IOW('U', 12, u32)
#define USBDEVFS_REAPURBNDELAY32   _IOW('U', 13, u32)

static int do_usbdevfs_reapurb(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs;
	void *kptr;
	int err;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd,
			(cmd == USBDEVFS_REAPURB32 ?
			 USBDEVFS_REAPURB :
			 USBDEVFS_REAPURBNDELAY),
			(unsigned long) &kptr);
	set_fs(old_fs);

	if (err >= 0 &&
	    put_user(((u32)(long)kptr), (u32 __user *) A(arg)))
		err = -EFAULT;

	return err;
}

struct usbdevfs_disconnectsignal32 {
	unsigned int signr;
	u32 context;
};

#define USBDEVFS_DISCSIGNAL32      _IOR('U', 14, struct usbdevfs_disconnectsignal32)

static int do_usbdevfs_discsignal(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct usbdevfs_disconnectsignal kdis;
	struct usbdevfs_disconnectsignal32 *udis;
	mm_segment_t old_fs;
	u32 uctx;
	int err;

	udis = (struct usbdevfs_disconnectsignal32 *) arg;

	if (get_user(kdis.signr, &udis->signr) ||
	    __get_user(uctx, &udis->context))
		return -EFAULT;

	kdis.context = (void *) (long)uctx;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, USBDEVFS_DISCSIGNAL, (unsigned long) &kdis);
	set_fs(old_fs);

	return err;
}

typedef int (* ioctl32_handler_t)(unsigned int, unsigned int, unsigned long, struct file *);

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl32_handler_t)(handler), NULL },
#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	}; struct ioctl_trans ioctl_end[0];

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
COMPATIBLE_IOCTL(TCSBRKP)
COMPATIBLE_IOCTL(TIOCSTART)
COMPATIBLE_IOCTL(TIOCSTOP)
COMPATIBLE_IOCTL(TIOCGSERIAL)
COMPATIBLE_IOCTL(TIOCSSERIAL)
COMPATIBLE_IOCTL(TIOCSLTC)
COMPATIBLE_IOCTL(FBIOGTYPE)
COMPATIBLE_IOCTL(FBIOSATTR)
COMPATIBLE_IOCTL(FBIOGATTR)
COMPATIBLE_IOCTL(FBIOSVIDEO)
COMPATIBLE_IOCTL(FBIOGVIDEO)
COMPATIBLE_IOCTL(FBIOGCURSOR32)  /* This is not implemented yet. Later it should be converted... */
COMPATIBLE_IOCTL(FBIOSCURPOS)
COMPATIBLE_IOCTL(FBIOGCURPOS)
COMPATIBLE_IOCTL(FBIOGCURMAX)
/* Little k */
COMPATIBLE_IOCTL(KIOCTYPE)
COMPATIBLE_IOCTL(KIOCLAYOUT)
COMPATIBLE_IOCTL(KIOCGTRANS)
COMPATIBLE_IOCTL(KIOCTRANS)
COMPATIBLE_IOCTL(KIOCCMD)
COMPATIBLE_IOCTL(KIOCSDIRECT)
COMPATIBLE_IOCTL(KIOCSLED)
COMPATIBLE_IOCTL(KIOCGLED)
COMPATIBLE_IOCTL(KIOCSRATE)
COMPATIBLE_IOCTL(KIOCGRATE)
COMPATIBLE_IOCTL(VUIDSFORMAT)
COMPATIBLE_IOCTL(VUIDGFORMAT)
/* Little v, the video4linux ioctls */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7])) /* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7])) /* RTCSET */
COMPATIBLE_IOCTL(ENVCTRL_RD_WARNING_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_SHUTDOWN_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_CPU_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_FAN_STATUS)
COMPATIBLE_IOCTL(ENVCTRL_RD_VOLTAGE_STATUS)
COMPATIBLE_IOCTL(ENVCTRL_RD_SCSI_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_ETHERNET_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_MTHRBD_TEMPERATURE)
COMPATIBLE_IOCTL(ENVCTRL_RD_CPU_VOLTAGE)
COMPATIBLE_IOCTL(ENVCTRL_RD_GLOBALADDRESS)
/* COMPATIBLE_IOCTL(D7SIOCRD) same value as ENVCTRL_RD_VOLTAGE_STATUS */
COMPATIBLE_IOCTL(D7SIOCWR)
COMPATIBLE_IOCTL(D7SIOCTM)
/* OPENPROMIO, SunOS/Solaris only, the NetBSD one's have
 * embedded pointers in the arg which we'd need to clean up...
 */
COMPATIBLE_IOCTL(OPROMGETOPT)
COMPATIBLE_IOCTL(OPROMSETOPT)
COMPATIBLE_IOCTL(OPROMNXTOPT)
COMPATIBLE_IOCTL(OPROMSETOPT2)
COMPATIBLE_IOCTL(OPROMNEXT)
COMPATIBLE_IOCTL(OPROMCHILD)
COMPATIBLE_IOCTL(OPROMGETPROP)
COMPATIBLE_IOCTL(OPROMNXTPROP)
COMPATIBLE_IOCTL(OPROMU2P)
COMPATIBLE_IOCTL(OPROMGETCONS)
COMPATIBLE_IOCTL(OPROMGETFBNAME)
COMPATIBLE_IOCTL(OPROMGETBOOTARGS)
COMPATIBLE_IOCTL(OPROMSETCUR)
COMPATIBLE_IOCTL(OPROMPCI2NODE)
COMPATIBLE_IOCTL(OPROMPATH2NODE)
/* Big L */
COMPATIBLE_IOCTL(LOOP_SET_STATUS64)
COMPATIBLE_IOCTL(LOOP_GET_STATUS64)
/* Big A */
COMPATIBLE_IOCTL(AUDIO_GETINFO)
COMPATIBLE_IOCTL(AUDIO_SETINFO)
COMPATIBLE_IOCTL(AUDIO_DRAIN)
COMPATIBLE_IOCTL(AUDIO_GETDEV)
COMPATIBLE_IOCTL(AUDIO_GETDEV_SUNOS)
COMPATIBLE_IOCTL(AUDIO_FLUSH)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE_MULTI)
/* Raw devices */
COMPATIBLE_IOCTL(RAW_SETBIND)
COMPATIBLE_IOCTL(RAW_GETBIND)
/* NCP ioctls which do not need any translations */
COMPATIBLE_IOCTL(NCP_IOC_CONN_LOGGED_IN)
COMPATIBLE_IOCTL(NCP_IOC_SIGN_INIT)
COMPATIBLE_IOCTL(NCP_IOC_SIGN_WANTED)
COMPATIBLE_IOCTL(NCP_IOC_SET_SIGN_WANTED)
COMPATIBLE_IOCTL(NCP_IOC_LOCKUNLOCK)
COMPATIBLE_IOCTL(NCP_IOC_GETROOT)
COMPATIBLE_IOCTL(NCP_IOC_SETROOT)
COMPATIBLE_IOCTL(NCP_IOC_GETCHARSETS)
COMPATIBLE_IOCTL(NCP_IOC_SETCHARSETS)
COMPATIBLE_IOCTL(NCP_IOC_GETDENTRYTTL)
COMPATIBLE_IOCTL(NCP_IOC_SETDENTRYTTL)
#if defined(CONFIG_DRM) || defined(CONFIG_DRM_MODULE)
COMPATIBLE_IOCTL(DRM_IOCTL_GET_MAGIC)
COMPATIBLE_IOCTL(DRM_IOCTL_IRQ_BUSID)
COMPATIBLE_IOCTL(DRM_IOCTL_AUTH_MAGIC)
COMPATIBLE_IOCTL(DRM_IOCTL_BLOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_UNBLOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_CONTROL)
COMPATIBLE_IOCTL(DRM_IOCTL_ADD_BUFS)
COMPATIBLE_IOCTL(DRM_IOCTL_MARK_BUFS)
COMPATIBLE_IOCTL(DRM_IOCTL_ADD_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_RM_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_MOD_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_GET_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_SWITCH_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_NEW_CTX)
COMPATIBLE_IOCTL(DRM_IOCTL_ADD_DRAW)
COMPATIBLE_IOCTL(DRM_IOCTL_RM_DRAW)
COMPATIBLE_IOCTL(DRM_IOCTL_LOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_UNLOCK)
COMPATIBLE_IOCTL(DRM_IOCTL_FINISH)
#endif /* DRM */
COMPATIBLE_IOCTL(WIOCSTART)
COMPATIBLE_IOCTL(WIOCSTOP)
COMPATIBLE_IOCTL(WIOCGSTAT)
COMPATIBLE_IOCTL(HCIUARTSETPROTO)
COMPATIBLE_IOCTL(HCIUARTGETPROTO)
COMPATIBLE_IOCTL(RFCOMMCREATEDEV)
COMPATIBLE_IOCTL(RFCOMMRELEASEDEV)
COMPATIBLE_IOCTL(RFCOMMGETDEVLIST)
COMPATIBLE_IOCTL(RFCOMMGETDEVINFO)
COMPATIBLE_IOCTL(RFCOMMSTEALDLC)
COMPATIBLE_IOCTL(BNEPCONNADD)
COMPATIBLE_IOCTL(BNEPCONNDEL)
COMPATIBLE_IOCTL(BNEPGETCONNLIST)
COMPATIBLE_IOCTL(BNEPGETCONNINFO)
/* device-mapper */
COMPATIBLE_IOCTL(DM_VERSION)
COMPATIBLE_IOCTL(DM_REMOVE_ALL)
COMPATIBLE_IOCTL(DM_DEV_CREATE)
COMPATIBLE_IOCTL(DM_DEV_REMOVE)
COMPATIBLE_IOCTL(DM_DEV_RELOAD)
COMPATIBLE_IOCTL(DM_DEV_SUSPEND)
COMPATIBLE_IOCTL(DM_DEV_RENAME)
COMPATIBLE_IOCTL(DM_DEV_DEPS)
COMPATIBLE_IOCTL(DM_DEV_STATUS)
COMPATIBLE_IOCTL(DM_TARGET_STATUS)
COMPATIBLE_IOCTL(DM_TARGET_WAIT)
/* And these ioctls need translation */
HANDLE_IOCTL(HDIO_GETGEO_BIG_RAW, hdio_getgeo_big)
/* NCPFS */
HANDLE_IOCTL(NCP_IOC_NCPREQUEST_32, do_ncp_ncprequest)
HANDLE_IOCTL(NCP_IOC_GETMOUNTUID2_32, do_ncp_getmountuid2)
HANDLE_IOCTL(NCP_IOC_GET_FS_INFO_V2_32, do_ncp_getfsinfo2)
HANDLE_IOCTL(NCP_IOC_GETOBJECTNAME_32, do_ncp_getobjectname)
HANDLE_IOCTL(NCP_IOC_SETOBJECTNAME_32, do_ncp_setobjectname)
HANDLE_IOCTL(NCP_IOC_GETPRIVATEDATA_32, do_ncp_getprivatedata)
HANDLE_IOCTL(NCP_IOC_SETPRIVATEDATA_32, do_ncp_setprivatedata)
/* Note SIOCRTMSG is no longer, so this is safe and * the user would have seen just an -EINVAL anyways. */
HANDLE_IOCTL(FBIOPUTCMAP32, fbiogetputcmap)
HANDLE_IOCTL(FBIOGETCMAP32, fbiogetputcmap)
HANDLE_IOCTL(FBIOSCURSOR32, fbiogscursor)
#if defined(CONFIG_DRM) || defined(CONFIG_DRM_MODULE)
HANDLE_IOCTL(DRM32_IOCTL_VERSION, drm32_version)
HANDLE_IOCTL(DRM32_IOCTL_GET_UNIQUE, drm32_getsetunique)
HANDLE_IOCTL(DRM32_IOCTL_SET_UNIQUE, drm32_getsetunique)
HANDLE_IOCTL(DRM32_IOCTL_ADD_MAP, drm32_addmap)
HANDLE_IOCTL(DRM32_IOCTL_INFO_BUFS, drm32_info_bufs)
HANDLE_IOCTL(DRM32_IOCTL_FREE_BUFS, drm32_free_bufs)
HANDLE_IOCTL(DRM32_IOCTL_MAP_BUFS, drm32_map_bufs)
HANDLE_IOCTL(DRM32_IOCTL_DMA, drm32_dma)
HANDLE_IOCTL(DRM32_IOCTL_RES_CTX, drm32_res_ctx)
#endif /* DRM */
#if 0
HANDLE_IOCTL(RTC32_IRQP_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_IRQP_SET, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_SET, do_rtc_ioctl)
#endif
HANDLE_IOCTL(USBDEVFS_CONTROL32, do_usbdevfs_control)
HANDLE_IOCTL(USBDEVFS_BULK32, do_usbdevfs_bulk)
/*HANDLE_IOCTL(USBDEVFS_SUBMITURB32, do_usbdevfs_urb)*/
HANDLE_IOCTL(USBDEVFS_REAPURB32, do_usbdevfs_reapurb)
HANDLE_IOCTL(USBDEVFS_REAPURBNDELAY32, do_usbdevfs_reapurb)
HANDLE_IOCTL(USBDEVFS_DISCSIGNAL32, do_usbdevfs_discsignal)
/* take care of sizeof(sizeof()) breakage */
IOCTL_TABLE_END
