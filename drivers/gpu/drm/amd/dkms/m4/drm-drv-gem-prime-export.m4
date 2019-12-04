dnl #
dnl # commit e4fa8457b2197118538a1400b75c898f9faaf164
dnl # drm/prime: Align gem_prime_export with obj_funcs.export
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DRV_GEM_PRIME_EXPORT],
	[AC_MSG_CHECKING([whether drm_driver->gem_prime_export with p,i arg is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_drv.h>
	],[
		struct drm_driver *test = NULL;
		test->gem_prime_export(NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DRV_GEM_PRIME_EXPORT_PI, 1, [ drm_driver->gem_prime_export with p,i arg is available])
	],[
		AC_MSG_RESULT(no)
	])
])
