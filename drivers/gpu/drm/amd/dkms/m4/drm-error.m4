dnl #
dnl # commit 02c9656b2f0d6997939933d8573c2ffb587427e6
dnl # drm: Move debug macros out of drmP.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ERROR],
	[AC_MSG_CHECKING([whether DRM_ERROR is defined in drmP.h])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
	],[
		DRM_ERROR("TEST DRM_ERROR");
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ERROR_IN_DRMP_H, 1, [DRM_ERRROR is defined in drmP.h])
	],[
		AC_MSG_RESULT(no)
	])
])
