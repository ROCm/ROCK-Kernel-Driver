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
#include "r128_drm.h"
#define IOCTL32_PRIVATE
#include "drm_ioctl32.h"

#define DRM_IOCTL_R128_INIT32		DRM_IOW( 0x40, drm32_r128_init_t)
#define DRM_IOCTL_R128_DEPTH32		DRM_IOW( 0x4c, drm32_r128_depth_t)
#define DRM_IOCTL_R128_STIPPLE32	DRM_IOW( 0x4d, drm32_r128_stipple_t)
#define DRM_IOCTL_R128_GETPARAM32	DRM_IOW( 0x52, drm32_r128_getparam_t)

typedef struct drm32_r128_init {
	int func;
	unsigned int sarea_priv_offset;
	int is_pci;
	int cce_mode;
	int cce_secure;
	int ring_size;
	int usec_timeout;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;
	unsigned int span_offset;

	unsigned int fb_offset;
	unsigned int mmio_offset;
	unsigned int ring_offset;
	unsigned int ring_rptr_offset;
	unsigned int buffers_offset;
	unsigned int agp_textures_offset;
} drm32_r128_init_t;

typedef struct drm32_r128_depth {
	int func;
	int n;
	u32 x;
	u32 y;
	u32 buffer;
	u32 mask;
} drm32_r128_depth_t;

typedef struct drm32_r128_stipple {
	u32 mask;
} drm32_r128_stipple_t;

typedef struct drm32_r128_getparam {
	int param;
	u32 value;
} drm32_r128_getparam_t;

static int
drm_128_init_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_r128_init_t *arg32 = (drm32_r128_init_t *) arg;
    drm_r128_init_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("r128_init_32_64");

    GET_USER(func);
    GET_USER(sarea_priv_offset);
    GET_USER(is_pci);
    GET_USER(cce_mode);
    GET_USER(cce_secure);
    GET_USER(ring_size);
    GET_USER(usec_timeout);
    GET_USER(fb_bpp);
    GET_USER(front_offset);
    GET_USER(front_pitch);
    GET_USER(back_offset);
    GET_USER(back_pitch);
    GET_USER(depth_bpp);
    GET_USER(depth_offset);
    GET_USER(depth_pitch);
    GET_USER(span_offset);
    GET_USER(fb_offset);
    GET_USER(mmio_offset);
    GET_USER(ring_offset);
    GET_USER(ring_rptr_offset);
    GET_USER(buffers_offset);
    GET_USER(agp_textures_offset);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_128_depth_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_r128_depth_t *arg32 = (drm32_r128_depth_t *) arg;
    drm_r128_depth_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("r128_depth_32_64");

    GET_USER(func);
    GET_USER(n);
    GET_USER_P(x);
    GET_USER_P(y);
    GET_USER_P(buffer);
    GET_USER_P(mask);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_128_stipple_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_r128_stipple_t *arg32 = (drm32_r128_stipple_t *) arg;
    drm_r128_stipple_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("r128_stipple_32_64");

    GET_USER_P(mask);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_128_getparam_32_64(unsigned int fd, unsigned int cmd,
	unsigned long arg, struct file *file)
{
    drm32_r128_getparam_t *arg32 = (drm32_r128_getparam_t *) arg;
    drm_r128_getparam_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;
    
    DEBUG("r128_getparam_wr_32_64");
    GET_USER(param);
    GET_USER_P(value);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    DEBUG("done");

    return err;
}


void
r128_unregister_ioctl32(void);

int
r128_register_ioctl32(void)
{
    int err;

    if (!drm32_register()) return -1;
    REG_IOCTL32(DRM_IOCTL_R128_INIT32, drm_128_init_32_64);
    REG_IOCTL32(DRM_IOCTL_R128_DEPTH32, drm_128_depth_32_64);
    REG_IOCTL32(DRM_IOCTL_R128_STIPPLE32, drm_128_stipple_32_64);
    REG_IOCTL32(DRM_IOCTL_R128_GETPARAM32, drm_128_getparam_32_64);

    REG_IOCTL32(DRM_IOCTL_R128_CCE_START, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_CCE_STOP, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_CCE_RESET, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_CCE_IDLE, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_RESET, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_SWAP, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_CLEAR, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_VERTEX, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_INDICES, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_BLIT, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_INDIRECT, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_FULLSCREEN, drm32_default_handler);
//    REG_IOCTL32(DRM_IOCTL_R128_CLEAR2, drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_R128_FLIP, drm32_default_handler);

    return 0;
 failed:
    r128_unregister_ioctl32();
    return -1;
}

void
r128_unregister_ioctl32(void)
{
    UNREG_IOCTL32(DRM_IOCTL_R128_INIT32);
    UNREG_IOCTL32(DRM_IOCTL_R128_DEPTH32);
    UNREG_IOCTL32(DRM_IOCTL_R128_STIPPLE32);
    UNREG_IOCTL32(DRM_IOCTL_R128_GETPARAM);

    UNREG_IOCTL32(DRM_IOCTL_R128_CCE_START);
    UNREG_IOCTL32(DRM_IOCTL_R128_CCE_STOP);
    UNREG_IOCTL32(DRM_IOCTL_R128_CCE_RESET);
    UNREG_IOCTL32(DRM_IOCTL_R128_CCE_IDLE);
    UNREG_IOCTL32(DRM_IOCTL_R128_RESET);
    UNREG_IOCTL32(DRM_IOCTL_R128_SWAP);
    UNREG_IOCTL32(DRM_IOCTL_R128_CLEAR);
    UNREG_IOCTL32(DRM_IOCTL_R128_VERTEX);
    UNREG_IOCTL32(DRM_IOCTL_R128_INDICES);
    UNREG_IOCTL32(DRM_IOCTL_R128_BLIT);
    UNREG_IOCTL32(DRM_IOCTL_R128_INDIRECT);
    UNREG_IOCTL32(DRM_IOCTL_R128_FULLSCREEN);
//    UNREG_IOCTL32(DRM_IOCTL_R128_CLEAR2);
    UNREG_IOCTL32(DRM_IOCTL_R128_FLIP);

    drm32_unregister();
}

#endif
