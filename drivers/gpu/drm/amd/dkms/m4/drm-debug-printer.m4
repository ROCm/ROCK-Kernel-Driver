dnl #
dnl # commit 3d387d923c18afbacef8f14ccaa2ace2a297df74
dnl # drm/printer: add debug printer
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEBUG_PRINTER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_print.h>
		], [
			drm_debug_printer(NULL);
		], [
			AC_DEFINE(HAVE_DRM_DEBUG_PRINTER, 1,
				[drm_debug_printer() function is available])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
