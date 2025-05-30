/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
 * Copyright (C) 2018 Intel Corp.
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
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Daniel Vetter <daniel.vetter@ffwll.ch>
 */
#ifndef AMDKCL_DRM_ATOMIC_HELPER_H
#define AMDKCL_DRM_ATOMIC_HELPER_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include <kcl/kcl_drm_modes.h>
#include <kcl/kcl_drm_crtc.h>

/* drm/atomic-helper: Remove _HELPER_ infix from DRM_PLANE_HELPER_NO_SCALING */
#ifndef DRM_PLANE_NO_SCALING
#define DRM_PLANE_NO_SCALING (1<<16)
#endif

/*
 * v4.19-rc1-206-ge267364a6e1b
 * drm/atomic: Initialise planes with opaque alpha values
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 20, 0)
#define AMDKCL__DRM_ATOMIC_HELPER_PLANE_RESET
void _kcl__drm_atomic_helper_plane_reset(struct drm_plane *plane,
							struct drm_plane_state *state);
#endif

#ifndef HAVE___DRM_ATOMIC_HELPER_CRTC_RESET
void __drm_atomic_helper_crtc_reset(struct drm_crtc *crtc,
                              struct drm_crtc_state *crtc_state);
#endif

#ifndef HAVE_DRM_ATOMIC_HELPER_CALC_TIMESTAMPING_CONSTANTS
void drm_atomic_helper_calc_timestamping_constants(struct drm_atomic_state *state);
#endif

#ifndef HAVE_DRM_ATOMIC_PLANE_ENABLING
static inline bool drm_atomic_plane_enabling(struct drm_plane_state *old_plane_state,
					     struct drm_plane_state *new_plane_state)
{
	/*
	 * When enabling a plane, CRTC and FB should always be set together.
	 * Anything else should be considered a bug in the atomic core, so we
	 * gently warn about it.
	 */
	WARN_ON((!new_plane_state->crtc && new_plane_state->fb) ||
		(new_plane_state->crtc && !new_plane_state->fb));

	return !old_plane_state->crtc && new_plane_state->crtc;
}
#endif

#endif
