/* i915.h -- Intel I915 DRM template customization -*- linux-c -*-
 */
/**************************************************************************
 * 
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 **************************************************************************/

#ifndef __I915_H__
#define __I915_H__

/* This remains constant for all DRM template files.
 */
#define DRM(x) i915_##x

/* General customization:
 */

#define DRIVER_AUTHOR		"Tungsten Graphics, Inc."

#define DRIVER_NAME		"i915"
#define DRIVER_DESC		"Intel Graphics"
#define DRIVER_DATE		"20040405"

/* Interface history:
 *
 * 1.1: Original.
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		1
#define DRIVER_PATCHLEVEL	0

#define DRIVER_IOCTLS							    \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_INIT)]   = { i915_dma_init,    1, 1 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_FLUSH)]  = { i915_flush_ioctl, 1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_FLIP)]   = { i915_flip_bufs,   1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_BATCHBUFFER)] = { i915_batchbuffer, 1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_IRQ_EMIT)] = { i915_irq_emit,  1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_IRQ_WAIT)] = { i915_irq_wait,  1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_GETPARAM)] = { i915_getparam,  1, 0 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_SETPARAM)] = { i915_setparam,  1, 1 }, \
        [DRM_IOCTL_NR(DRM_IOCTL_I915_ALLOC)]   = { i915_mem_alloc,  1, 0 }, \
        [DRM_IOCTL_NR(DRM_IOCTL_I915_FREE)]    = { i915_mem_free,    1, 0 }, \
        [DRM_IOCTL_NR(DRM_IOCTL_I915_INIT_HEAP)] = { i915_mem_init_heap, 1, 1 }, \
	[DRM_IOCTL_NR(DRM_IOCTL_I915_CMDBUFFER)] = { i915_cmdbuffer, 1, 0 }

/* We use our own dma mechanisms, not the drm template code.  However,
 * the shared IRQ code is useful to us:
 */
#define __HAVE_PM		1

#endif
