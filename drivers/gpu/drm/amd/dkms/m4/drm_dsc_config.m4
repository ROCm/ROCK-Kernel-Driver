dnl #
dnl # commit v4.20-rc3-804-g19fd5adbb595
dnl # drm/dsc: Define VESA Display Stream Compression Capabilities
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DSC_CONFIG_SIMPLE_422], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dsc.h>
		], [
			struct drm_dsc_config *conf = NULL;
			conf->simple_422 = true;
		], [
			AC_DEFINE(HAVE_DRM_DSC_CONFIG_SIMPLE_422, 1,
				[struct drm_dsc_config has member simple_422])
		])
	])
])
