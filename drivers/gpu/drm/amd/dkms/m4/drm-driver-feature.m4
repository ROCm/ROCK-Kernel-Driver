AC_DEFUN([AC_AMDGPU_DRM_DRIVER_FEATURE], [
	dnl #
	dnl # commit 88a48e297b3a3bac6022c03babfb038f1a886cea
	dnl # drm: add atomic properties
	dnl # commit 0e2a933b02c972919f7478364177eb76cd4ae00d
	dnl # drm: Switch DRIVER_ flags to an enum
	dnl #
	AC_MSG_CHECKING([whether drm_driver_feature DRIVER_ATOMIC is available])
        AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_drv.h>
        ], [
		int _ = DRIVER_ATOMIC;
        ], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DRV_DRIVER_ATOMIC, 1, [
			drm_driver_feature DRIVER_ATOMIC is available
		])
        ], [
		AC_MSG_RESULT(no)
        ])

	dnl #
	dnl # commit: 060cebb20cdbcd3185d593e7194fa7a738201817
	dnl # drm: introduce a capability flag for syncobj timeline support
	dnl #
	AC_MSG_CHECKING([whether drm_driver_feature DRIVER_SYNCOBJ_TIMELINE is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_drv.h>
	],[
		int _ = DRIVER_SYNCOBJ_TIMELINE;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DRV_DRIVER_SYNCOBJ_TIMELINE, 1, [
			drm_driver_feature DRIVER_SYNCOBJ_TIMELINE is available
		])
	],[
		AC_MSG_RESULT(no)
	])

	dnl #
	dnl # commit: 1ff494813bafa127ecba1160262ba39b2fdde7ba
	dnl # drm/irq: Ditch DRIVER_IRQ_SHARED
	dnl #
	AC_MSG_CHECKING([whether drm_driver_feature DRIVER_IRQ_SHARED is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_drv.h>
	],[
		int _ = DRIVER_IRQ_SHARED;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DRV_DRIVER_IRQ_SHARED, 1, [
			drm_driver_feature DRIVER_IRQ_SHARED is available
		])
	],[
		AC_MSG_RESULT(no)
	])
])

