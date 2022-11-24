dnl #
dnl # commit: v6.1-rc1-27-30c637151cfa
dnl # drm/plane-helper: Export individual helpers
dnl #

AC_DEFUN([AC_AMDGPU_DRM_PLANE_HELPER_DESTROY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_plane_helper.h>
		], [
			drm_plane_helper_destroy(NULL);
		], [
			AC_DEFINE(HAVE_DRM_PLANE_HELPER_DESTROY, 1,
				[drm_plane_helper_destroy() is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_DRM_PLANE_HELPER_FUNCS], [
        AC_AMDGPU_DRM_PLANE_HELPER_DESTROY
])
