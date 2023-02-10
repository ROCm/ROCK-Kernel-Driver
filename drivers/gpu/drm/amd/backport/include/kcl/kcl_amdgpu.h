/* SPDX-License-Identifier: MIT */
#ifndef AMDGPU_BACKPORT_KCL_AMDGPU_H
#define AMDGPU_BACKPORT_KCL_AMDGPU_H

#include <drm/drm_vblank.h>
#include "amdgpu.h"
#include <drm/drm_drv.h>

#ifndef HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP
static inline u32 kcl_amdgpu_get_vblank_counter_kms(struct drm_device *dev, unsigned int crtc)
{
	struct drm_crtc *drm_crtc = drm_crtc_from_index(dev, crtc);

	return amdgpu_get_vblank_counter_kms(drm_crtc);
}

static inline int kcl_amdgpu_enable_vblank_kms(struct drm_device *dev, unsigned int crtc)
{
	struct drm_crtc *drm_crtc = drm_crtc_from_index(dev, crtc);

	return amdgpu_enable_vblank_kms(drm_crtc);
}

static inline void kcl_amdgpu_disable_vblank_kms(struct drm_device *dev, unsigned int crtc)
{
	struct drm_crtc *drm_crtc = drm_crtc_from_index(dev, crtc);

	return amdgpu_disable_vblank_kms(drm_crtc);
}

static inline bool kcl_amdgpu_get_crtc_scanout_position(struct drm_device *dev, unsigned int pipe,
				 bool in_vblank_irq, int *vpos, int *hpos,
				 ktime_t *stime, ktime_t *etime,
				 const struct drm_display_mode *mode)
{
	return !!amdgpu_display_get_crtc_scanoutpos(dev, pipe, in_vblank_irq, vpos, hpos, stime, etime, mode);
}

static inline bool kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int pipe,
					int *max_error,	ktime_t *vblank_time,
					bool in_vblank_irq)
{
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time, in_vblank_irq);
}
#endif /* HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP */

static inline ktime_t kcl_amdgpu_get_vblank_time_ns(struct drm_vblank_crtc *vblank)
{
	return vblank->time;
}

#endif /* AMDGPU_BACKPORT_KCL_AMDGPU_H */
