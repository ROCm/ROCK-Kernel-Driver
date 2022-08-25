dnl #
dnl # commit 96d4f267e40f9509e8a66e2b39e8b95655617693
dnl # Author: Linus Torvalds <torvalds@linux-foundation.org>
dnl # Date:   Thu Jan 3 18:57:57 2019 -0800
dnl # Remove 'type' argument from access_ok() function
dnl #
AC_DEFUN([AC_AMDGPU_DRM_GEM_PLANE_HELPER_PREPARE_FB], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_gem_atomic_helper.h>
		],[
			drm_gem_plane_helper_prepare_fb(NULL, NULL);
		],[
			AC_DEFINE(HAVE_DRM_GEM_PLANE_HELPER_PREPARE_FB, 1,
				[drm_gem_plane_helper_prepare_fb() is available])
		])
	])
])
