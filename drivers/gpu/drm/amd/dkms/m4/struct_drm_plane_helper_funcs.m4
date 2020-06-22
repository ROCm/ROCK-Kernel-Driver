dnl #
dnl # v4.12-rc7-1335-gfef9df8b5945
dnl # drm/atomic: initial support for asynchronous plane update
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_PLANE_HELPER_FUNCS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_modeset_helper_vtables.h>
		], [
			struct drm_plane_helper_funcs *funcs = NULL;
			funcs->atomic_async_check(NULL, NULL);
			funcs->atomic_async_update(NULL, NULL);
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_PLANE_HELPER_FUNCS_ATOMIC_ASYNC_CHECK, 1,
				[drm_plane_helper_funcs->atomic_async_check() is available])
		])
	])
])
