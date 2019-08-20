dnl #
dnl # commit 4e64e5539d152e202ad6eea2b6f65f3ab58d9428
dnl # Author: Chris Wilson <chris@chris-wilson.co.uk>
dnl # Date:   Thu Feb 2 21:04:38 2017 +0000
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MM_INSERT_MODE],
	[AC_MSG_CHECKING([whether drm_mm_insert_mode is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
	],[
		enum drm_mm_insert_mode mode = DRM_MM_INSERT_BEST;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_MM_INSERT_MODE, 1, [whether drm_mm_insert_mode is available])
	],[
		AC_MSG_RESULT(no)
	])
])
