dnl #
dnl # commit v5.5-rc2-1419-g7e13ad896484
dnl # drm: Avoid drm_global_mutex for simple inc/dec of dev->open_count
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_OPEN_COUNT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_device.h>
		],[
			struct drm_device *ddev = NULL;
			ddev->open_count = 0;
		],[
			AC_DEFINE(HAVE_DRM_DEVICE_OPEN_COUNT_INT, 1,
				[drm_device->open_count is int])
		])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_DEVICE], [
	AC_AMDGPU_DRM_DEVICE_OPEN_COUNT
])
