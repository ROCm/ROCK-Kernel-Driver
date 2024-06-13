dnl #
dnl # commit v6.2-rc6-1230-g169b9182f192
dnl # drm/atomic-helper: Add atomic_enable plane-helper callback
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_PLANE_ENABLING], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_atomic_helper.h>
		], [
			drm_atomic_plane_enabling(NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_ATOMIC_PLANE_ENABLING, 1,
				[drm_atomic_plane_enabling() is available])
		])
	])
])
