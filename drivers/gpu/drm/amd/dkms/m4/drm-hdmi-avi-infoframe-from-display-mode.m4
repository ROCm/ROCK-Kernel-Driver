dnl 10a8512008655d5ce62f8c56323a6b5bd221c920
dnl # drm: Add HDMI infoframe helpers
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_DRM_HDMI_AVI_INFOFRAME_FROM_DISPLAY_MODE],
	[AC_MSG_CHECKING([whether drm_hdmi_avi_infoframe_from_display_mode() wants 2 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_edid.h>
	], [
		drm_hdmi_avi_infoframe_from_display_mode(NULL, NULL);
	], [drm_hdmi_avi_infoframe_from_display_mode], [drivers/gpu/drm/drm_edid.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_DRM_HDMI_AVI_INFOFRAME_FROM_DISPLAY_MODE, 1, [drm_hdmi_avi_infoframe_from_display_mode() wants 2 args])
	], [
		AC_MSG_RESULT(no)
	])
])
