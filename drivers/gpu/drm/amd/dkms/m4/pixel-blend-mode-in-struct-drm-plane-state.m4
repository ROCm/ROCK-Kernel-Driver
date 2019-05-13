dnl #
dnl # commit a5ec8332d4280500544e316f76c04a7adc02ce03
dnl # Author: Lowry Li <lowry.li@arm.com>
dnl # Date:   Thu Aug 23 16:30:19 2018 +0800
dnl # drm: Add per-plane pixel blend mode property
dnl #
AC_DEFUN([AC_AMDGPU_PIXEL_BLEND_MODE_IN_STRUCT_DRM_PLANE_STATE],
	[AC_MSG_CHECKING([whether there exist pixel_blend_mode in struct drm_plane_state])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_plane.h>
	], [
		struct drm_plane_state state;
		state.pixel_blend_mode = 1;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PIXEL_BLEND_MODE_IN_STRUCT_DRM_PLANE_STATE, 1, [there exist pixel_blend_mode in struct drm_plane_state])
	], [
		AC_MSG_RESULT(no)
	])
])
