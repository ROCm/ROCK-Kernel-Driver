/* SPDX-License-Identifier: MIT */
#ifndef AMDGPU_BACKPORT_KCL_AMDGPU_H
#define AMDGPU_BACKPORT_KCL_AMDGPU_H

#include <drm/drm_vblank.h>
#include "amdgpu.h"

#ifndef HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP

#if defined(HAVE_VGA_USE_UNSIGNED_INT_PIPE)
static inline u32 kcl_amdgpu_get_vblank_counter_kms(struct drm_device *dev, unsigned int crtc)
#else
static inline u32 kcl_amdgpu_get_vblank_counter_kms(struct drm_device *dev, int crtc)
#endif
{
	struct drm_crtc *drm_crtc = drm_crtc_from_index(dev, crtc);

	return amdgpu_get_vblank_counter_kms(drm_crtc);
}

#if defined(HAVE_VGA_USE_UNSIGNED_INT_PIPE)
static inline int kcl_amdgpu_enable_vblank_kms(struct drm_device *dev, unsigned int crtc)
#else
static inline int kcl_amdgpu_enable_vblank_kms(struct drm_device *dev, int crtc)
#endif
{
	struct drm_crtc *drm_crtc = drm_crtc_from_index(dev, crtc);

	return amdgpu_enable_vblank_kms(drm_crtc);
}

#if defined(HAVE_VGA_USE_UNSIGNED_INT_PIPE)
static inline void kcl_amdgpu_disable_vblank_kms(struct drm_device *dev, unsigned int crtc)
#else
static inline void kcl_amdgpu_disable_vblank_kms(struct drm_device *dev, int crtc)
#endif
{
	struct drm_crtc *drm_crtc = drm_crtc_from_index(dev, crtc);

	return amdgpu_disable_vblank_kms(drm_crtc);
}

#if defined(HAVE_GET_SCANOUT_POSITION_RETURN_BOOL)
static inline bool kcl_amdgpu_get_crtc_scanout_position(struct drm_device *dev, unsigned int pipe,
				 bool in_vblank_irq, int *vpos, int *hpos,
				 ktime_t *stime, ktime_t *etime,
				 const struct drm_display_mode *mode)
{
	return !!amdgpu_display_get_crtc_scanoutpos(dev, pipe, in_vblank_irq, vpos, hpos, stime, etime, mode);
}
#elif defined(HAVE_VGA_USE_UNSIGNED_INT_PIPE)
static inline int kcl_amdgpu_get_crtc_scanout_position(struct drm_device *dev, unsigned int crtc,
				   unsigned int flags, int *vpos, int *hpos,
				   ktime_t *stime, ktime_t *etime,
				   const struct drm_display_mode *mode)
{
	return amdgpu_display_get_crtc_scanoutpos(dev, crtc, flags, vpos, hpos, stime, etime, mode);
}
#elif defined(HAVE_GET_SCANOUT_POSITION_HAS_DRM_DISPLAY_MODE_ARG)
static inline int kcl_amdgpu_get_crtc_scanout_position(struct drm_device *dev, int crtc,
					   unsigned int flags,
					   int *vpos, int *hpos,
					   ktime_t *stime, ktime_t *etime,
					   const struct drm_display_mode *mode)
{
	return amdgpu_display_get_crtc_scanoutpos(dev, crtc, flags, vpos, hpos, stime, etime, mode);
}
#elif defined(HAVE_GET_SCANOUT_POSITION_HAS_TIMESTAMP_ARG)
static inline int kcl_amdgpu_get_crtc_scanout_position(struct drm_device *dev, int crtc,
					   int *vpos, int *hpos, ktime_t *stime,
					   ktime_t *etime)
{
	return amdgpu_display_get_crtc_scanoutpos(dev, crtc, 0, vpos, hpos, stime, etime, NULL);
}
#else
static inline int kcl_amdgpu_get_crtc_scanout_position(struct drm_device *dev, int crtc,
					   int *vpos, int *hpos)
{
	return amdgpu_display_get_crtc_scanoutpos(dev, crtc, 0, vpos, hpos, NULL, NULL, NULL);
}
#endif

#if defined(HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_HAS_KTIME_T)
static inline bool kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int pipe,
					int *max_error,	ktime_t *vblank_time,
					bool in_vblank_irq)
{
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time, in_vblank_irq);
}
#elif defined(HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_HAS_BOOL_IN_VBLANK_IRQ)
static inline bool kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int pipe,
						int *max_error, struct timeval *vblank_time,
						bool in_vblank_irq)
{
	return !!amdgpu_get_vblank_timestamp_kms(dev, pipe, max_error, vblank_time, in_vblank_irq);
}
#elif defined(HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_RETURN_BOOL)
static inline bool kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int pipe,
						int *max_error, struct timeval *vblank_time,
						unsigned flags)
{
	return !!amdgpu_get_vblank_timestamp_kms(dev, pipe, max_error, vblank_time, flags);
}
#elif defined(HAVE_VGA_USE_UNSIGNED_INT_PIPE)
static inline int kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int pipe,
					int *max_error,	struct timeval *vblank_time,
					unsigned flags)
{
	return amdgpu_get_vblank_timestamp_kms(dev, pipe, max_error, vblank_time, flags);
}
#else
static inline int kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, int crtc,
					int *max_error,
					struct timeval *vblank_time,
					unsigned flags)
{
	return amdgpu_get_vblank_timestamp_kms(dev, crtc, max_error, vblank_time, flags);
}
#endif
#endif /* HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP */
#endif /* AMDGPU_BACKPORT_KCL_AMDGPU_H */
