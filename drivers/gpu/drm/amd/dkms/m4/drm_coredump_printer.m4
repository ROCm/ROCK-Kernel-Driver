dnl #
dnl # commit cfc57a18a3c5dc95d06db80bddd30015162c57d2
dnl # drm: drm_printer: Add printer for devcoredump
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_DRM_COREDUMP_PRINTER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_print.h>
		], [
			drm_coredump_printer(NULL);
		], [
			AC_DEFINE(HAVE_DRM_COREDUMP_PRINTER, 1,
				[drm_coredump_printer function is available])
		])
	])
])
