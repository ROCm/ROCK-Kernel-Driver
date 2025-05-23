/*
 * GEM Graphics Execution Manager Driver Interfaces
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 * Copyright © 2014 Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
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
 */

#ifndef __KCL_KCL_DRM_GEM_H__
#define __KCL_KCL_DRM_GEM_H__

#include <drm/drm_gem.h>

#ifndef HAVE_DRM_PRINT_MEMORY_STATS
enum drm_gem_object_status {
	DRM_GEM_OBJECT_RESIDENT  = BIT(0),
	DRM_GEM_OBJECT_PURGEABLE = BIT(1),
};
#endif

#if defined(HAVE_DRM_GEM_OBJECT_PUT)
#if defined(HAVE_DRM_GEM_OBJECT_PUT_SYMBOL)
static inline void
_kcl_drm_gem_object_put(struct drm_gem_object *obj)
{
	return drm_gem_object_put_unlocked(obj);
}
#endif
#else
static inline void
drm_gem_object_put(struct drm_gem_object *obj)
{
	return drm_gem_object_unreference_unlocked(obj);
}

static inline void
drm_gem_object_get(struct drm_gem_object *obj)
{
	kref_get(&obj->refcount);
}
#endif /* HAVE_DRM_GEM_OBJECT_PUT */

/* copy from include/drm/drm_gem.h */
#ifndef HAVE_DRM_GEM_OBJECT_IS_SHARED_FOR_MEMORY_STATS
static inline bool drm_gem_object_is_shared_for_memory_stats(struct drm_gem_object *obj)
{
        return (obj->handle_count > 1) || obj->dma_buf;
}
#endif /* HAVE_DRM_GEM_OBJECT_IS_SHARED_FOR_MEMORY_STATS */

#endif
