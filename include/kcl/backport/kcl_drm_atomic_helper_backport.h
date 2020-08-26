/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_ATOMIC_HELPER_BACKPORT_H
#define AMDKCL_DRM_ATOMIC_HELPER_BACKPORT_H

#include <kcl/kcl_drm_atomic_helper.h>

/*
 * commit v4.14-rc4-1-g78279127253a
 * drm/atomic: Unref duplicated drm_atomic_state in drm_atomic_helper_resume()
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 15, 0) && \
	defined(HAVE_DRM_ATOMIC_STATE_PUT)
static inline
int _kcl_drm_atomic_helper_resume(struct drm_device *dev,
					 struct drm_atomic_state *state)
{
	unsigned int prev, after;
	int ret;

	prev = kref_read(&state->ref);

	drm_atomic_state_get(state);
	ret = drm_atomic_helper_resume(dev, state);

	after = kref_read(&state->ref);
	drm_atomic_state_put(state);
	if (prev != after)
		drm_atomic_state_put(state);

	return ret;
}
#define drm_atomic_helper_resume _kcl_drm_atomic_helper_resume
#endif

#ifdef AMDKCL__DRM_ATOMIC_HELPER_PLANE_RESET
#define __drm_atomic_helper_plane_reset _kcl__drm_atomic_helper_plane_reset
#endif /* AMDKCL__DRM_ATOMIC_HELPER_PLANE_RESET */

#endif
