dnl #
dnl # commit v6.5-rc2-871-g82b599ece3b8
dnl # drm/edid: parse source physical address
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO_SOURCE_PHYSICAL_ADDRESS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		],[
			struct drm_display_info *info = NULL;
			info->source_physical_address = 0;
		],[
			AC_DEFINE(HAVE_DRM_DISPLAY_INFO_SOURCE_PHYSICAL_ADDRESS, 1,
				[struct drm_display_info->source_physical_address is available])
		])
	])
])
