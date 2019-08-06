dnl #
dnl # commit 5488dc16fde74595a40c5d20ae52d978313f0b4e
dnl # drm: introduce pipe color correction properties
dnl #
AC_DEFUN([AC_AMDGPU_COLOR_MGMT_CHANGED_IN_STRUCT_DRM_CRTC_STATE],
	[AC_MSG_CHECKING([for color_mgmt_changed field within drm_crtc_state structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_crtc.h>
	], [
		struct drm_crtc_state state;
		state.color_mgmt_changed = 0;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_COLOR_MGMT_CHANGED_IN_STRUCT_DRM_CRTC_STATE, 1, [ddrm_crtc_state structure contains color_mgmt_changed field])
	], [
		AC_MSG_RESULT(no)
	])
])
