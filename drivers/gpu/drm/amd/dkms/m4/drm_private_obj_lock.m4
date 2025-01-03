dnl #
dnl # commit v4.20-rc4-945-gb962a12050a3
dnl # drm/atomic: integrate modeset lock with private objects
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PRIVATE_OBJ_LOCK], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_atomic.h>
			#include <drm/drm_modeset_lock.h>
		], [
			struct drm_private_obj *obj = NULL;
			struct drm_modeset_lock lock = {};
			obj->lock = lock;
		], [
			AC_DEFINE(HAVE_DRM_PRIVATE_OBJ_LOCK, 1,
				[struct drm_private_obj.lock is available])
		])
	])
])
