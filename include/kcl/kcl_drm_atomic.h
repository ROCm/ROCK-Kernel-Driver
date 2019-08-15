/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_ATOMIC_H
#define AMDKCL_DRM_ATOMIC_H

#include <drm/drm_atomic.h>
#ifndef HAVE_DRM_ATOMIC_STATE_PUT
static inline void drm_atomic_state_put(struct drm_atomic_state *state)
{
	return drm_atomic_state_free(state);
}
#endif /* HAVE_DRM_ATOMIC_STATE_PUT */
#endif
