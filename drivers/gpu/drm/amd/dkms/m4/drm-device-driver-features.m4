dnl #
dnl # commit 18ace11f87e69454379a3a1247a657b70ca142f
dnl # drm: Introduce per-device driver_features
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_DRIVER_FEATURES],
	[AC_MSG_CHECKING([whether drm_device->driver_features is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_device.h>
	],[
		struct drm_device *ddev = NULL;
		ddev->driver_features = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DEVICE_DRIVER_FEATURES, 1, [dev_device->driver_features is available])
	],[
		AC_MSG_RESULT(no)
	])
])
