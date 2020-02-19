dnl #
dnl # 4.10 API
dnl # commit 4cd39917ddb2fb5691e05b13b13f1f2398343b3e
dnl # drm/atomic: Add __drm_atomic_helper_connector_reset, v2.
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_CONNECTOR_RESET], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_atomic_helper.h>
		], [
			__drm_atomic_helper_connector_reset(NULL, NULL);
		], [__drm_atomic_helper_connector_reset], [drivers/gpu/drm/drm_atomic_state_helper.c], [
			AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_CONNECTOR_RESET, 1,
				[__drm_atomic_helper_connector_reset() is available])
		])
	])
])
