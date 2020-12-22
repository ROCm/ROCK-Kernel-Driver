/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_drm_atomic_helper.h>
#include <kcl/kcl_drm.h>
#include <kcl/header/kcl_drm_vblank_h.h>

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

#ifndef HAVE_DRM_ATOMIC_HELPER_CALC_TIMESTAMPING_CONSTANTS
/*
 * This implementation is duplicated from v5.9-rc5-1595-ge1ad957d45f7
 * "Extract drm_atomic_helper_calc_timestamping_constants()"
 *
 */
void drm_atomic_helper_calc_timestamping_constants(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

#if !defined(for_each_new_crtc_in_state)
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		new_crtc_state = crtc->state;
#else
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
#endif
		if (new_crtc_state->enable)
			drm_calc_timestamping_constants(crtc,
							&new_crtc_state->adjusted_mode);
	}
}
EXPORT_SYMBOL(drm_atomic_helper_calc_timestamping_constants);
#endif
