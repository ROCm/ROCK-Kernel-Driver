dnl #
dnl # commit c7fcbf2513973208c03a2173cd25a2c48fec6605
dnl # drm/plane: check that fb_damage is set up when used
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PLANE_GET_DAMAGE_CLIPS_COUNT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_plane.h>
		], [
			drm_plane_get_damage_clips_count(NULL);
		], [
			AC_DEFINE(HAVE_DRM_PLANE_GET_DAMAGE_CLIPS_COUNT, 1,
				[drm_plane_get_damage_clips_count function is available])
		])
	])
])
