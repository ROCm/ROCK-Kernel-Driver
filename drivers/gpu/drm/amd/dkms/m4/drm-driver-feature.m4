dnl #
dnl # commit 0e2a933b02c972919f7478364177eb76cd4ae00d
dnl # drm: Switch DRIVER_ flags to an enum
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DRIVER_FEATURE], [
	dnl #
	dnl # commit: v5.1-rc5-1467-g060cebb20cdb
	dnl # drm: introduce a capability flag for syncobj timeline support
	dnl #
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
			],[
				int _ = DRIVER_SYNCOBJ_TIMELINE;
			],[
				AC_DEFINE(HAVE_DRM_DRV_DRIVER_SYNCOBJ_TIMELINE, 1, [
					drm_driver_feature DRIVER_SYNCOBJ_TIMELINE is available])
			])
		], [
			AC_DEFINE(HAVE_DRM_DRV_DRIVER_SYNCOBJ_TIMELINE, 1, [
				drm_driver_feature DRIVER_SYNCOBJ_TIMELINE is available])
		])
	])

	dnl #
	dnl # commit: v5.0-rc1-390-g1ff494813baf
	dnl # drm/irq: Ditch DRIVER_IRQ_SHARED
	dnl #
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
			],[
				int _ = DRIVER_IRQ_SHARED;
			],[
				AC_DEFINE(HAVE_DRM_DRV_DRIVER_IRQ_SHARED, 1, [
					drm_driver_feature DRIVER_IRQ_SHARED is available])
			])
		])
	])

	dnl #
	dnl # commit: v5.2-rc5-867-g0424fdaf883a
	dnl # drm/prime: Actually remove DRIVER_PRIME everywhere
	dnl #
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
			],[
				int _ = DRIVER_PRIME;
			],[
				AC_DEFINE(HAVE_DRM_DRV_DRIVER_PRIME, 1, [
					drm_driver_feature DRIVER_PRIME is available])
			])
		])
	])
])

