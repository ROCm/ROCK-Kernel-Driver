dnl #
dnl # commit b962a12050a387e4bbf3a48745afe1d29d396b0d
dnl # drm/atomic: integrate modeset lock with private objects
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_atomic.h>
		], [
			drm_atomic_private_obj_init(NULL, NULL, NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_ATOMIC_PRIVATE_OBJ_INIT_P_P_P_P, 1,
				[drm_atomic_private_obj_init() has p,p,p,p interface])
		])
	])
])
