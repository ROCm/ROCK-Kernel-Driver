/*
 * Copyright 2003, 2004, Egbert Eich
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * EGBERT EICH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CON-
 * NECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Egbert Eich shall not
 * be used in advertising or otherwise to promote the sale, use or other deal-
 *ings in this Software without prior written authorization from Egbert Eich.
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#if 0
#include <linux/wrapper.h>
#else
#include <linux/syscalls.h>
#endif
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#ifdef __x86_64__

#include <asm/ioctl32.h>
#include "drm.h"
#define IOCTL32_PRIVATE
#include "drm_ioctl32.h"


MODULE_AUTHOR( "Egbert Eich, eich@suse.de" );
MODULE_DESCRIPTION( "DRM ioctl32 translations" );
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL and additional rights");
#endif

#define DRM_IOCTL_VERSION_32		DRM_IOWR(0x00, drm32_version_t)
#define DRM_IOCTL_GET_UNIQUE_32		DRM_IOWR(0x01, drm32_unique_t)
#define DRM_IOCTL_GET_MAP_32            DRM_IOWR(0x04, drm32_map_t)
#define DRM_IOCTL_GET_CLIENT_32         DRM_IOWR(0x05, drm32_client_t)
#define DRM_IOCTL_GET_STATS_32          DRM_IOR( 0x06, drm32_stats_t)
#define DRM_IOCTL_SET_UNIQUE_32		DRM_IOW( 0x10, drm32_unique_t)
#define DRM_IOCTL_ADD_MAP_32		DRM_IOWR(0x15, drm32_map_t)
#define DRM_IOCTL_ADD_BUFS_32		DRM_IOWR(0x16, drm32_buf_desc_t)
#define DRM_IOCTL_MARK_BUFS_32		DRM_IOW( 0x17, drm32_buf_desc_t)
#define DRM_IOCTL_INFO_BUFS_32		DRM_IOWR(0x18, drm32_buf_info_t)
#define DRM_IOCTL_MAP_BUFS_32		DRM_IOWR(0x19, drm32_buf_map_t)
#define DRM_IOCTL_FREE_BUFS_32		DRM_IOW( 0x1a, drm32_buf_free_t)
#define DRM_IOCTL_RM_MAP_32		DRM_IOW( 0x1b, drm32_map_t)
#if 0
#define DRM_IOCTL_SET_SAREA_CTX_32	DRM_IOW( 0x1c, drm32_ctx_priv_map_t)
#define DRM_IOCTL_GET_SAREA_CTX_32 	DRM_IOWR(0x1d, drm32_ctx_priv_map_t)
#endif
#define DRM_IOCTL_RES_CTX_32		DRM_IOWR(0x26, drm32_ctx_res_t)
#define DRM_IOCTL_DMA_32		DRM_IOWR(0x29, drm32_dma_t)
#define DRM_IOCTL_AGP_ENABLE_32		DRM_IOW( 0x32, drm32_agp_mode_t)
#define DRM_IOCTL_AGP_INFO_32		DRM_IOR( 0x33, drm32_agp_info_t)
#define DRM_IOCTL_AGP_ALLOC_32		DRM_IOWR(0x34, drm32_agp_buffer_t)
#define DRM_IOCTL_AGP_FREE_32		DRM_IOW( 0x35, drm32_agp_buffer_t)
#define DRM_IOCTL_AGP_BIND_32		DRM_IOW( 0x36, drm32_agp_binding_t)
#define DRM_IOCTL_AGP_UNBIND_32		DRM_IOW( 0x37, drm32_agp_binding_t)
#define DRM_IOCTL_SG_ALLOC_32		DRM_IOW( 0x38, drm32_scatter_gather_t)
#define DRM_IOCTL_SG_FREE_32		DRM_IOW( 0x39, drm32_scatter_gather_t)
#define DRM_IOCTL_WAIT_VBLANK_32	DRM_IOWR(0x3a, drm32_wait_vblank_t)

typedef struct drm32_version {  /* OK */
	int            version_major;	  /* Major version		   */
	int            version_minor;	  /* Minor version	           */
	int            version_patchlevel;/* Patch level		   */
	unsigned int   name_len;	  /* Length of name buffer         */
	u32    name;		          /* Name of driver		   */
	unsigned int   date_len;	  /* Length of date buffer	   */
	u32    date;		          /* User-space buffer to hold date*/
	unsigned int   desc_len;          /* Length of desc buffer	   */
	u32    desc;		          /* User-space buffer to hold desc*/
} drm32_version_t;

