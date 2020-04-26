dnl # commit 490d3d1b91201fd3d3d01d64e11df4eac1d92bd4
dnl # drm: Store the plane's index
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_INDEX], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc *crtc = NULL;

			crtc->index = 0;
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_INDEX, 1,
				[struct drm_crtc has index])
		])
	])
])
