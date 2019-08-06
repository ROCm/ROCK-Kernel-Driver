dnl #
dnl # commit 3d387d923c18afbacef8f14ccaa2ace2a297df74
dnl # drm/printer: add debug printer
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEBUG_PRINTER],
	[AC_MSG_CHECKING([whether drm_debug_printer() function is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_print.h>
	],[
		struct drm_printer printer;
		printer = drm_debug_printer(NULL);
	], [__drm_printfn_debug], [drivers/gpu/drm/drm_print.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DEBUG_PRINTER, 1, [drm_debug_printer() function is available])
	],[
		AC_MSG_RESULT(no)
	])
])
