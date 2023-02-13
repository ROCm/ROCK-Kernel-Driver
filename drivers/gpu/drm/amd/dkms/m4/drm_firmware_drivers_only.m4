dnl #
dnl # v5.16-rc1-268-g6a2d2ddf2c34
dnl # drm: Move nomodeset kernel parameter to the DRM subsystem
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FIRMWARE_DRIVERS_ONLY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_drv.h>
		], [
			drm_firmware_drivers_only();
		], [
			AC_DEFINE(HAVE_DRM_FIRMWARE_DRIVERS_ONLY, 1,
				[drm_firmware_drivers_only() is available])
		])
	])
])
