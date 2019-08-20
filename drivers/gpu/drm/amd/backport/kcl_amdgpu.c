#include <kcl/kcl_amdgpu.h>

#if defined(HAVE_VGA_USE_UNSIGNED_INT_PIPE)
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

int kcl_amdgpu_display_get_crtc_scanoutpos(struct drm_device *dev, unsigned int crtc,
				   unsigned int flags, int *vpos, int *hpos,
				   ktime_t *stime, ktime_t *etime,
				   const struct drm_display_mode *mode)
{
	return amdgpu_display_get_crtc_scanoutpos(dev, crtc, flags, vpos, hpos, stime, etime, mode);
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

int kcl_amdgpu_display_get_crtc_scanoutpos(struct drm_device *dev, int crtc, unsigned int flags,
				   int *vpos, int *hpos, ktime_t *stime, ktime_t *etime)
{
	return amdgpu_display_get_crtc_scanoutpos(dev, crtc, flags, vpos, hpos, stime, etime, NULL);
}
#endif
