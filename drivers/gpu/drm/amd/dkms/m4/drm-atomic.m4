AC_DEFUN([AC_AMDGPU_DRM_ATOMIC], [
dnl #
dnl # commit b962a12050a387e4bbf3a48745afe1d29d396b0d
dnl # drm/atomic: integrate modeset lock with private objects
dnl #
	AC_MSG_CHECKING([whether drm_atomic_private_obj_init() has p,p,p,p interface])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_atomic.h>
	], [
		drm_atomic_private_obj_init(NULL, NULL, NULL, NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_PRIVATE_OBJ_INIT_P_P_P_P, 1, [
			drm_atomic_private_obj_init() has p,p,p,p interface
		])
	], [
		AC_MSG_RESULT(no)
	])
])
