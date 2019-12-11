dnl #
dnl # commit v4.11-rc3-918-g4834442d70be
dnl # drm: Extract drm_debugfs.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEBUGFS_H],
	[AC_MSG_CHECKING([whether drm/drm_debugfs.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_debugfs.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DEBUGFS_H, 1, [drm/drm_debugfs.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