typedef struct drm32_unique {  /* OK */
	unsigned int   unique_len;/* Length of unique			    */
	u32    unique;		  /* Unique name for driver instantiation   */
} drm32_unique_t;

typedef struct drm32_client {
	int		idx;	/* Which client desired?                    */
	int		auth;	/* Is client authenticated?                 */
/*?*/	unsigned int	pid;	/* Process id                               */
/*?*/	unsigned int	uid;	/* User id                                  */
/*?*/	unsigned int	magic;	/* Magic                                    */
/*?*/	unsigned int	iocs;	/* Ioctl count                              */
} drm32_client_t;

#if 0
typedef struct drm32_ctx_priv_map {
	unsigned int	ctx_id;  /* Context requesting private mapping */
/*?*/	u32		handle;  /* Handle of map */
} drm32_ctx_priv_map_t;
#endif

typedef struct drm32_buf_desc { /* OK */
	int	      count;	 /* Number of buffers of this size	     */
	int	      size;	 /* Size in bytes			     */
	int	      low_mark;	 /* Low water mark			     */
	int	      high_mark; /* High water mark			     */
	int	      flags;
/*?*/	unsigned int agp_start; /* Start address of where the agp buffers
				  * are in the agp aperture */
} drm32_buf_desc_t;

typedef struct drm32_buf_info { /* OK */
	int	       count;	/* Entries in list			     */
  	u32            list;
} drm32_buf_info_t;

typedef struct drm32_buf_pub { /* OK (see drm32_buf_map) */
	int		  idx;	       /* Index into master buflist	     */
	int		  total;       /* Buffer size			     */
	int		  used;	       /* Amount of buffer in use (for DMA)  */
  	u32		  address;     /* Address of buffer		     */
} drm32_buf_pub_t;

typedef struct drm32_buf_map { /*OK (if do_mmap works correctly) */
	int	      count;	/* Length of buflist			    */
  	u32	      virtual;	/* Mmaped area in user-virtual		    */
  	u32           list;	/* Buffer information			    */
} drm32_buf_map_t;

typedef struct drm32_buf_free { /* OK (see above) */
	int	       count;
  	u32	       list;
} drm32_buf_free_t;

typedef struct drm32_stats {
/*?*/	unsigned int count;
	struct {
		unsigned int   value;
		drm_stat_type_t type;
	} data[15];
} drm32_stats_t;

typedef struct drm32_map {
/*?*/	unsigned int	offset;	 /* Requested physical address (0 for SARA)*/
/*?*/	unsigned int	size;	 /* Requested physical size (bytes)	    */
	drm_map_type_t	type;	 /* Type of memory to map		    */
	drm_map_flags_t flags;	 /* Flags				    */
        unsigned long	pub_handle; /* User-space: "Handle" to pass to mmap    */
	int		mtrr;	 /* MTRR slot used			    */
				 /* Private data			    */
} drm32_map_t;

typedef struct drm32_ctx_res { /* OK */
	int		count;
  	u32		contexts;
} drm32_ctx_res_t;

typedef struct drm32_dma { /* should be OK */
				/* Indices here refer to the offset into
				   buflist in drm_buf_get_t.  */
	int		context;	  /* Context handle		    */
	int		send_count;	  /* Number of buffers to send	    */
  	u32		send_indices;	  /* List of handles to buffers	    */
  	u32		send_sizes;	  /* Lengths of data to send	    */
	drm_dma_flags_t flags;		  /* Flags			    */
	int		request_count;	  /* Number of buffers requested    */
	int		request_size;	  /* Desired size for buffers	    */
  	u32		request_indices; /* Buffer information		    */
  	u32		request_sizes;
	int		granted_count;	  /* Number of buffers granted	    */
} drm32_dma_t;

typedef struct drm32_agp_buffer {
/*?*/	int	 size;
/*?*/	int	 handle;
/*?*/	int	 type; 
/*?*/   int      physical; 
} drm32_agp_buffer_t;

typedef struct drm32_agp_info {
	int            agp_version_major;
	int            agp_version_minor;
/*?*/	unsigned int   mode;
/*?*/	unsigned int   aperture_base;  /* physical address */
/*?*/	unsigned int   aperture_size;  /* bytes */
/*?*/	unsigned int   memory_allowed; /* bytes */
/*?*/	unsigned int   memory_used;

				/* PCI information */
	unsigned short id_vendor;
	unsigned short id_device;
} drm32_agp_info_t;

