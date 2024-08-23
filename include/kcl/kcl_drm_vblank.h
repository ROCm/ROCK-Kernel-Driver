/*
 * Copyright 2016 Intel Corp.
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
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_KCL_DRM_VBLANK_H
#define _KCL_KCL_DRM_VBLANK_H

#include <drm/drm_crtc.h>
#include <drm/drm_vblank.h>
#include <drm/drm_device.h>

/*copy from include/drm/drm_vblank.h */
#ifndef HAVE_CRTC_DRM_VBLANK_CRTC
struct drm_vblank_crtc *drm_crtc_vblank_crtc(struct drm_crtc *crtc);
#endif

#endif /*_KCL_KCL_DRM_VBLANK_H */
