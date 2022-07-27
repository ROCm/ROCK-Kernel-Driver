dnl #
dnl # commit v4.13-rc5-826-g6f8bcc744aad
dnl # drm/atomic: Prepare drm_modeset_lock infrastructure for interruptible waiting, v2.
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MODESET_BACKOFF_RETURN_INT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_modeset_lock.h>
		], [
			int ret;
			ret = drm_modeset_backoff(NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_MODESET_BACKOFF_RETURN_INT, 1,
				[drm_modeset_backoff() has int return])
		])
	])
])
