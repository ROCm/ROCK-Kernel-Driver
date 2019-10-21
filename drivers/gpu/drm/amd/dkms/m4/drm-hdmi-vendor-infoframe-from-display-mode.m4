dnl f1781e9bb2dd2305d8d7ffbede1888ae22119557
dnl # drm/edid: Allow HDMI infoframe without VIC or S3D
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_DRM_HDMI_VENDOR_INFOFRAME_FROM_DISPLAY_MODE],
	[AC_MSG_CHECKING([whether drm_hdmi_vendor_infoframe_from_display_mode() wants 2 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_edid.h>
	], [
		drm_hdmi_vendor_infoframe_from_display_mode(NULL, 0);
	], [drm_hdmi_vendor_infoframe_from_display_mode], [drivers/gpu/drm/drm_edid.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_DRM_HDMI_VENDOR_INFOFRAME_FROM_DISPLAY_MODE, 1, [drm_hdmi_vendor_infoframe_from_display_mode() wants 2 args])
	], [
		AC_MSG_RESULT(no)
	])
])