typedef struct drm32_agp_mode {
/*?*/	unsigned int mode;
} drm32_agp_mode_t;

typedef struct drm32_agp_binding {
/*?*/	unsigned int handle;   /* From drm_agp_buffer */
/*?*/	unsigned int offset;	/* In bytes -- will round to page boundary */
} drm32_agp_binding_t;

typedef struct drm32_scatter_gather {
/*?*/	unsigned int size;	/* In bytes -- will round to page boundary */
/*?*/	unsigned int handle;	/* Used for mapping / unmapping */
} drm32_scatter_gather_t;

struct drm32_wait_vblank_request {
        drm_vblank_seq_type_t type;
        unsigned int sequence;
        unsigned int signal;
};

struct drm32_wait_vblank_reply {
        drm_vblank_seq_type_t type;
        unsigned int sequence;
        int tval_sec;
        int tval_usec;
};

typedef union drm32_wait_vblank {
        struct drm32_wait_vblank_request request;
        struct drm32_wait_vblank_reply reply;
} drm32_wait_vblank_t;

static void drm_unregister_ioctl32(void);

static int    ioctl_conversions_successful = 0;

static int
drm_version_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_version_t *arg32 = (drm32_version_t *) arg;
    drm_version_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_version_32_64");
    GET_USER(version_major);
    GET_USER(version_minor);
    GET_USER(version_patchlevel);
    GET_USER(name_len);
    GET_USER_P(name);
    GET_USER(date_len);
    GET_USER_P(date);
    GET_USER(desc_len);
    GET_USER_P(desc);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    if (err) return err;
    
    PUT_USER(version_major);
    PUT_USER(version_minor);
    PUT_USER(version_patchlevel);
    PUT_USER(name_len);
//  PUT_USER(name);
    PUT_USER(date_len);
//  PUT_USER(date);
    PUT_USER(desc_len);
//  PUT_USER(desc);

    DEBUG("done");
    return err ? -EFAULT : 0;
}

static int
drm_unique_wr_32_64(unsigned int fd, unsigned cmd, 
		    unsigned long arg, struct file *file)
{
    drm32_unique_t *arg32 = (drm32_unique_t *) arg;
    drm_unique_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_unique_wr_32_64");
    GET_USER(unique_len);
    GET_USER_P(unique);
    
    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    if (err) return err;
    
    PUT_USER(unique_len);
//  PUT_USER(unique);
    DEBUG("done");
    return err ? -EFAULT : 0;
}

static int
drm_unique_w_32_64(unsigned int fd, unsigned int cmd, 
		   unsigned long arg, struct file *file)
{
    drm32_unique_t *arg32 = (drm32_unique_t *) arg;
    drm_unique_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_unique_w_32_64");
    GET_USER(unique_len);
    GET_USER_P(unique);
    
    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    DEBUG("done");
    return err;
}

static int
drm_map_rw_32_64(unsigned int fd, unsigned int cmd, 
		 unsigned long arg, struct file *file)
{
    drm32_map_t *arg32 = (drm32_map_t *) arg;
    drm_map_t arg64;
    mm_segment_t old_fs;
    int err = 0;
//  u64 dummy;

    DEBUG("drm_map_rw_32_64");
    GET_USER(offset);
    GET_USER(size);
    GET_USER(type);
    GET_USER(flags);
//  GET_USER(pub_handle);
    GET_USER(mtrr);

    if (err) return -EFAULT;
    
    SYS_IOCTL;

    if (err) return err;
    
    ASSERT32(offset);
    PUT_USER(offset);
    ASSERT32(size);
    PUT_USER(size);
    PUT_USER(type);
    PUT_USER(flags);
    PUT_USER(pub_handle);
    PUT_USER(mtrr);

    DEBUG("done");
    return err ? -EFAULT : 0;
}

static int
drm_map_w_32_64(unsigned int fd, unsigned int cmd, 
		unsigned long arg, struct file *file)
{
    drm32_map_t *arg32 = (drm32_map_t *) arg;
    drm_map_t arg64;
    mm_segment_t old_fs;
    int err = 0;
//  u64 dummy;
    
    DEBUG("drm_map_w_32_64");
    GET_USER(offset);
    GET_USER(size);
    GET_USER(type);
    GET_USER(flags);
//  GET_USER_P(handle);
    GET_USER(mtrr);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    DEBUG("done");
    return err;
}

