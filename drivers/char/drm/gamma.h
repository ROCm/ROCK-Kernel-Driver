/* gamma.c -- 3dlabs GMX 2000 driver -*- linux-c -*-
 * Created: Mon Jan  4 08:58:31 1999 by gareth@valinux.com
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifndef __GAMMA_H__
#define __GAMMA_H__

/* This remains constant for all DRM template files.
 */
#define DRM(x) gamma_##x

/* General customization:
 */
#define DRIVER_AUTHOR		"VA Linux Systems Inc."

#define DRIVER_NAME		"gamma"
#define DRIVER_DESC		"3DLabs gamma"
#define DRIVER_DATE		"20010624"

#define DRIVER_MAJOR		2
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define DRIVER_IOCTLS							  \
	[DRM_IOCTL_NR(DRM_IOCTL_DMA)]	     = { gamma_dma,	  1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_GAMMA_INIT)] = { gamma_dma_init,  1, 1 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_GAMMA_COPY)] = { gamma_dma_copy,  1, 1 }

#define IOCTL_TABLE_NAME	DRM(ioctls)
#define IOCTL_FUNC_NAME 	DRM(ioctl)

#define __HAVE_COUNTERS		5
#define __HAVE_COUNTER6		_DRM_STAT_IRQ
#define __HAVE_COUNTER7		_DRM_STAT_DMA
#define __HAVE_COUNTER8		_DRM_STAT_PRIMARY
#define __HAVE_COUNTER9		_DRM_STAT_SPECIAL
#define __HAVE_COUNTER10	_DRM_STAT_MISSED

/* Driver customization:
 */
#define DRIVER_PRETAKEDOWN() do {					\
	gamma_do_cleanup_dma( dev );					\
} while (0)

/* DMA customization:
 */
#define __HAVE_MULTIPLE_DMA_QUEUES	1
#define __HAVE_DMA_WAITQUEUE		1

/* removed from DRM HAVE_DMA_FREELIST & HAVE_DMA_SCHEDULE */

#endif /* __GAMMA_H__ */
