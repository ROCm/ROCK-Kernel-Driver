dnl #
dnl # commit v4.12-rc1-158-g3ed4351a83ca
dnl # drm: Extract drm_vblank.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_VBLANK_H],
	[AC_MSG_CHECKING([whether drm/drm_vblank.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_vblank.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_VBLANK_H, 1, [drm/drm_vblank.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