static int
drm_client_32_64(unsigned int fd, unsigned int cmd, 
		 unsigned long arg, struct file *file)
{
    drm32_client_t *arg32 = (drm32_client_t *) arg;
    drm_client_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_client_32_64");
    GET_USER(idx);
//  GET_USER(auth);
//  GET_USER(pid);
//  GET_USER(uid);
//  GET_USER(magic);
//  GET_USER(iocs);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    if (err) return err;
    
//  PUT_USER(idx);
    PUT_USER(auth);
    ASSERT32(pid);
    PUT_USER(pid);
    ASSERT32(uid);
    PUT_USER(uid);
    ASSERT32(magic);
    PUT_USER(magic);
    ASSERT32(iocs);
    PUT_USER(iocs);

    DEBUG("done");
    return err ? -EFAULT : 0;
}

static int
drm_stats_32_64(unsigned int fd, unsigned int cmd, 
		unsigned long arg, struct file *file)
{
    drm32_stats_t *arg32 = (drm32_stats_t *) arg;
    drm_stats_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    int i;

    DEBUG("drm_stats_32_64");
    SYS_IOCTL;    
    if (err) return err;
    
    PUT_USER(count);
    for (i = 0; i < arg64.count; i ++) {
	PUT_USER(data[i].value);
	PUT_USER(data[i].type);
    }

    DEBUG("done");
    return err ? -EFAULT : 0;
}

#if 0
static int
drm_ctx_priv_map_wr_32_64(unsigned int fd, unsigned int cmd, 
			  unsigned long arg, struct file *file)
{
    drm32_ctx_priv_map_t *arg32 = (drm32_ctx_priv_map_t *) arg;
    drm_ctx_priv_map_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_ctx_priv_map_wr_32_64");
    GET_USER(ctx_id);
//    GET_USER_P(handle);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    if (err) return err;
    
//    PUT_USER(ctx_id);
    PUT_USER_P(handle);

    DEBUG("done");
    return err ? -EFAULT : 0;
}

static int
drm_ctx_priv_map_w_32_64(unsigned int fd, unsigned int cmd, 
			 unsigned long arg, struct file *file)
{
    drm32_ctx_priv_map_t *arg32 = (drm32_ctx_priv_map_t *) arg;
    drm_ctx_priv_map_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_ctx_priv_map_w_32_64");
    GET_USER(ctx_id);
    GET_USER_P(handle);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    

    DEBUG("done");
    return err;
}
#endif

static int
drm_ctx_res_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_ctx_res_t *arg32 = (drm32_ctx_res_t *) arg;
    drm_ctx_res_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_ctx_res_32_64");
    GET_USER(count);
    GET_USER_P(contexts);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    if (err) return err;
    
    PUT_USER(count);
//    PUT_USER(contexts);

    DEBUG("done");
    return err ? -EFAULT : 0;
}


static int
drm_dma_32_64(unsigned int fd, unsigned int cmd, 
	      unsigned long arg, struct file *file)
{
    drm32_dma_t *arg32 = (drm32_dma_t *) arg;
    drm_dma_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;
    
    DEBUG("drm_dma_32_64");
    GET_USER(context);
    GET_USER(send_count);
    GET_USER_P(send_indices);
    GET_USER_P(send_sizes);
    GET_USER(flags);
    GET_USER(request_count);
    GET_USER(request_size);
    GET_USER_P(request_indices);
    GET_USER_P(request_sizes);
    GET_USER(granted_count);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    if (err) return err;
    
    PUT_USER(context);
    PUT_USER(send_count);
//  PUT_USER(send_indices);
//  PUT_USER(send_sizes);
    PUT_USER(flags);
    PUT_USER(request_count);
    PUT_USER(request_size);
//    PUT_USER(request_indices);
//    PUT_USER(request_sizes);
    PUT_USER(granted_count);

    DEBUG("done");
    return err ? -EFAULT : 0;
}

static int
drm_buf_desc_wr_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_buf_desc_t *arg32 = (drm32_buf_desc_t *) arg;
    drm_buf_desc_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_buf_desc_wr_32_64");
    GET_USER(count);
    GET_USER(size);
    GET_USER(low_mark);
    GET_USER(high_mark);
    GET_USER(flags);
    GET_USER(agp_start);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    if (err) return err;
    
    PUT_USER(count);
    PUT_USER(size);
