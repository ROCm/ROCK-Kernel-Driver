dnl #
dnl # commit d78aa650670d2257099469c344d4d147a43652d9
dnl # drm: Add drm/drm_util.h header file
dnl #
dnl # commit e9eafcb589213395232084a2378e2e90f67feb29
dnl # drm: move drm_can_sleep() to drm_util.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_UTIL_H],
	[AC_MSG_CHECKING([whether drm/drm_util.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm_util.h],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_UTIL_H, 1, [drm/drm_util.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
