dnl #
dnl # commit b5c3714fe8789745521d8351d75049b9c6a0d26b
dnl # drm/mm: Convert to drm_printer
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MM_PRINT], [
	AC_MSG_CHECKING([whether drm_mm_print() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
		#include <drm/drm_mm.h>
	], [
		drm_mm_print(NULL, NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_MM_PRINT, 1, [drm_mm_print() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