//   PUT_USER(low_mark);
//   PUT_USER(high_mark);
//   PUT_USER(flags);
//   PUT_USER(agp_start);

    DEBUG("done");
    return err ? -EFAULT : 0;
}

static int
drm_buf_desc_w_32_64(unsigned int fd, unsigned int cmd,
		     unsigned long arg, struct file *file)
{
    drm32_buf_desc_t *arg32 = (drm32_buf_desc_t *) arg;
    drm_buf_desc_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_buf_desc_w_32_64");
    GET_USER(count);
    GET_USER(size);
    GET_USER(low_mark);
    GET_USER(high_mark);
    GET_USER(flags);
    GET_USER(agp_start);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    

    DEBUG("done");
    return err;
}

static int
drm_buf_info_32_64(unsigned int fd, unsigned int cmd, 
		   unsigned long arg, struct file *file)
{
    drm32_buf_info_t *arg32 = (drm32_buf_info_t *) arg;
    drm_buf_info_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    drm32_buf_desc_t *list32 = (drm32_buf_desc_t*)(u64)arg32->list;
    drm_buf_desc_t *list64;
    int i;
    
    DEBUG("drm_buf_info_32_64");
    list64 = K_ALLOC(arg32->count * sizeof (drm_buf_desc_t));
    if (!list64) return -EFAULT;
    
    GET_USER(count);
    arg64.list = list64;
    
    for (i = 0 ; i < arg32->count; i ++) {
	err |= get_user(list64[i].count,&list32[i].count);
	err |= get_user(list64[i].size,&list32[i].size);
	err |= get_user(list64[i].high_mark,&list32[i].low_mark);
	err |= get_user(list64[i].flags,&list32[i].flags);
	err |= get_user(list64[i].agp_start,&list32[i].agp_start);
    }
    
    if (err) {
	K_FREE(list64);
	return -EFAULT;
    }
    
    SYS_IOCTL;    
    if (err) {
	K_FREE(list64);
	return err;
    }
    
    
    for (i = 0 ; i < arg32->count; i ++) {
	err |= put_user(list64[i].count,&list32[i].count);
	err |= put_user(list64[i].size,&list32[i].size);
	err |= put_user(list64[i].low_mark,&list32[i].low_mark);
	err |= put_user(list64[i].high_mark,&list32[i].high_mark);
	err |= put_user(list64[i].flags,&list32[i].flags);
//	err |= put_user(list64[i].agp_start,&list32[i].agp_start);
    }
    PUT_USER(count);

    K_FREE(list64);

    DEBUG("done");
    return err ? -EFAULT : 0;
}


static int
drm_buf_map_32_64(unsigned int fd, unsigned cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_buf_map_t *arg32 = (drm32_buf_map_t *) arg;
    drm_buf_map_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    drm32_buf_pub_t *list32 = (drm32_buf_pub_t*)(unsigned long)arg32->list;
    drm_buf_pub_t *list64;
    int count, i;
    u64 dummy;
    
    DEBUG("drm_buf_map_32_64");
    list64 = K_ALLOC(arg32->count * sizeof (drm_buf_pub_t));
    if (!list64) return -EFAULT;
    
    GET_USER(count);
    GET_USER_P(virtual);
    arg64.list = list64;
#if 0
    for (i = 0 ; i < arg32->count; i ++) {
	err |= get_user(list64[i].idx,&list32[i].idx);
	err |= get_user(list64[i].total,&list32[i].total);
	err |= get_user(list64[i].used,&list32[i].used);
	err |= get_user(dummy,&list32[i].address);
	list64[i].address = (void *)dummy;
    }
#endif
    if (err) {
	K_FREE(list64);
	return -EFAULT;
    }
    
    SYS_IOCTL;    
    if (err) {
	K_FREE(list64);
	return err;
    }
    
    count = arg32->count < arg64.count ? arg32->count : arg64.count;
    PUT_USER(count);
    PUT_USER_P(virtual);

    for (i = 0 ; i < count; i ++) {
	err |= put_user(list64[i].idx,&list32[i].idx);
	err |= put_user(list64[i].total,&list32[i].total);
	err |= put_user(list64[i].used,&list32[i].used);
	dummy = (u64)list64[i].address;
	err |= put_user((u32)dummy,&list32[i].address);
    }
    K_FREE(list64);

    return err ? -EFAULT : 0;
}

