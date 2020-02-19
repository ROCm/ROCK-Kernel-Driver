dnl #
dnl # commit 4c18d3010bb722e6ab7d0b80fcf78dd6f95a5a15
dnl # drm/atomic-helpers: Export drm_atomic_helper_update_legacy_modeset_state
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_UPDATE_LEGACY_MODESET_STATE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_atomic_helper.h>
		], [
			drm_atomic_helper_update_legacy_modeset_state(NULL,NULL);
		], [drm_atomic_helper_update_legacy_modeset_state],[drivers/gpu/drm/drm_atomic_helper.c],[
			AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_UPDATE_LEGACY_MODESET_STATE, 1,
				[drm_atomic_helper_update_legacy_modeset_state() is available])
		])
	])
])
