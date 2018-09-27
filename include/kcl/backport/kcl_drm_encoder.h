/* SPDX-License-Identifier: MIT */
#ifndef KCL_BACKPORT_KCL_DRM_ENCODER_H
#define KCL_BACKPORT_KCL_DRM_ENCODER_H

#include <kcl/header/kcl_drm_encoder_h.h>

#if !defined(HAVE_DRM_ENCODER_FIND_VALID_WITH_FILE)
#define drm_encoder_find(dev, file, id) drm_encoder_find(dev, id)
#endif

#endif
