/* mga_drv.c -- Matrox G200/G400 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:56:22 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "mga.h"
#include "drmP.h"
#include "mga_drv.h"

#define DRIVER_AUTHOR		"Gareth Hughes, VA Linux Systems Inc."

#define DRIVER_NAME		"mga"
#define DRIVER_DESC		"Matrox G200/G400"
#define DRIVER_DATE		"20010321"

#define DRIVER_MAJOR		3
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	2

#define DRIVER_IOCTLS							   \
	[DRM_IOCTL_NR(DRM_IOCTL_DMA)]	      = { mga_dma_buffers, 1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_INIT)]    = { mga_dma_init,    1, 1 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_FLUSH)]   = { mga_dma_flush,   1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_RESET)]   = { mga_dma_reset,   1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_SWAP)]    = { mga_dma_swap,    1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_CLEAR)]   = { mga_dma_clear,   1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_VERTEX)]  = { mga_dma_vertex,  1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_INDICES)] = { mga_dma_indices, 1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_ILOAD)]   = { mga_dma_iload,   1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_BLIT)]    = { mga_dma_blit,    1, 0 },


#define __HAVE_COUNTERS         3
#define __HAVE_COUNTER6         _DRM_STAT_IRQ
#define __HAVE_COUNTER7         _DRM_STAT_PRIMARY
#define __HAVE_COUNTER8         _DRM_STAT_SECONDARY


#include "drm_agpsupport.h"
#include "drm_auth.h"
#include "drm_bufs.h"
#include "drm_context.h"
#include "drm_dma.h"
#include "drm_drawable.h"
#include "drm_drv.h"

#ifndef MODULE
/* DRM(options) is called by the kernel to parse command-line options
 * passed via the boot-loader (e.g., LILO).  It calls the insmod option
 * routine, drm_parse_drm.
 */

/* JH- We have to hand expand the string ourselves because of the cpp.  If
 * anyone can think of a way that we can fit into the __setup macro without
 * changing it, then please send the solution my way.
 */
static int __init mga_options( char *str )
{
	DRM(parse_options)( str );
	return 1;
}

__setup( DRIVER_NAME "=", mga_options );
#endif


#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lock.h"
#include "drm_memory.h"
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"
