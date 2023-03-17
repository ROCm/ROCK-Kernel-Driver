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

dnl #
dnl # commit v5.18-3347-g721ed0ae5acf
dnl # drm/edid: add a quirk for two LG monitors to get them to work on 10bpc
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO_MAX_DSC_BPP], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		],[
			struct drm_display_info *display_info = NULL;
			display_info->max_dsc_bpp=0;
		],[
			AC_DEFINE(HAVE_DRM_DISPLAY_INFO_MAX_DSC_BPP, 1,
				[display_info->max_dsc_bpp is available])
		])
	])
])

dnl #
dnl # commit v6.1-rc1~27-a61bb3422e8d
dnl # drm/amdgpu_dm: Rely on split out luminance calculation function
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO_LUMINANCE_RANGE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		],[
			struct drm_display_info *display_info = NULL;
			struct drm_luminance_range_info *luminance_range;
			luminance_range = &display_info->luminance_range;
		],[
			AC_DEFINE(HAVE_DRM_DISPLAY_INFO_LUMINANCE_RANGE, 1,
				[display_info->luminance_range is available])
		])
	])
])


AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO], [
	AC_AMDGPU_DRM_DISPLAY_INFO_EDID_HDMI_RGB444_DC_MODES
	AC_AMDGPU_DRM_DISPLAY_INFO_MONITOR_RANGE
	AC_AMDGPU_DRM_DISPLAY_INFO_MAX_DSC_BPP
	AC_AMDGPU_DRM_DISPLAY_INFO_LUMINANCE_RANGE
])
