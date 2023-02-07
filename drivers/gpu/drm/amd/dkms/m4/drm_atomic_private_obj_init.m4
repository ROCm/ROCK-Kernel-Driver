dnl #
dnl # commit v4.20-rc4-945-gb962a12050a3
dnl # drm/atomic: integrate modeset lock with private objects
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_PRIVATE_OBJ_INIT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_atomic.h>
		], [
			drm_atomic_private_obj_init(NULL, NULL, NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_ATOMIC_PRIVATE_OBJ_INIT_4ARGS, 1,
				[drm_atomic_private_obj_init() wants 4 args])
		])
	])
])
