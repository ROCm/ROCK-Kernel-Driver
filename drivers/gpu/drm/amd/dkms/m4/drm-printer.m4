dnl #
dnl # commit d8187177b0b195368699ba12b5fa8cd5fdc39b79
dnl # drm: add helper for printing to log or seq_file
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PRINTER],
	[AC_MSG_CHECKING([whether drm_printer is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_print.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_PRINTER, 1, [drm_printer is available])
	], [
		AC_MSG_RESULT(no)
	])
])