static int
drm_buf_free_w_32_64(unsigned int fd, unsigned int cmd, 
		     unsigned long arg, struct file *file)
{
    drm32_buf_free_t *arg32 = (drm32_buf_free_t *) arg;
    drm_buf_free_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    int i;
    int *list32 = (int *)(unsigned long)arg32->list;

    DEBUG("drm_buf_free_w_32_64");
    GET_USER(count);
    for (i = 0; i < arg32->count; i++)
	err |= get_user(arg64.list[i],&list32[i]);
	
    
    if (err) return -EFAULT;
    
    SYS_IOCTL;    

    DEBUG("done");
    return err;
}

static int
drm_agp_info_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_agp_info_t *arg32 = (drm32_agp_info_t *) arg;
    drm_agp_info_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_agp_info_32_64");
    
    SYS_IOCTL;    
    if (err) return err;
    
    PUT_USER(agp_version_major);
    PUT_USER(agp_version_minor);
    PUT_USER(mode);
    ASSERT32(aperture_base);
    PUT_USER(aperture_base);
    ASSERT32(aperture_size);
    PUT_USER(aperture_size);
    ASSERT32(memory_allowed);
    PUT_USER(memory_allowed);
    ASSERT32(memory_used);
    PUT_USER(memory_used);
    PUT_USER(id_vendor);
    PUT_USER(id_device);

    return err ? -EFAULT : 0;
}

static int
drm_agp_buffer_w_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_agp_buffer_t *arg32 = (drm32_agp_buffer_t *) arg;
    drm_agp_buffer_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_agp_buffer_w_32_64");
    GET_USER(handle);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    return err;
}

static int
drm_agp_buffer_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_agp_buffer_t *arg32 = (drm32_agp_buffer_t *) arg;
    drm_agp_buffer_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_agp_buffer_32_64");
    GET_USER(size);
    GET_USER(handle);
    GET_USER(type);
    GET_USER(physical);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    if (err) return err;
    
    ASSERT32(size);
    PUT_USER(size);
    ASSERT32(handle);
    PUT_USER(handle);
    ASSERT32(type);
    PUT_USER(type);
//    PUT_USER(physical);

    return err ? -EFAULT : 0;
}

static int
drm_agp_mode_w_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_agp_mode_t *arg32 = (drm32_agp_mode_t *) arg;
    drm_agp_mode_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_agp_mode_w_32_64");
    GET_USER(mode);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    

    return err;
}

static int
drm_agp_binding_w_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_agp_binding_t *arg32 = (drm32_agp_binding_t *) arg;
    drm_agp_binding_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_agp_binding_w_32_64");
    GET_USER(handle);
    GET_USER(offset);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    

    return err;
}

static int
drm32_scatter_gather_w_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_scatter_gather_t *arg32 = (drm32_scatter_gather_t *) arg;
    drm_scatter_gather_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_agp_scatter_gather_w_32_64");

    GET_USER(size);
    GET_USER(handle);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    

    return err;
}

static int
drm32_wait_vblank_rw_32_64(unsigned int fd, unsigned int cmd, 
		      unsigned long arg, struct file *file)
{
    drm32_wait_vblank_t *arg32 = (drm32_wait_vblank_t *) arg;
    drm_wait_vblank_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("drm_agp_scatter_gather_rw_32_64");

    err |= get_user(arg64.request.type,&arg32->request.type);
    err |= get_user(arg64.request.sequence,&arg32->request.sequence);
    err |= get_user(arg64.request.signal,&arg32->request.signal);

    if (err) return -EFAULT;
    
    SYS_IOCTL;

    err |= put_user(arg64.reply.type,&arg32->reply.type);
    err |= put_user(arg64.reply.sequence,&arg32->reply.sequence);
    ASSERT32(reply.tval_sec);
    err |= put_user(arg64.reply.tval_sec,&arg32->reply.tval_sec);
    ASSERT32(reply.tval_usec);
    err |= put_user(arg64.reply.tval_usec,&arg32->reply.tval_usec);

    return err ? -EFAULT : 0;
}

int
drm32_default_handler(unsigned int fd, unsigned int cmd, 
		     unsigned long arg, struct file *file)
{
    return SYS_IOCTL_FUNC(fd,cmd,arg);
}


