dnl #
dnl # commit e4672e55d6f3428ae9f27542e05c891f2af71051
dnl # drm: Extract drm_device.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_H],
	[AC_MSG_CHECKING([whether drm/drm_device.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_device.h],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DEVICE_H, 1, [drm/drm_device.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
