dnl #
dnl # commit d8187177b0b195368699ba12b5fa8cd5fdc39b79
dnl # drm: add helper for printing to log or seq_file
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PRINTF],
	[AC_MSG_CHECKING([whether drm_printf() function is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_print.h>
	],[
		drm_printf(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_PRINTF, 1, [drm_printf() function is available])
	],[
		AC_MSG_RESULT(no)
	])
])
