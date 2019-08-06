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

#endif
