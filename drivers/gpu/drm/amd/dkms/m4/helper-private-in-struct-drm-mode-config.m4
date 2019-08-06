dnl #
dnl # commit 28575f165d36051310d7ea2350e2011f8095b6fb
dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
dnl # Date:   Mon Nov 14 12:58:23 2016 +0100
dnl # drm: Extract drm_mode_config.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_HELPER_PRIVATE_IN_STRUCT_DRM_MODE_CONFIG],
	[AC_MSG_CHECKING([whether there exist helper_private in struct drm_mode_config])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_mode_config.h>
	], [
		struct drm_mode_config mode;
		mode.helper_private = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_HELPER_PRIVATE_IN_STRUCT_DRM_MODE_CONFIG, 1, [there exist alpha in struct drm_plane_state])
	], [
		dnl #
		dnl # commit 9f2a7950e77abf00a2a87f3b4cbefa36e9b6009b
		dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
		dnl # Date:   Wed Jun 8 14:19:02 2016 +0200
		dnl # drm/atomic-helper: nonblocking commit support
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether there exist helper_private in struct drm_mode_config])
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_mode_config mode;
			mode.helper_private = NULL;
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_HELPER_PRIVATE_IN_STRUCT_DRM_MODE_CONFIG, 1, [there exist alpha in struct drm_plane_state])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
