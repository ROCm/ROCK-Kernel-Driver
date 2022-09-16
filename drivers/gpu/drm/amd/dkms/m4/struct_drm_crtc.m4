dnl #
dnl # commit v4.10-rc1~154-9edbf1fa6
dnl # drm: Add API for capturing frame CRCs
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_DEBUGFS_ENTRY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc *test = NULL;
			test->debugfs_entry = NULL;
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_DEBUGFS_ENTRY, 1, [
				drm_crtc->debugfs_entry is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC], [
	AC_AMDGPU_STRUCT_DRM_CRTC_DEBUGFS_ENTRY
])
