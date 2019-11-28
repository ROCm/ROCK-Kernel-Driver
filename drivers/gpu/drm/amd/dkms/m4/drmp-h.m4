dnl #
dnl # commit 4e98f871bcffa322850c73d22c66bbd7af2a0374
dnl # drm: delete drmP.h + drm_os_linux.h
dnl #
AC_DEFUN([AC_AMDGPU_DRMP_H],
	[AC_MSG_CHECKING([whether drmP.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRMP_H, 1, [include/drm/drmP.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
