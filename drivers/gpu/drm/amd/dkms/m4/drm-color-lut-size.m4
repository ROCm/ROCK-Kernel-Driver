dnl #
dnl # commit 5488dc16fde74595a40c5d20ae52d978313f0b4e
dnl # drm: introduce pipe color correction properties
dnl #
AC_DEFUN([AC_AMDGPU_DRM_COLOR_LUT_SIZE],
	[AC_MSG_CHECKING([whether drm_color_lut_size() is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_mode.h>
		#include <drm/drm_property.h>
		#include <drm/drm_color_mgmt.h>
	], [
		struct drm_property_blob blob;
		drm_color_lut_size(&blob);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_COLOR_LUT_SIZE, 1, [drm_color_lut structure is defined])
	], [
		AC_MSG_RESULT(no)
	])
])
