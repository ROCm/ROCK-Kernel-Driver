#ifndef AMDGPU_BACKPORT_KCL_AMDGPU_H
#define AMDGPU_BACKPORT_KCL_AMDGPU_H

#include <linux/version.h>
#include <amdgpu.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0) || \
		defined(OS_NAME_RHEL_6) || \
		defined(OS_NAME_RHEL_7_3)
u32 kcl_amdgpu_get_vblank_counter_kms(struct drm_device *dev, unsigned int crtc);
int kcl_amdgpu_enable_vblank_kms(struct drm_device *dev, unsigned int crtc);
void kcl_amdgpu_disable_vblank_kms(struct drm_device *dev, unsigned int crtc);
int kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int crtc,
					int *max_error,
					struct timeval *vblank_time,
					unsigned flags);
int kcl_amdgpu_get_crtc_scanoutpos(struct drm_device *dev, unsigned int crtc,
				   unsigned int flags, int *vpos, int *hpos,
				   ktime_t *stime, ktime_t *etime,
				   const struct drm_display_mode *mode);
#else
u32 kcl_amdgpu_get_vblank_counter_kms(struct drm_device *dev, int crtc);
int kcl_amdgpu_enable_vblank_kms(struct drm_device *dev, int crtc);
void kcl_amdgpu_disable_vblank_kms(struct drm_device *dev, int crtc);
int kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, int crtc,
					int *max_error,
					struct timeval *vblank_time,
					unsigned flags);
int kcl_amdgpu_get_crtc_scanoutpos(struct drm_device *dev, int crtc, unsigned int flags,
				   int *vpos, int *hpos, ktime_t *stime, ktime_t *etime);
#endif

#endif /* AMDGPU_BACKPORT_KCL_AMDGPU_H */
