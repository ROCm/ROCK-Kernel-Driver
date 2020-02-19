dnl #
dnl # commit 1d2ac403ae3bfde7c50328ee0d39d3fb3d8d9823
dnl # drm: Protect dev->filelist with its own mutex
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_FILELIST_MUTEX], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_device.h>
		],[
			struct drm_device *ddev;
			ddev->filelist_mutex = ddev->filelist_mutex;
		],[
			AC_DEFINE(HAVE_DRM_DEVICE_FILELIST_MUTEX, 1,
				[drm_device->filelist_mutex is available])
		])
	])
])
