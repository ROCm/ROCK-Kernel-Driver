dnl #
dnl # commit 1494276000db789c6d2acd85747be4707051c801
dnl # drm/atomic-helper: Implement subsystem-level suspend/resume
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_SUSPEND_RESUME], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_atomic_helper.h>
		], [
			drm_atomic_helper_suspend(NULL);
			drm_atomic_helper_resume(NULL, NULL);
		], [drm_atomic_helper_suspend drm_atomic_helper_resume], [drivers/gpu/drm/drm_atomic_helper.c], [
			AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_SUSPEND_RESUME, 1,
				[drm_atomic_helper_suspend() is available])
		])
	])
])
