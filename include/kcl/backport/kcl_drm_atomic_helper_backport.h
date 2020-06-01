/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_ATOMIC_HELPER_BACKPORT_H
#define AMDKCL_DRM_ATOMIC_HELPER_BACKPORT_H

#include <kcl/kcl_drm_atomic_helper.h>

#ifdef AMDKCL__DRM_ATOMIC_HELPER_PLANE_RESET
#define __drm_atomic_helper_plane_reset _kcl__drm_atomic_helper_plane_reset
#endif /* AMDKCL__DRM_ATOMIC_HELPER_PLANE_RESET */

#endif
