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
#include <linux/syscalls.h>
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
	
	ret = get_user(f.index, &(((struct fbcmap32 __user *)arg)->index));
	ret |= __get_user(f.count, &(((struct fbcmap32 __user *)arg)->count));
	ret |= __get_user(r, &(((struct fbcmap32 __user *)arg)->red));
	ret |= __get_user(g, &(((struct fbcmap32 __user *)arg)->green));
	ret |= __get_user(b, &(((struct fbcmap32 __user *)arg)->blue));
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
	
	ret = copy_from_user (&f, (struct fbcursor32 __user *) arg,
			      2 * sizeof (short) + 2 * sizeof(struct fbcurpos));
	ret |= __get_user(f.size.x,
			  &(((struct fbcursor32 __user *)arg)->size.x));
	ret |= __get_user(f.size.y,
			  &(((struct fbcursor32 __user *)arg)->size.y));
	ret |= __get_user(f.cmap.index,
			  &(((struct fbcursor32 __user *)arg)->cmap.index));
	ret |= __get_user(f.cmap.count,
			  &(((struct fbcursor32 __user *)arg)->cmap.count));
	ret |= __get_user(r, &(((struct fbcursor32 __user *)arg)->cmap.red));
	ret |= __get_user(g, &(((struct fbcursor32 __user *)arg)->cmap.green));
	ret |= __get_user(b, &(((struct fbcursor32 __user *)arg)->cmap.blue));
	ret |= __get_user(m, &(((struct fbcursor32 __user *)arg)->mask));
	ret |= __get_user(i, &(((struct fbcursor32 __user *)arg)->image));
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
	drm32_version_t __user *uversion = (drm32_version_t __user *)arg;
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
	drm32_unique_t __user *uarg = (drm32_unique_t __user *)arg;
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
	drm32_map_t __user *uarg = (drm32_map_t __user *) arg;
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

	karg.handle = (void *) (unsigned long) tmp;

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
	drm32_buf_info_t __user *uarg = (drm32_buf_info_t __user *)arg;
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
	drm32_buf_free_t __user *uarg = (drm32_buf_free_t __user *)arg;
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
	drm32_buf_map_t __user *uarg = (drm32_buf_map_t __user *)arg;
	drm32_buf_pub_t __user *ulist;
	drm_buf_map_t karg;
	mm_segment_t old_fs;
	int orig_count, ret, i;
	u32 tmp1, tmp2;

	if (get_user(karg.count, &uarg->count) ||
	    get_user(tmp1, &uarg->virtual) ||
	    get_user(tmp2, &uarg->list))
		return -EFAULT;

	karg.virtual = (void *) (unsigned long) tmp1;
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

		karg.list[i].address = (void *) (unsigned long) tmp1;
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
	drm32_dma_t __user *uarg = (drm32_dma_t __user *) arg;
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
	drm32_ctx_res_t __user *uarg = (drm32_ctx_res_t __user *) arg;
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

typedef int (* ioctl32_handler_t)(unsigned int, unsigned int, unsigned long, struct file *);

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl32_handler_t)(handler), NULL },
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
/* And these ioctls need translation */
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
/* take care of sizeof(sizeof()) breakage */
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
