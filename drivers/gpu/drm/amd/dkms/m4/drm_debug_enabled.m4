dnl #
dnl # commit v5.3-rc1-708-gf0a8f533adc2
dnl # drm/print: add drm_debug_enabled()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEBUG_ENABLED], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_print.h>
		],[
			drm_debug_enabled(0);
		],[
			AC_DEFINE(HAVE_DRM_DEBUG_ENABLED,
				1,
				[drm_debug_enabled() is available])
		])
	])
])

dnl #
dnl # commit v6.8-rc3-242-g9fd6f61a297e
dnl # drm/print: add drm_dbg_printer() for drm device specific printer
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DBG_PRINTER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_print.h>
		],[
			drm_dbg_printer(NULL, 0, NULL);
		],[
			AC_DEFINE(HAVE_DRM_DBG_PRINTER,
				1,
				[drm_dbg_printer() is available])
		])
	])
])

dnl #
dnl # commit v5.4-rc4-974-g876905b8fe59
dnl # drm/print: convert debug category macros into an enum
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEBUG_CATEGORY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_print.h>
		],[
			enum drm_debug_category category;
			category = DRM_UT_CORE;
		],[
			AC_DEFINE(HAVE_DRM_DEBUG_CATEGORY,
				1,
				[enum drm_debug_category is available])
		])
	])
])
