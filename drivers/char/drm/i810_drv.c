/* i810_drv.c -- I810 driver -*- linux-c -*-
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
 *    Jeff Hartmann <jhartmann@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "i810.h"
#include "drmP.h"
#include "i810_drv.h"

#define DRIVER_AUTHOR		"VA Linux Systems Inc."

#define DRIVER_NAME		"i810"
#define DRIVER_DESC		"Intel i810"
#define DRIVER_DATE		"20010616"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		1
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
   	[DRM_IOCTL_NR(DRM_IOCTL_I810_DOCOPY)] = { i810_docopy,      1, 0 },


#define __HAVE_COUNTERS         4
#define __HAVE_COUNTER6         _DRM_STAT_IRQ
#define __HAVE_COUNTER7         _DRM_STAT_PRIMARY
#define __HAVE_COUNTER8         _DRM_STAT_SECONDARY
#define __HAVE_COUNTER9         _DRM_STAT_DMA


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
static int __init i810_options( char *str )
{
	DRM(parse_options)( str );
	return 1;
}

__setup( DRIVER_NAME "=", i810_options );
#endif

#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lock.h"
#include "drm_lists.h"
#include "drm_memory.h"
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"
