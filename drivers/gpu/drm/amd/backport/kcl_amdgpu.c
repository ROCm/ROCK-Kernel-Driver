#include <kcl/kcl_amdgpu.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0) || \
		defined(OS_NAME_RHEL_6) || \
		defined(OS_NAME_RHEL_7_3)
u32 kcl_amdgpu_get_vblank_counter_kms(struct drm_device *dev, unsigned int crtc)
{
	return amdgpu_get_vblank_counter_kms(dev, crtc);
}

int kcl_amdgpu_enable_vblank_kms(struct drm_device *dev, unsigned int crtc)
{
	return amdgpu_enable_vblank_kms(dev, crtc);
}

void kcl_amdgpu_disable_vblank_kms(struct drm_device *dev, unsigned int crtc)
{
	amdgpu_disable_vblank_kms(dev, crtc);
}

int kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int crtc,
					int *max_error,
					struct timeval *vblank_time,
					unsigned flags)
{
	return amdgpu_get_vblank_timestamp_kms(dev, crtc, max_error, vblank_time, flags);
}

int kcl_amdgpu_get_crtc_scanoutpos(struct drm_device *dev, unsigned int crtc,
				   unsigned int flags, int *vpos, int *hpos,
				   ktime_t *stime, ktime_t *etime,
				   const struct drm_display_mode *mode)
{
	return amdgpu_get_crtc_scanoutpos(dev, crtc, flags, vpos, hpos, stime, etime, mode);
}
#else
u32 kcl_amdgpu_get_vblank_counter_kms(struct drm_device *dev, int crtc)
{
	return amdgpu_get_vblank_counter_kms(dev, crtc);
}

int kcl_amdgpu_enable_vblank_kms(struct drm_device *dev, int crtc)
{
	return amdgpu_enable_vblank_kms(dev, crtc);
}

void kcl_amdgpu_disable_vblank_kms(struct drm_device *dev, int crtc)
{
	amdgpu_disable_vblank_kms(dev, crtc);
}

int kcl_amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, int crtc,
					int *max_error,
					struct timeval *vblank_time,
					unsigned flags)
{
	return amdgpu_get_vblank_timestamp_kms(dev, crtc, max_error, vblank_time, flags);
}

int kcl_amdgpu_get_crtc_scanoutpos(struct drm_device *dev, int crtc, unsigned int flags,
				   int *vpos, int *hpos, ktime_t *stime, ktime_t *etime)
{
	return amdgpu_get_crtc_scanoutpos(dev, crtc, flags, vpos, hpos, stime, etime, NULL);
}
#endif
