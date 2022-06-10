dnl #
dnl # commit v4.9-rc1-522171951761153172c75b94ae1f4bc9ab631745
dnl # drm: Extract drm_connector.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO_EDID_HDMI_RGB444_DC_MODES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		], [
			struct drm_display_info *display_info = NULL;
			display_info->edid_hdmi_rgb444_dc_modes = 0;
		], [
			AC_DEFINE(HAVE_DRM_DISPLAY_INFO_EDID_HDMI_RGB444_DC_MODES, 1,
				[display_info->edid_hdmi_rgb444_dc_modes is available])
		])
	])
])


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
	AC_AMDGPU_DRM_DISPLAY_INFO_EDID_HDMI_RGB444_DC_MODES
	AC_AMDGPU_DRM_DISPLAY_INFO_MONITOR_RANGE
])
