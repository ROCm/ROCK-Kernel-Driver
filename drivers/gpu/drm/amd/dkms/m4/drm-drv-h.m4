dnl #
dnl # drm: Extract drm_drv.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DRV_H],
	[AC_MSG_CHECKING([whether drm/drm_drv.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_drv.h],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DRV_H, 1, [drm/drm_drv.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
