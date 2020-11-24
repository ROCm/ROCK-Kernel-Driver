/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_drm_atomic_helper.h>
#include <kcl/kcl_drm.h>
#include "kcl_common.h"

#ifdef AMDKCL__DRM_ATOMIC_HELPER_PLANE_RESET
void _kcl__drm_atomic_helper_plane_reset(struct drm_plane *plane,
							struct drm_plane_state *state)
{
	state->plane = plane;
	state->rotation = DRM_MODE_ROTATE_0;

#ifdef DRM_BLEND_ALPHA_OPAQUE
	state->alpha = DRM_BLEND_ALPHA_OPAQUE;
#endif
#ifdef DRM_MODE_BLEND_PREMULTI
	state->pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
#endif

	plane->state = state;
}
EXPORT_SYMBOL(_kcl__drm_atomic_helper_plane_reset);
#endif

#ifndef HAVE___DRM_ATOMIC_HELPER_CRTC_RESET
void
__drm_atomic_helper_crtc_reset(struct drm_crtc *crtc,
                              struct drm_crtc_state *crtc_state)
{
       if (crtc_state)
               crtc_state->crtc = crtc;

       crtc->state = crtc_state;
}
EXPORT_SYMBOL(__drm_atomic_helper_crtc_reset);
#endif
