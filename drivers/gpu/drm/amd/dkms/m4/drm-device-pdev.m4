dnl #
dnl # commit b347e04452ff6382ace8fba9c81f5bcb63be17a6
dnl # drm: Remove pdev field from struct drm_device
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_PDEV], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drmP.h>
            #include <drm/drm_device.h>
		], [
			struct drm_device *pdd = NULL;
			pdd->pdev = NULL;
		], [
			AC_DEFINE(HAVE_DRM_DEVICE_PDEV, 1, [struct drm_device has pdev member])
		])
	])
])