static int
drm_register_ioctl32(void)
{
    int err;
    REG_IOCTL32(DRM_IOCTL_VERSION_32,drm_version_32_64);
    REG_IOCTL32(DRM_IOCTL_GET_UNIQUE_32,drm_unique_wr_32_64);
    REG_IOCTL32(DRM_IOCTL_GET_MAP_32,drm_map_rw_32_64);
    REG_IOCTL32(DRM_IOCTL_GET_CLIENT_32,drm_client_32_64);
    REG_IOCTL32(DRM_IOCTL_GET_STATS_32,drm_stats_32_64);
    REG_IOCTL32(DRM_IOCTL_SET_UNIQUE_32,drm_unique_w_32_64);
    REG_IOCTL32(DRM_IOCTL_ADD_MAP_32,drm_map_rw_32_64);
    REG_IOCTL32(DRM_IOCTL_ADD_BUFS_32,drm_buf_desc_wr_32_64);
    REG_IOCTL32(DRM_IOCTL_MARK_BUFS_32,drm_buf_desc_w_32_64);
    REG_IOCTL32(DRM_IOCTL_INFO_BUFS_32,drm_buf_info_32_64);
    REG_IOCTL32(DRM_IOCTL_MAP_BUFS_32,drm_buf_map_32_64);
    REG_IOCTL32(DRM_IOCTL_FREE_BUFS_32,drm_buf_free_w_32_64);
    REG_IOCTL32(DRM_IOCTL_RM_MAP_32,drm_map_w_32_64);
#if 0
    REG_IOCTL32(DRM_IOCTL_SET_SAREA_CTX_32,drm_ctx_priv_map_w_32_64);
    REG_IOCTL32(DRM_IOCTL_GET_SAREA_CTX_32,drm_ctx_priv_map_wr_32_64);
#endif
    REG_IOCTL32(DRM_IOCTL_RES_CTX_32,drm_ctx_res_32_64)
    REG_IOCTL32(DRM_IOCTL_DMA_32,drm_dma_32_64);
    REG_IOCTL32(DRM_IOCTL_AGP_ENABLE_32,drm_agp_mode_w_32_64);
    REG_IOCTL32(DRM_IOCTL_AGP_INFO_32,drm_agp_info_32_64);
    REG_IOCTL32(DRM_IOCTL_AGP_ALLOC_32,drm_agp_buffer_32_64);
    REG_IOCTL32(DRM_IOCTL_AGP_FREE_32,drm_agp_buffer_w_32_64);
    REG_IOCTL32(DRM_IOCTL_AGP_BIND_32,drm_agp_binding_w_32_64);
    REG_IOCTL32(DRM_IOCTL_AGP_UNBIND_32,drm_agp_binding_w_32_64);
    REG_IOCTL32(DRM_IOCTL_SG_ALLOC_32,drm32_scatter_gather_w_32_64);
    REG_IOCTL32(DRM_IOCTL_SG_FREE_32,drm32_scatter_gather_w_32_64);
    REG_IOCTL32(DRM_IOCTL_WAIT_VBLANK_32,drm32_wait_vblank_rw_32_64);
    REG_IOCTL32(DRM_IOCTL_GET_MAGIC,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_IRQ_BUSID,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_SET_VERSION,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_AUTH_MAGIC,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_BLOCK,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_UNBLOCK,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_CONTROL,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_SET_SAREA_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_GET_SAREA_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_ADD_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RM_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MOD_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_GET_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_SWITCH_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_NEW_CTX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_ADD_DRAW,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RM_DRAW,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_LOCK,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_UNLOCK,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_FINISH,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_AGP_ACQUIRE,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_AGP_RELEASE,drm32_default_handler);

    ioctl_conversions_successful = 1;
    
    return 0;
 failed:
    return -1;
}


