/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_VBLANK_H_H
#define AMDKCL_DRM_VBLANK_H_H

#ifdef HAVE_DRM_DRM_VBLANK_H
#include <drm/drm_vblank.h>
#else
#include <kcl/header/kcl_drm_irq_h.h>
#endif
#endif
