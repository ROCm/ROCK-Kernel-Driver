dnl # commit 1398958cfd8d331342d657d37151791dd7256b40
dnl # Author: Nicholas Kazlauskas <nicholas.kazlauskas@amd.com>
dnl # Date:   Thu Oct 4 11:46:07 2018 -0400
dnl #	drm: Add vrr_enabled property to drm CRTC
dnl # There is no member vrr_enabled in struct dm_crtc_state before drm version(5.0.0)
AC_DEFUN([AC_AMDGPU_VRR_ENABLED_IN_STRUCT_DM_CRTC_STATE],
	[AC_MSG_CHECKING([whether there is vrr_enabled field in dm_crtc_state structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_crtc.h>
	],[
		struct drm_crtc_state base;
		base.vrr_enabled = 1;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VRR_ENABLED_IN_STRUCT_DM_CRTC_STATE, 1, [there is vrr_enabled field in dm_crtc_state structure])
	],[
		AC_MSG_RESULT(no)
	])
])
