dnl #
dnl # commit c8e0f93a381d
dnl # drm/i915: Replace a calloc followed by copying data over it with malloc.
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MALLOC_AB], [
	AC_MSG_CHECKING([whether drm_malloc_ab() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
	],[
		drm_malloc_ab(0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_MALLOC_AB, 1, [drm_malloc_ab() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
