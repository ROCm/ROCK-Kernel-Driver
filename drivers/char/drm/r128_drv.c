/* r128_drv.c -- ATI Rage 128 driver -*- linux-c -*-
 * Created: Mon Dec 13 09:47:27 1999 by faith@precisioninsight.com
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
#include "r128.h"
#include "drmP.h"
#include "r128_drv.h"
#include "ati_pcigart.h"

#define DRIVER_AUTHOR		"Gareth Hughes, VA Linux Systems Inc."

#define DRIVER_NAME		"r128"
#define DRIVER_DESC		"ATI Rage 128"
#define DRIVER_DATE		"20010405"

#define DRIVER_MAJOR		2
#define DRIVER_MINOR		1
#define DRIVER_PATCHLEVEL	6

#define DRIVER_IOCTLS							    \
   [DRM_IOCTL_NR(DRM_IOCTL_DMA)]             = { r128_cce_buffers,  1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_INIT)]       = { r128_cce_init,     1, 1 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_CCE_START)]  = { r128_cce_start,    1, 1 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_CCE_STOP)]   = { r128_cce_stop,     1, 1 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_CCE_RESET)]  = { r128_cce_reset,    1, 1 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_CCE_IDLE)]   = { r128_cce_idle,     1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_RESET)]      = { r128_engine_reset, 1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_FULLSCREEN)] = { r128_fullscreen,   1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_SWAP)]       = { r128_cce_swap,     1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_CLEAR)]      = { r128_cce_clear,    1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_VERTEX)]     = { r128_cce_vertex,   1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_INDICES)]    = { r128_cce_indices,  1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_BLIT)]       = { r128_cce_blit,     1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_DEPTH)]      = { r128_cce_depth,    1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_STIPPLE)]    = { r128_cce_stipple,  1, 0 }, \
   [DRM_IOCTL_NR(DRM_IOCTL_R128_INDIRECT)]   = { r128_cce_indirect, 1, 1 },


#if 0
/* GH: Count data sent to card via ring or vertex/indirect buffers.
 */
#define __HAVE_COUNTERS         3
#define __HAVE_COUNTER6         _DRM_STAT_IRQ
#define __HAVE_COUNTER7         _DRM_STAT_PRIMARY
#define __HAVE_COUNTER8         _DRM_STAT_SECONDARY
#endif


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
static int __init r128_options( char *str )
{
	DRM(parse_options)( str );
	return 1;
}

__setup( DRIVER_NAME "=", r128_options );
#endif

#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lock.h"
#include "drm_memory.h"
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"
#include "drm_scatter.h"
