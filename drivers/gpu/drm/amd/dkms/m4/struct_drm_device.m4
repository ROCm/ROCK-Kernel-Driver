dnl #
dnl # commit v4.19-rc1-194-g18ace11f87e6
dnl # drm: Introduce per-device driver_features
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_DRIVER_FEATURES], [
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_device.h>
	],[
		struct drm_device *ddev = NULL;
		ddev->driver_features = 0;
	],[
		AC_DEFINE(HAVE_DRM_DEVICE_DRIVER_FEATURES, 1,
			[dev_device->driver_features is available])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_DEVICE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_AMDGPU_DRM_DEVICE_DRIVER_FEATURES
	])
])
