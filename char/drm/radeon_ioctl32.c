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
#include "radeon_drm.h"
#define IOCTL32_PRIVATE
#include "drm_ioctl32.h"



typedef struct drm32_radeon_clear {
	unsigned int flags;
	unsigned int clear_color;
	unsigned int clear_depth;
	unsigned int color_mask;
	unsigned int depth_mask;
        u32 depth_boxes; /* drm_radeon_clear_rect_t * */
} drm32_radeon_clear_t;

typedef struct drm32_radeon_stipple {
        u32 mask; /* unsigned int * */
} drm32_radeon_stipple_t;

typedef struct drm32_radeon_tex_image {
	unsigned int x, y;		/* Blit coordinates */
	unsigned int width, height;
	u32 data;  /* const void * */
} drm32_radeon_tex_image_t;

typedef struct drm32_radeon_texture {
	int offset;
	int pitch;
	int format;
	int width;			/* Texture image coordinates */
	int height;
	u32 image;  /* drm_radeon_tex_image_t * */
} drm32_radeon_texture_t;

typedef struct drm32_radeon_vertex2 {
    int idx;
    int discard;
    int nr_states;
    u32 state; /* drm_radeon_state_t */
    int nr_prims;
    u32 prim;  /* drm_radeon_prim_t */
} drm32_radeon_vertex2_t;

typedef struct drm32_radeon_init {
    int func;
    unsigned int sarea_priv_offset;
    int is_pci;
    int cp_mode;
    int gart_size;
    int ring_size;
    int usec_timeout;
    unsigned int fb_bpp;
    unsigned int front_offset, front_pitch;
    unsigned int back_offset, back_pitch;
    unsigned int depth_bpp;
    unsigned int depth_offset, depth_pitch;

    u32 fb_offset;
    u32 mmio_offset;
    u32 ring_offset;
    u32 ring_rptr_offset;
    u32 buffers_offset;
    u32 gart_textures_offset;
} drm32_radeon_init_t;

typedef struct drm32_radeon_getparam {
	int param;
	u32 value;
} drm32_radeon_getparam_t;

typedef struct drm32_radeon_mem_alloc {
	int region;
	int alignment;
	int size;
	u32 region_offset;	/* offset from start of fb or agp */
} drm32_radeon_mem_alloc_t;

typedef struct drm32_radeon_irq_emit {
	u32 irq_seq;
} drm32_radeon_irq_emit_t;

typedef struct drm32_radeon_cmd_buffer {
	int bufsz;
	u32 buf;
	int nbox;
	u32 boxes;
} drm32_radeon_cmd_buffer_t;

typedef struct drm32_radeon_setparam {
	unsigned int param;
	int64_t      value;
} __attribute__((packed)) drm32_radeon_setparam_t;


#define DRM_IOCTL_RADEON_CLEAR32	DRM_IOW( 0x48, drm32_radeon_clear_t)
#define DRM_IOCTL_RADEON_STIPPLE32	DRM_IOW( 0x4c, drm32_radeon_stipple_t)
#define DRM_IOCTL_RADEON_TEXTURE32	DRM_IOWR(0x4e, drm32_radeon_texture_t)
#define DRM_IOCTL_RADEON_VERTEX232	DRM_IOW( 0x4f, drm32_radeon_vertex2_t)
#define DRM_IOCTL_RADEON_CP_INIT32	DRM_IOW( 0x40, drm32_radeon_init_t)
#define DRM_IOCTL_RADEON_ALLOC32        DRM_IOWR(0x53, drm32_radeon_mem_alloc_t)
#define DRM_IOCTL_RADEON_IRQ_EMIT32     DRM_IOWR(0x56, drm32_radeon_irq_emit_t)
#define DRM_IOCTL_RADEON_GETPARAM32     DRM_IOWR(0x51, drm32_radeon_getparam_t)
#define DRM_IOCTL_RADEON_CMDBUF32       DRM_IOW(0x50, drm32_radeon_cmd_buffer_t)
#define DRM_IOCTL_RADEON_SETPARAM32     DRM_IOW(0x59, drm32_radeon_setparam_t)


void radeon_unregister_ioctl32(void);

static int
drm_clear_w_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_clear_t *arg32 = (drm32_radeon_clear_t *) arg;
    drm_radeon_clear_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_clear_w_32_64");
    GET_USER(flags);
    GET_USER(clear_color);
    GET_USER(clear_depth);
    GET_USER(color_mask);
    GET_USER(depth_mask);
    GET_USER_P(depth_boxes);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_stipple_w_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_stipple_t *arg32 = (drm32_radeon_stipple_t *) arg;
    drm_radeon_stipple_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("drm_stipple_w_32_64");
    GET_USER_P(mask);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_texture_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_texture_t *arg32 = (drm32_radeon_texture_t *) arg;
    drm_radeon_texture_t arg64;
    drm32_radeon_tex_image_t *image32;
    drm_radeon_tex_image_t image64;
    mm_segment_t old_fs;
    int err = 0, err_tmp = 0;
    u64 dummy;

    DEBUG("drm_texture_32_64");
    GET_USER(offset);
    GET_USER(pitch);
    GET_USER(format);
    GET_USER(width);
    GET_USER(height);
    image32 = (drm32_radeon_tex_image_t *)(unsigned long)(arg32->image);
    arg64.image = &image64;
    err |= get_user(image64.x,&image32->x);
    err |= get_user(image64.y,&image32->y);
    err |= get_user(image64.width,&image32->width);
    err |= get_user(image64.height,&image32->height);
    err |= get_user(dummy,&image32->data);
    image64.data = (void *)dummy;
    
    if (err) return -EFAULT;
    
    SYS_IOCTL;
    err_tmp = err;
    err = 0;

    PUT_USER(offset);
    PUT_USER(pitch);
    PUT_USER(format);
    PUT_USER(width);
    PUT_USER(height);
    err |= put_user(image64.x,&image32->x);
    err |= put_user(image64.y,&image32->y);
    err |= put_user(image64.width,&image32->width);
    err |= put_user(image64.height,&image32->height);
    dummy = (u64)image64.data;

    err |= put_user((u32)dummy,&image32->data);
    return err ? -EFAULT : err_tmp;
}

