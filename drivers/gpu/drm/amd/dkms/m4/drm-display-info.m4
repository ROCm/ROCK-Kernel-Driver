dnl #
dnl # commit v5.6-rc2-1062-ga1d11d1efe4d
dnl # drm/edid: Add function to parse EDID descriptors for monitor range
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO_MONITOR_RANGE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		],[
			struct drm_display_info *info = NULL;
			info->monitor_range.min_vfreq=0;
			info->monitor_range.max_vfreq=0;
		],[
			AC_DEFINE(HAVE_DRM_DISPLAY_INFO_MONITOR_RANGE, 1,
				[struct drm_display_info has monitor_range member])
		])
	])
])

AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO], [
	AC_AMDGPU_DRM_DISPLAY_INFO_MONITOR_RANGE
])
