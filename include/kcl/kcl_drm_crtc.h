/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *   For codes copied from include/drm/drm_crtc.h
 *
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *   For codes copied from include/drm/drm_crtc_helper.h
 *
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007 Jakob Bornecrantz <wallbraker@gmail.com>
 * Copyright (c) 2008 Red Hat Inc.
 * Copyright (c) 2007-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * Copyright (c) 2007-2008 Intel Corporation
 *   For codes copied from include/drm/drm_mode.h
 *
 * Copyright 2018 Intel Corporation
 *   For codes copied from include/drm/drm_util.h
 *
 * Copyright (c) 2016 Intel Corporation
 *   For codes copied from include/drm/drm_encoder.h
 *
 * Copyright (c) 2016 Intel Corporation
 *   For codes copied from include/drm/drm_framebuffer.h
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef KCL_KCL_DRM_CRTC_H
#define KCL_KCL_DRM_CRTC_H

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>

/* Copied from include/drm/drm_mode.h */
#ifndef DRM_MODE_ROTATE_0
#define DRM_MODE_ROTATE_0       (1<<0)
#endif
#ifndef DRM_MODE_ROTATE_90
#define DRM_MODE_ROTATE_90      (1<<1)
#endif
#ifndef DRM_MODE_ROTATE_180
#define DRM_MODE_ROTATE_180     (1<<2)
#endif
#ifndef DRM_MODE_ROTATE_270
#define DRM_MODE_ROTATE_270     (1<<3)
#endif

#ifndef DRM_MODE_ROTATE_MASK
#define DRM_MODE_ROTATE_MASK (\
		DRM_MODE_ROTATE_0  | \
		DRM_MODE_ROTATE_90  | \
		DRM_MODE_ROTATE_180 | \
		DRM_MODE_ROTATE_270)
#endif

#if !defined(HAVE_DRM_CRTC_FORCE_DISABLE_ALL)
extern int drm_crtc_force_disable(struct drm_crtc *crtc);
extern int drm_crtc_force_disable_all(struct drm_device *dev);
#endif

/**
 * drm_color_lut_size - calculate the number of entries in the LUT
 * @blob: blob containing the LUT
 *
 * Returns:
 * The number of entries in the color LUT stored in @blob.
 */
#if !defined(HAVE_DRM_COLOR_LUT_SIZE)
static inline int drm_color_lut_size(const struct drm_property_blob *blob)
{
	return blob->length / sizeof(struct drm_color_lut);
}
#endif

#if !defined(HAVE_DRM_CRTC_ACCURATE_VBLANK_COUNT)
static inline u64 drm_crtc_accurate_vblank_count(struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ACCURATE_VBLANK_COUNT)
	return drm_accurate_vblank_count(crtc);
#else
	pr_warn_once("%s is not supported\n", __func__);
	return 0;
#endif
}
#endif

#endif
