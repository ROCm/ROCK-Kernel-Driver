dnl #
dnl # commit f3804203306e098dae9ca51540fcd5eb700d7f40
dnl # array_index_nospec: Sanitize speculative array de-references
dnl #
AC_DEFUN([AC_AMDGPU_DRM_AUTH_H],
	[AC_MSG_CHECKING([whether drm/drm_auth.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_auth.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_AUTH_H, 1, [drm/drm_auth.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
