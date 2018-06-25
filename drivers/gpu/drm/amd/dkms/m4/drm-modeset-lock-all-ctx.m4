dnl #
dnl # commit 06eaae46381737a6236ad6fe81e5358fad3bbbe5
dnl # drm: Implement drm_modeset_lock_all_ctx()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MODESET_LOCK_ALL_CTX], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_modeset_lock.h>
		], [
			drm_modeset_lock_all_ctx(NULL, NULL);
		], [drm_modeset_lock_all_ctx], [drivers/gpu/drm/drm_modeset_lock.c], [
			AC_DEFINE(HAVE_DRM_MODESET_LOCK_ALL_CTX, 1,
				[drm_modeset_lock_all_ctx() is available])
		])
	])
])
