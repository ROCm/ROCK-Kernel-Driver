dnl #
dnl # commit 2570fe2586254ff174c2ba5a20dabbde707dbb9b
dnl # drm: add helper functions for YCBCR420 handling
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MODE_IS_420_ONLY],
	[AC_MSG_CHECKING([whether drm_mode_is_420_only() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_modes.h>
	], [
		drm_mode_is_420_only(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_MODE_IS_420_ONLY, 1, [drm_mode_is_420_only() is available])

	],[
		AC_MSG_RESULT(no)
	])
])
