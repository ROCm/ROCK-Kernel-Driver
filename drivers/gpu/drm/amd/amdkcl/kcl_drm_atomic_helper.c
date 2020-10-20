/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_drm_atomic_helper.h>
#include <kcl/kcl_drm.h>
#include "kcl_common.h"

#if !defined(HAVE_DRM_ATOMIC_HELPER_DISABLE_ALL)
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	int err;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	drm_for_each_connector(conn, dev) {
		struct drm_crtc *crtc = conn->state->crtc;
		struct drm_crtc_state *crtc_state;

		if (!crtc || conn->dpms != DRM_MODE_DPMS_ON)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}

		crtc_state->active = false;
	}

	err = drm_atomic_commit(state);

free:
	if (err < 0)
		drm_atomic_state_free(state);

	return err;
}
EXPORT_SYMBOL(drm_atomic_helper_disable_all);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE)
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int err = 0;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->acquire_ctx = ctx;

	drm_for_each_crtc(crtc, dev) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}
	}

	drm_for_each_plane(plane, dev) {
		struct drm_plane_state *plane_state;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			err = PTR_ERR(plane_state);
			goto free;
		}
	}

	drm_for_each_connector(conn, dev) {
		struct drm_connector_state *conn_state;

		conn_state = drm_atomic_get_connector_state(state, conn);
		if (IS_ERR(conn_state)) {
			err = PTR_ERR(conn_state);
			goto free;
		}
	}

	/* clear the acquire context so that it isn't accidentally reused */
	state->acquire_ctx = NULL;

free:
	if (err < 0) {
		drm_atomic_state_free(state);
		state = ERR_PTR(err);
	}

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_duplicate_state);
#endif

static inline struct drm_plane_state *
_kcl_drm_atomic_get_existing_plane_state(struct drm_atomic_state *state,
							  struct drm_plane *plane)
{
#ifdef HAVE_DRM_ATOMIC_STATE_PLANE_STATES
	return state->plane_states[drm_plane_index(plane)];
#else
	return state->planes[drm_plane_index(plane)].state;
#endif
}

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
