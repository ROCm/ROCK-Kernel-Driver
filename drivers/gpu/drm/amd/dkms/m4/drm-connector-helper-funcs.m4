dnl #
dnl # commit v5.2-rc2-529-g6f3b62781bbd
dnl # drm: Convert connector_helper_funcs->atomic_check to accept drm_atomic_state
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_HELPER_FUNCS_ATOMIC_CHECK], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_modeset_helper_vtables.h>
			#include <drm/drm_atomic.h>
		], [
			struct drm_connector_helper_funcs *p = NULL;
			p->atomic_check(NULL, (struct drm_atomic_state*)NULL);
		], [
			AC_DEFINE(HAVE_DRM_CONNECTOR_HELPER_FUNCS_ATOMIC_CHECK_ARG_DRM_ATOMIC_STATE, 1,
				[drm_connector_helper_funcs->atomic_check() wants struct drm_atomic_state arg])
		])
	])
])
