dnl #
dnl # commit 9f0ba539d13aebacb05dda542df7ef80684b2c70
dnl # drm/gem: support BO freeing without dev->struct_mutex
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DRIVER_GEM_FREE_OBJECT_UNLOCKED],
	[AC_MSG_CHECKING([whether drm_driver->gem_free_object_unlock() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
	],[
		struct drm_driver *ddrv = NULL;
		ddrv->gem_free_object_unlocked = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GEM_FREE_OBJECT_UNLOCKED_IN_DRM_DRIVER, 1, [drm_driver->gem_free_object_unlocked() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
