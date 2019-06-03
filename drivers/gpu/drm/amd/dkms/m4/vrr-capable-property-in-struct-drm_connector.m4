dnl #
dnl # commit ba1b0f6c73d4ea1390f0d5381f715ffa20c75f09
dnl # Author: Nicholas Kazlauskas <nicholas.kazlauskas@amd.com>
dnl # Date:   Tue Sep 18 09:55:20 2018 -0400
dnl # drm: Add vrr_capable property to the drm connector
dnl #
AC_DEFUN([AC_AMDGPU_VRR_CAPABLE_PROPERTY_IN_STRUCT_DRM_CONNECTOR],
	[AC_MSG_CHECKING([for vrr_capable_property field within drm_connector structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_connector.h>
	], [
		struct drm_connector connector;
		connector.vrr_capable_property = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VRR_CAPABLE_PROPERTY_IN_STRUCT_DRM_CONNECTOR, 1, [drm_connector structure contains vrr_capable_property field])
	], [
		AC_MSG_RESULT(no)
	])
])
