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
#include "mga_drm.h"
#define IOCTL32_PRIVATE
#include "drm_ioctl32.h"


typedef struct drm32_mga_init {
	int func;
   	u32 sarea_priv_offset;
	int chipset;
   	int sgram;
	unsigned int maccess;
   	unsigned int fb_cpp;
	unsigned int front_offset, front_pitch;
   	unsigned int back_offset, back_pitch;
   	unsigned int depth_cpp;
   	unsigned int depth_offset, depth_pitch;
   	unsigned int texture_offset[MGA_NR_TEX_HEAPS];
   	unsigned int texture_size[MGA_NR_TEX_HEAPS];
	u32 fb_offset;
	u32 mmio_offset;
	u32 status_offset;
	u32 warp_offset;
	u32 primary_offset;
	u32 buffers_offset;
} drm32_mga_init_t;

typedef struct drm32_mga_getpram {
    int param;
    u32 value;
} drm32_mga_getparam_t;

#define DRM_IOCTL_MGA_INIT32		DRM_IOW( 0x40, drm32_mga_init_t)
#define DRM_IOCTL_MGA_GETPARAM32        DRM_IOWR(0x49, drm32_mga_getparam_t)

void mga_unregister_ioctl32(void);

static int
mga_init_w_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_mga_init_t *arg32 = (drm32_mga_init_t *) arg;
    drm_mga_init_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    int i;

    DEBUG("mga_init_w_32_64");
    GET_USER(func);
    GET_USER(sarea_priv_offset);
    GET_USER(chipset);
    GET_USER(sgram);
    GET_USER(maccess);
    GET_USER(fb_cpp);
    GET_USER(front_offset);
    GET_USER(front_pitch);
    GET_USER(back_offset);
    GET_USER(back_pitch);
    GET_USER(depth_cpp);
    GET_USER(depth_offset);
    GET_USER(depth_pitch);

    for (i = 0; i < MGA_NR_TEX_HEAPS; i++) {
	GET_USER(texture_offset[i]);
	GET_USER(texture_size[i]);
    }

    GET_USER(fb_offset);
    GET_USER(mmio_offset);
    GET_USER(status_offset);
    GET_USER(warp_offset);
    GET_USER(primary_offset);
    GET_USER(buffers_offset);
    
    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
mga_getparam_wr_32_64(unsigned int fd, unsigned int cmd,
	unsigned long arg, struct file *file)
{
    drm32_mga_getparam_t *arg32 = (drm32_mga_getparam_t *) arg;
    drm_mga_getparam_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;
    
    DEBUG("mga_getparam_wr_32_64");
    GET_USER(param);
    GET_USER_P(value);

    if (err) return -EFAULT;
    
    SYS_IOCTL;    
    DEBUG("done");

    return err;
}

int
mga_register_ioctl32(void)
{
    int err;

    if (!drm32_register()) return -1;
    REG_IOCTL32(DRM_IOCTL_MGA_INIT32,mga_init_w_32_64);
    REG_IOCTL32(DRM_IOCTL_MGA_GETPARAM32,mga_getparam_wr_32_64);
    
    REG_IOCTL32(DRM_IOCTL_MGA_FLUSH,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MGA_RESET,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MGA_SWAP,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MGA_CLEAR,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MGA_VERTEX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MGA_INDICES,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MGA_ILOAD,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_MGA_BLIT,drm32_default_handler);
    
    return 0;
 failed:
    mga_unregister_ioctl32();
    return -1;
}

void
mga_unregister_ioctl32(void)
{
    UNREG_IOCTL32(DRM_IOCTL_MGA_INIT32);
    UNREG_IOCTL32(DRM_IOCTL_MGA_GETPARAM32);
    UNREG_IOCTL32(DRM_IOCTL_MGA_FLUSH);
    UNREG_IOCTL32(DRM_IOCTL_MGA_RESET);
    UNREG_IOCTL32(DRM_IOCTL_MGA_RESET);
    UNREG_IOCTL32(DRM_IOCTL_MGA_SWAP);
    UNREG_IOCTL32(DRM_IOCTL_MGA_CLEAR);
    UNREG_IOCTL32(DRM_IOCTL_MGA_VERTEX);
    UNREG_IOCTL32(DRM_IOCTL_MGA_INDICES);
    UNREG_IOCTL32(DRM_IOCTL_MGA_INDICES);
    UNREG_IOCTL32(DRM_IOCTL_MGA_ILOAD);
    UNREG_IOCTL32(DRM_IOCTL_MGA_BLIT);

    drm32_unregister();
}

#endif
