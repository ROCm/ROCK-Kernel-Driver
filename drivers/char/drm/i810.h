/* i810.h -- Intel i810/i815 DRM template customization -*- linux-c -*-
 * Created: Thu Feb 15 00:01:12 2001 by gareth@valinux.com
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
 */

#ifndef __I810_H__
#define __I810_H__

/* This remains constant for all DRM template files.
 */
#define DRM(x) i810_##x

/* General customization:
 */

#define DRIVER_AUTHOR		"VA Linux Systems Inc."

#define DRIVER_NAME		"i810"
#define DRIVER_DESC		"Intel i810"
#define DRIVER_DATE		"20030605"

/* Interface history
 *
 * 1.1   - XFree86 4.1
 * 1.2   - XvMC interfaces
 *       - XFree86 4.2
 * 1.2.1 - Disable copying code (leave stub ioctls for backwards compatibility)
 *       - Remove requirement for interrupt (leave stubs again)
 * 1.3   - Add page flipping.
 * 1.4   - fix DRM interface
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		4
#define DRIVER_PATCHLEVEL	0

#define DRIVER_IOCTLS							    \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_INIT)]   = { i810_dma_init,    1, 1 }, \
   	[DRM_IOCTL_NR(DRM_IOCTL_I810_VERTEX)] = { i810_dma_vertex,  1, 0 }, \
   	[DRM_IOCTL_NR(DRM_IOCTL_I810_CLEAR)]  = { i810_clear_bufs,  1, 0 }, \
      	[DRM_IOCTL_NR(DRM_IOCTL_I810_FLUSH)]  = { i810_flush_ioctl, 1, 0 }, \
   	[DRM_IOCTL_NR(DRM_IOCTL_I810_GETAGE)] = { i810_getage,      1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_GETBUF)] = { i810_getbuf,      1, 0 }, \
   	[DRM_IOCTL_NR(DRM_IOCTL_I810_SWAP)]   = { i810_swap_bufs,   1, 0 }, \
   	[DRM_IOCTL_NR(DRM_IOCTL_I810_COPY)]   = { i810_copybuf,     1, 0 }, \
   	[DRM_IOCTL_NR(DRM_IOCTL_I810_DOCOPY)] = { i810_docopy,      1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_OV0INFO)] = { i810_ov0_info,   1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_FSTATUS)] = { i810_fstatus,    1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_OV0FLIP)] = { i810_ov0_flip,   1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_MC)]      = { i810_dma_mc,     1, 1 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_RSTATUS)] = { i810_rstatus,    1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I810_FLIP)] =    { i810_flip_bufs,  1, 0 }

#endif
