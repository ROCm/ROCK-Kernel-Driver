/* radeon.h -- ATI Radeon DRM template customization -*- linux-c -*-
 * Created: Wed Feb 14 17:07:34 2001 by gareth@valinux.com
 *
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#ifndef __RADEON_H__
#define __RADEON_H__

/* This remains constant for all DRM template files.
 */
#define DRM(x) radeon_##x

/* General customization:
 */
#define __HAVE_AGP		1
#define __MUST_HAVE_AGP		0
#define __HAVE_MTRR		1
#define __HAVE_CTX_BITMAP	1
#define __HAVE_SG		1
#define __HAVE_PCI_DMA		1

#define DRIVER_AUTHOR		"Gareth Hughes, Keith Whitwell, others."

#define DRIVER_NAME		"radeon"
#define DRIVER_DESC		"ATI Radeon"
#define DRIVER_DATE		"20020611"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		5
#define DRIVER_PATCHLEVEL	0

/* Interface history:
 *
 * 1.1 - ??
 * 1.2 - Add vertex2 ioctl (keith)
 *     - Add stencil capability to clear ioctl (gareth, keith)
 *     - Increase MAX_TEXTURE_LEVELS (brian)
 * 1.3 - Add cmdbuf ioctl (keith)
 *     - Add support for new radeon packets (keith)
 *     - Add getparam ioctl (keith)
 *     - Add flip-buffers ioctl, deprecate fullscreen foo (keith).
 * 1.4 - Add scratch registers to get_param ioctl.
 * 1.5 - Add r200 packets to cmdbuf ioctl
 *     - Add r200 function to init ioctl
 *     - Add 'scalar2' instruction to cmdbuf
 */
#define DRIVER_IOCTLS							     \
 [DRM_IOCTL_NR(DRM_IOCTL_DMA)]               = { radeon_cp_buffers,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_INIT)]    = { radeon_cp_init,     1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_START)]   = { radeon_cp_start,    1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_STOP)]    = { radeon_cp_stop,     1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_RESET)]   = { radeon_cp_reset,    1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CP_IDLE)]    = { radeon_cp_idle,     1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_RESET)]    = { radeon_engine_reset,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_FULLSCREEN)] = { radeon_fullscreen,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_SWAP)]       = { radeon_cp_swap,     1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CLEAR)]      = { radeon_cp_clear,    1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_VERTEX)]     = { radeon_cp_vertex,   1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_INDICES)]    = { radeon_cp_indices,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_TEXTURE)]    = { radeon_cp_texture,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_STIPPLE)]    = { radeon_cp_stipple,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_INDIRECT)]   = { radeon_cp_indirect, 1, 1 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_VERTEX2)]    = { radeon_cp_vertex2,  1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_CMDBUF)]     = { radeon_cp_cmdbuf,   1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_GETPARAM)]   = { radeon_cp_getparam, 1, 0 }, \
 [DRM_IOCTL_NR(DRM_IOCTL_RADEON_FLIP)]       = { radeon_cp_flip,     1, 0 }, 

/* Driver customization:
 */
#define DRIVER_PRERELEASE() do {					\
	if ( dev->dev_private ) {					\
		drm_radeon_private_t *dev_priv = dev->dev_private;	\
		if ( dev_priv->page_flipping ) {			\
			radeon_do_cleanup_pageflip( dev );		\
		}							\
	}								\
} while (0)

#define DRIVER_PRETAKEDOWN() do {					\
	if ( dev->dev_private ) radeon_do_cleanup_cp( dev );		\
} while (0)

/* DMA customization:
 */
#define __HAVE_DMA		1

/* Buffer customization:
 */
#define DRIVER_BUF_PRIV_T	drm_radeon_buf_priv_t

#define DRIVER_AGP_BUFFERS_MAP( dev )					\
	((drm_radeon_private_t *)((dev)->dev_private))->buffers

#endif
