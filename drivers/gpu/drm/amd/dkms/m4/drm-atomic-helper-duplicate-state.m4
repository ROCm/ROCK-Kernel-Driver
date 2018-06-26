dnl #
dnl # commit 397fd77c0491ceb0ed4783eb88fc05d0222e2030
dnl # drm/atomic-helper: Implement drm_atomic_helper_duplicate_state()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_DUPLICATE_STATE],
	[AC_MSG_CHECKING([whether drm_atomic_helper_duplicate_state() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_atomic_helper.h>
	], [
		drm_atomic_helper_duplicate_state(NULL, NULL);
	], [drm_atomic_helper_duplicate_state], [drivers/gpu/drm/drm_atomic_helper.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE, 1, [drm_atomic_helper_duplicate_state() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
