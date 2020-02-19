dnl #
dnl # commit 5488dc16fde74595a40c5d20ae52d978313f0b4e
dnl # drm: introduce pipe color correction properties
dnl #
AC_DEFUN([AC_AMDGPU_DRM_COLOR_LUT_SIZE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_mode.h>
		], [
			struct drm_color_lut lut;
			lut.red = 0;
		], [
			AC_DEFINE(HAVE_DRM_COLOR_LUT, 1,
				[drm_color_lut structure is defined])

			dnl #
			dnl # commit 41204dfeed93f2c7668cf8aa5086bcd96eccaa35
			dnl # drm: Introduce drm_color_lut_size()
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_mode.h>
				#include <drm/drm_property.h>
				struct drm_crtc;
				#include <drm/drm_color_mgmt.h>
			], [
				struct drm_property_blob blob;
				drm_color_lut_size(&blob);
			], [
				AC_DEFINE(HAVE_DRM_COLOR_LUT_SIZE, 1,
					[drm_color_lut_size() is available])
			])
		])
	])
])
