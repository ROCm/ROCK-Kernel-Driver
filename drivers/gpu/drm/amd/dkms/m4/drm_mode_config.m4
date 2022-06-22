AC_DEFUN([AC_AMDGPU_DRM_MODE_CONFIG_FB_MODIFIERS_NOT_SUPPORTED], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_mode_config *mode_config = NULL;
			mode_config->fb_modifiers_not_supported = true;
		], [
			AC_DEFINE(HAVE_DRM_MODE_CONFIG_FB_MODIFIERS_NOT_SUPPORTED, 1,
				[drm_mode_config->fb_modifiers_not_supported is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_DRM_MODE_CONFIG], [
	AC_AMDGPU_DRM_MODE_CONFIG_FB_MODIFIERS_NOT_SUPPORTED
])

