dnl #
dnl # commit id:4c5b7f3ae53b02136d38dee46b412ac8a7f6f4ff
dnl # drm/atomic: export drm_atomic_helper_wait_for_fences()
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_WAIT_FOR_FENCES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_atomic_helper.h>
		], [
			drm_atomic_helper_wait_for_fences(NULL, NULL, false);
		], [drm_atomic_helper_wait_for_fences],[drivers/gpu/drm/drm_atomic_helper.c],[
			AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_WAIT_FOR_FENCES, 1,
				[drm_atomic_helper_wait_for_fences() is available])
		])
	])
])
