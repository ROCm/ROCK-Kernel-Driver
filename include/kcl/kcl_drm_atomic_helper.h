/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_ATOMIC_HELPER_H
#define AMDKCL_DRM_ATOMIC_HELPER_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include <kcl/kcl_drm.h>

#if !defined(HAVE_DRM_ATOMIC_HELPER_DISABLE_ALL)
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE)
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_SUSPEND_RESUME)
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev);
int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_CONNECTOR_RESET)
extern void
__kcl_drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state);

static inline void
__drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state)
{
	return __kcl_drm_atomic_helper_connector_reset(connector, conn_state);
}
#endif

#ifndef HAVE_DRM_ATOMIC_HELPER_UPDATE_LEGACY_MODESET_STATE
extern void _kcl_drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state);

static inline void
drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
	_kcl_drm_atomic_helper_update_legacy_modeset_state(dev, old_state);
}
#endif

static inline struct drm_crtc_state *
kcl_drm_atomic_get_old_crtc_state_before_commit(struct drm_atomic_state *state,
					    struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_old_crtc_state(state, crtc);
#elif defined(HAVE_DRM_CRTCS_STATE_MEMBER)
	return state->crtcs[drm_crtc_index(crtc)].ptr->state;
#else
	return state->crtcs[drm_crtc_index(crtc)]->state;
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_old_crtc_state_after_commit(struct drm_atomic_state *state,
				  struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_old_crtc_state(state, crtc);
#else
	return drm_atomic_get_existing_crtc_state(state, crtc);
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_new_crtc_state_before_commit(struct drm_atomic_state *state,
				  struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_new_crtc_state(state, crtc);
#else
	return drm_atomic_get_existing_crtc_state(state, crtc);
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_new_crtc_state_after_commit(struct drm_atomic_state *state,
					    struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_new_crtc_state(state, crtc);
#elif defined(HAVE_DRM_CRTCS_STATE_MEMBER)
	return state->crtcs[drm_crtc_index(crtc)].ptr->state;
#else
	return state->crtcs[drm_crtc_index(crtc)]->state;
#endif
}

static inline struct drm_plane_state *
kcl_drm_atomic_get_new_plane_state_before_commit(struct drm_atomic_state *state,
							struct drm_plane *plane)
{
#if defined(HAVE_DRM_ATOMIC_GET_NEW_PLANE_STATE)
	return drm_atomic_get_new_plane_state(state, plane);
#else
	return drm_atomic_get_existing_plane_state(state, plane);
#endif
}

#ifndef HAVE_DRM_ATOMIC_HELPER_CHECK_PLANE_STATE
static inline int
drm_atomic_helper_check_plane_state(struct drm_plane_state *plane_state,
					const struct drm_crtc_state *crtc_state,
					int min_scale,
					int max_scale,
					bool can_position,
					bool can_update_disabled)
{
#ifdef HAVE_DRM_PLANE_HELPER_CHECK_STATE
	struct drm_rect clip = {};
	struct drm_crtc *crtc = crtc_state->crtc;
	if (crtc->enabled)
		drm_mode_get_hv_timing(&crtc->mode, &clip.x2, &clip.y2);
	return drm_plane_helper_check_state(plane_state, &clip, min_scale,
				max_scale, can_position, can_update_disabled);
#else
	return 0;
#endif
}
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

#endif
