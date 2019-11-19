dnl #
dnl # commit 7c247c067590b102ed2bd119bcadf4986ca10e94
dnl # Author: Manasi Navare <manasi.d.navare@intel.com>
dnl # Date:   Tue Nov 27 13:41:04 2018 -0800
dnl #
dnl # drm/dsc: Define Display Stream Compression PPS infoframe
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DSC_H],
	[AC_MSG_CHECKING([whether drm/drm_dsc.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_dsc.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DSC_H, 1, [drm/drm_dsc.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
