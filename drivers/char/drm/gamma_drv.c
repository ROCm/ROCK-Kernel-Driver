/* gamma.c -- 3dlabs GMX 2000 driver -*- linux-c -*-
 * Created: Mon Jan  4 08:58:31 1999 by faith@precisioninsight.com
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
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "gamma.h"
#include "drmP.h"
#include "gamma_drv.h"

#define DRIVER_AUTHOR		"VA Linux Systems Inc."

#define DRIVER_NAME		"gamma"
#define DRIVER_DESC		"3DLabs gamma"
#define DRIVER_DATE		"20010216"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define DRIVER_IOCTLS							  \
	[DRM_IOCTL_NR(DRM_IOCTL_DMA)]	     = { gamma_dma,	  1, 0 }


#define __HAVE_COUNTERS		5
#define __HAVE_COUNTER6		_DRM_STAT_IRQ
#define __HAVE_COUNTER7		_DRM_STAT_DMA
#define __HAVE_COUNTER8		_DRM_STAT_PRIMARY
#define __HAVE_COUNTER9		_DRM_STAT_SPECIAL
#define __HAVE_COUNTER10	_DRM_STAT_MISSED


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
static int __init gamma_options( char *str )
{
	DRM(parse_options)( str );
	return 1;
}

__setup( DRIVER_NAME "=", gamma_options );
#endif


#include "drm_fops.h"
#include "drm_init.h"
#include "drm_ioctl.h"
#include "drm_lists.h"
#include "drm_lock.h"
#include "drm_memory.h"
#include "drm_proc.h"
#include "drm_vm.h"
#include "drm_stub.h"