static int
drm_vertex2_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_vertex2_t *arg32 = (drm32_radeon_vertex2_t *) arg;
    drm_radeon_vertex2_t arg64;
    mm_segment_t old_fs;
    u64 dummy;
    int err = 0;
    
    DEBUG("drm_vertex2_32_64");

    GET_USER(idx);
    GET_USER(discard);
    GET_USER(nr_states);
    GET_USER_P(state);
    GET_USER(nr_prims);
    GET_USER_P(prim);

    if (err) 
	return -EFAULT;
    
    SYS_IOCTL;

    return err;
}

static int
drm_radeon_init_w_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_init_t *arg32 = (drm32_radeon_init_t *) arg;
    drm_radeon_init_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("radeon_init_w_32_64");

    GET_USER(func);
    GET_USER(sarea_priv_offset);
    GET_USER(is_pci);
    GET_USER(cp_mode);
    GET_USER(gart_size);
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
    GET_USER(fb_offset);
    GET_USER(mmio_offset);
    GET_USER(ring_offset);
    GET_USER(ring_rptr_offset);
    GET_USER(buffers_offset);
    GET_USER(gart_textures_offset);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_radeon_cmd_buffer_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_cmd_buffer_t *arg32 = (drm32_radeon_cmd_buffer_t *) arg;
    drm_radeon_cmd_buffer_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("radeon_cmd_buffer_32_64");

    GET_USER(bufsz);
    GET_USER_P(buf);
    GET_USER(nbox);
    GET_USER_P(boxes);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_radeon_mem_alloc_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_mem_alloc_t *arg32 = (drm32_radeon_mem_alloc_t *) arg;
    drm_radeon_mem_alloc_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("radeon_alloc_32_64");

    GET_USER(region);
    GET_USER(alignment);
    GET_USER(size);
    GET_USER_P(region_offset);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_radeon_irq_emit_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_irq_emit_t *arg32 = (drm32_radeon_irq_emit_t *) arg;
    drm_radeon_irq_emit_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("radeon_irq_emit_32_64");

    GET_USER_P(irq_seq);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_radeon_getparam_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_getparam_t *arg32 = (drm32_radeon_getparam_t *) arg;
    drm_radeon_getparam_t arg64;
    mm_segment_t old_fs;
    int err = 0;
    u64 dummy;

    DEBUG("radeon_getpram_32_64");

    GET_USER(param);
    GET_USER_P(value);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

static int
drm_radeon_setparam_32_64(unsigned int fd, unsigned int cmd, 
		  unsigned long arg, struct file *file)
{
    drm32_radeon_setparam_t *arg32 = (drm32_radeon_setparam_t *) arg;
    drm_radeon_setparam_t arg64;
    mm_segment_t old_fs;
    int err = 0;

    DEBUG("radeon_setparam_32_64");

    GET_USER(param);
    GET_USER(value);

    if (err) return -EFAULT;
    
    SYS_IOCTL;
    return err;
}

int
radeon_register_ioctl32(void)
{
    int err;

    if (!drm32_register()) return -1;
    REG_IOCTL32(DRM_IOCTL_RADEON_CLEAR32,drm_clear_w_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_STIPPLE32,drm_stipple_w_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_TEXTURE32,drm_texture_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_VERTEX232,drm_vertex2_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_CP_INIT32,drm_radeon_init_w_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_ALLOC32,drm_radeon_mem_alloc_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_IRQ_EMIT32,drm_radeon_irq_emit_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_GETPARAM32,drm_radeon_getparam_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_CMDBUF32,drm_radeon_cmd_buffer_32_64);
    REG_IOCTL32(DRM_IOCTL_RADEON_SETPARAM32,drm_radeon_setparam_32_64);

    REG_IOCTL32(DRM_IOCTL_RADEON_CP_START,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_CP_STOP,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_CP_RESET,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_CP_IDLE,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_RESET,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_FULLSCREEN,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_SWAP,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_VERTEX,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_INDICES,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_INDIRECT,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_FLIP,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_FREE,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_IRQ_WAIT,drm32_default_handler);
    REG_IOCTL32(DRM_IOCTL_RADEON_CP_RESUME,drm32_default_handler);
    return 0;
 failed:
    radeon_unregister_ioctl32();
    return -1;
}

void
radeon_unregister_ioctl32(void)
{
    UNREG_IOCTL32(DRM_IOCTL_RADEON_CLEAR32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_STIPPLE32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_TEXTURE32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_VERTEX232);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_CP_INIT32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_ALLOC32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_IRQ_EMIT32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_GETPARAM32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_CMDBUF32);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_SETPARAM32);

    UNREG_IOCTL32(DRM_IOCTL_RADEON_CP_START);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_CP_STOP);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_CP_RESET);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_CP_IDLE);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_RESET);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_FULLSCREEN);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_SWAP);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_VERTEX);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_INDICES);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_INDIRECT);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_FLIP);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_FREE);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_IRQ_WAIT);
    UNREG_IOCTL32(DRM_IOCTL_RADEON_CP_RESUME);

    drm32_unregister();
}

#endif
