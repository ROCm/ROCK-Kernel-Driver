dnl #
dnl # commit 1494276000db789c6d2acd85747be4707051c801
dnl # drm/atomic-helper: Implement subsystem-level suspend/resume
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_SUSPEND],
	[AC_MSG_CHECKING([whether drm_atomic_helper_suspend() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_atomic_helper.h>
	], [
		drm_atomic_helper_suspend(NULL);
	], [drm_atomic_helper_suspend], [drivers/gpu/drm/drm_atomic_helper.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_SUSPEND, 1, [drm_atomic_helper_suspend() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
