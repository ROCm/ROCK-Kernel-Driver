dnl #
dnl # commit ae0e28265e216dad11d4cbde42fc15e92919af78
dnl # Author: Maxime Ripard <maxime.ripard@bootlin.com>
dnl # Date:   Wed Apr 11 09:39:25 2018 +0200
dnl # drm/blend: Add a generic alpha property
dnl #
AC_DEFUN([AC_AMDGPU_ALPHA_IN_STRUCT_DRM_PLANE_STATE],
	[AC_MSG_CHECKING([for alpha field in drm_plane_state structure])
	AC_KERNEL_TRY_COMPILE([
		#include <uapi/drm/drm_mode.h>
		#include <drm/drm_property.h>
		#include <drm/drm_rect.h>
		#include <drm/drm_plane.h>
	], [
		struct drm_plane_state state;
		state.alpha = 1;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ALPHA_IN_STRUCT_DRM_PLANE_STATE, 1, [there is alpha field in drm_plane_state structure])
	], [
		AC_MSG_RESULT(no)
	])
])
