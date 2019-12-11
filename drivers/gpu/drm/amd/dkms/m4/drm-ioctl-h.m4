dnl #
dnl # commit v4.11-rc3-927-g7cfdf711ffb0
dnl # drm: Extract drm_ioctl.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_IOCTL_H],
	[AC_MSG_CHECKING([whether drm/drm_ioctl.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_ioctl.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_IOCTL_H, 1, [drm/drm_ioctl.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
