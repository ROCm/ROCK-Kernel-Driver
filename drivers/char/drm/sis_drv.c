/* sis.c -- sis driver -*- linux-c -*-
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/config.h>
#include "sis.h"
#include "drmP.h"
#include "sis_drm.h"
#include "sis_drv.h"

#define DRIVER_AUTHOR	 "SIS"
#define DRIVER_NAME	 "sis"
#define DRIVER_DESC	 "SIS 300/630/540"
#define DRIVER_DATE	 "20010503"
#define DRIVER_MAJOR	 1
#define DRIVER_MINOR	 0
#define DRIVER_PATCHLEVEL  0

#define DRIVER_IOCTLS \
        [DRM_IOCTL_NR(SIS_IOCTL_FB_ALLOC)]   = { sis_fb_alloc,	  1, 0 }, \
        [DRM_IOCTL_NR(SIS_IOCTL_FB_FREE)]    = { sis_fb_free,	  1, 0 }, \
        /* AGP Memory Management */					  \
        [DRM_IOCTL_NR(SIS_IOCTL_AGP_INIT)]   = { sisp_agp_init,	  1, 0 }, \
        [DRM_IOCTL_NR(SIS_IOCTL_AGP_ALLOC)]  = { sisp_agp_alloc,  1, 0 }, \
        [DRM_IOCTL_NR(SIS_IOCTL_AGP_FREE)]   = { sisp_agp_free,	  1, 0 }
#if 0 /* these don't appear to be defined */
	/* SIS Stereo */						 
	[DRM_IOCTL_NR(DRM_IOCTL_CONTROL)]    = { sis_control,	  1, 1 }, 
        [DRM_IOCTL_NR(SIS_IOCTL_FLIP)]       = { sis_flip,	  1, 1 }, 
        [DRM_IOCTL_NR(SIS_IOCTL_FLIP_INIT)]  = { sis_flip_init,	  1, 1 }, 
        [DRM_IOCTL_NR(SIS_IOCTL_FLIP_FINAL)] = { sis_flip_final,  1, 1 }
#endif

#define __HAVE_COUNTERS		5

#include "drm_auth.h"
#include "drm_agpsupport.h"
#include "drm_bufs.h"
#include "drm_context.h"
#include "drm_dma.h"
#include "drm_drawable.h"
#include "drm_drv.h"
#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lists.h"
#include "drm_lock.h"
#include "drm_memory.h"
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"
