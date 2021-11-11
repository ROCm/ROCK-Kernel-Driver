dnl #
dnl # commit v4.14-rc3-575-g29ad20b22c8f
dnl # drm: Add drm_device->fb_helper pointer
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_FB_HELPER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#ifdef HAVE_DRM_DRMP_H
			#include <drm/drmP.h>
			#endif
			#ifdef HAVE_DRM_DRM_DEVICE_H
                        #include <drm/drm_device.h>
                        #endif
		], [
			struct drm_device *pdd = NULL;
			pdd->fb_helper = NULL;
		], [
			AC_DEFINE(HAVE_DRM_DEVICE_FB_HELPER, 1, [struct drm_device has fb_helper member])
		])
	])
])
