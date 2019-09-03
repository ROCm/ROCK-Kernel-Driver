dnl #
dnl # commit a8f8b1d9b8701465f1309d551fba2ebda6760f49
dnl # drm: Extract drm_file.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FILE_H],
	[AC_MSG_CHECKING([whether drm/drm_file.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_file.h],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FILE_H, 1, [drm/drm_file.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
