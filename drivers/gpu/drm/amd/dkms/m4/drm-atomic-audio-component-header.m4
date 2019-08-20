dnl # commit 72fdb40c1a4b48f5fa6f6083ea7419b94639ed57
dnl # drm: extract drm_atomic_uapi.c
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_UAPI_HEADER],
	[AC_MSG_CHECKING([whether drm_atomic_uapi.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_atomic_uapi.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_UAPI_HEADER, 1, [include/drm/drm_atomic_uapi.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