static void
drm_unregister_ioctl32(void)
{
    UNREG_IOCTL32(DRM_IOCTL_VERSION_32);
    UNREG_IOCTL32(DRM_IOCTL_GET_UNIQUE_32);
    UNREG_IOCTL32(DRM_IOCTL_GET_CLIENT_32);
#if 0
    UNREG_IOCTL32(DRM_IOCTL_SET_SAREA_CTX_32);
    UNREG_IOCTL32(DRM_IOCTL_GET_SAREA_CTX_32);
#endif
    UNREG_IOCTL32(DRM_IOCTL_RES_CTX_32);
    UNREG_IOCTL32(DRM_IOCTL_DMA_32);
    
    UNREG_IOCTL32(DRM_IOCTL_INFO_BUFS_32);
    UNREG_IOCTL32(DRM_IOCTL_MAP_BUFS_32);
    UNREG_IOCTL32(DRM_IOCTL_FREE_BUFS_32);

    UNREG_IOCTL32(DRM_IOCTL_GET_MAP_32);
    UNREG_IOCTL32(DRM_IOCTL_GET_STATS_32);
    UNREG_IOCTL32(DRM_IOCTL_SET_UNIQUE_32);
    UNREG_IOCTL32(DRM_IOCTL_ADD_MAP_32);
    UNREG_IOCTL32(DRM_IOCTL_RM_MAP_32);
    UNREG_IOCTL32(DRM_IOCTL_ADD_BUFS_32);
    UNREG_IOCTL32(DRM_IOCTL_MARK_BUFS_32);
    UNREG_IOCTL32(DRM_IOCTL_AGP_ALLOC_32);
    UNREG_IOCTL32(DRM_IOCTL_AGP_FREE_32);
    UNREG_IOCTL32(DRM_IOCTL_AGP_INFO_32);
    UNREG_IOCTL32(DRM_IOCTL_AGP_ENABLE_32);
    UNREG_IOCTL32(DRM_IOCTL_AGP_BIND_32);
    UNREG_IOCTL32(DRM_IOCTL_AGP_UNBIND_32);
    UNREG_IOCTL32(DRM_IOCTL_SG_ALLOC_32);
    UNREG_IOCTL32(DRM_IOCTL_SG_FREE_32);
    UNREG_IOCTL32(DRM_IOCTL_WAIT_VBLANK_32);

    UNREG_IOCTL32(DRM_IOCTL_SET_SAREA_CTX);
    UNREG_IOCTL32(DRM_IOCTL_GET_SAREA_CTX);
    UNREG_IOCTL32(DRM_IOCTL_GET_MAGIC);
    UNREG_IOCTL32(DRM_IOCTL_IRQ_BUSID);
    UNREG_IOCTL32(DRM_IOCTL_SET_VERSION);
    UNREG_IOCTL32(DRM_IOCTL_BLOCK);
    UNREG_IOCTL32(DRM_IOCTL_UNBLOCK);
    UNREG_IOCTL32(DRM_IOCTL_AUTH_MAGIC);
    UNREG_IOCTL32(DRM_IOCTL_ADD_CTX);
    UNREG_IOCTL32(DRM_IOCTL_RM_CTX);
    UNREG_IOCTL32(DRM_IOCTL_MOD_CTX);
    UNREG_IOCTL32(DRM_IOCTL_GET_CTX);
    UNREG_IOCTL32(DRM_IOCTL_SWITCH_CTX);
    UNREG_IOCTL32(DRM_IOCTL_NEW_CTX);
    UNREG_IOCTL32(DRM_IOCTL_ADD_DRAW);
    UNREG_IOCTL32(DRM_IOCTL_RM_DRAW);
    UNREG_IOCTL32(DRM_IOCTL_LOCK);
    UNREG_IOCTL32(DRM_IOCTL_UNLOCK);
    UNREG_IOCTL32(DRM_IOCTL_FINISH);
    UNREG_IOCTL32(DRM_IOCTL_CONTROL);
    UNREG_IOCTL32(DRM_IOCTL_AGP_ACQUIRE);
    UNREG_IOCTL32(DRM_IOCTL_AGP_RELEASE);
}

int
drm32_register(void)
{
    if (ioctl_conversions_successful == 0)
	return 0;
#if 0 && defined(MODULE)
    MOD_INC_USE_COUNT;
#endif
	return 1;
}

void
drm32_unregister(void)
{
#if 0 && defined(MODULE)
    if (ioctl_conversions_successful == 0)
	return;
    MOD_DEC_USE_COUNT;
#endif
	return;
}

static int
__init drm32_init( void )
{
    return drm_register_ioctl32();
}

static void
__exit drm32_cleanup (void)
{
    drm_unregister_ioctl32();
}


module_init(drm32_init);
module_exit(drm32_cleanup);

EXPORT_SYMBOL(drm32_register);
EXPORT_SYMBOL(drm32_unregister);
EXPORT_SYMBOL(drm32_default_handler);

#endif
