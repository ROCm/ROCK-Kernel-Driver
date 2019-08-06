dnl #
dnl # commit 6cbe5c466d73360506a24d98a2e71e47ae02e3ef
dnl # Author: Andrey Grodzovsky <Andrey.Grodzovsky@amd.com>
dnl # Date:   Thu Feb 2 16:56:29 2017 -0500
dnl # drm/atomic: Save flip flags in drm_crtc_state
dnl #
AC_DEFUN([AC_AMDGPU_PAGEFLIP_FLAGS_IN_STRUCTURE_DRM_CRTC_STATE],
	[AC_MSG_CHECKING([for pageflip_flags field within drm_crtc_state structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_crtc.h>
	], [
		struct drm_crtc_state state;
		state.pageflip_flags = 1;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PAGEFLIP_FLAGS_IN_STRUCTURE_DRM_CRTC_STATE, 1, [drm_crtc_state structure contains pageflip_flags field])
	], [
		AC_MSG_RESULT(no)
	])
])
