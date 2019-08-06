dnl #
dnl # commit 47e22ff1a9e0c144611bd063b3e6135f9a269503
dnl # Author: Radhakrishna Sripada <radhakrishna.sripada@intel.com>
dnl # Date:   Fri Oct 12 11:42:32 2018 -0700
dnl # drm: Add connector property to limit max bpc
dnl #
AC_DEFUN([AC_AMDGPU_MAX_BPC_AND_MAX_REQUESTED_BPC_IN_STRUCT_DRM_CONNECTOR_STATE],
	[AC_MSG_CHECKING([for max_bpc and max_requested_bpc field within drm_connector_state structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_connector.h>
	], [
		struct drm_connector_state state;
		struct drm_connector con;
		state.max_bpc = 1;
		state.max_requested_bpc = 1;
		con.max_bpc_property = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MAX_BPC_AND_MAX_REQUESTED_BPC_IN_STRUCT_DRM_CONNECTOR_STATE, 1, [drm_connector_state structure contains max_bpc and max_requested_bpc field])
	], [
		AC_MSG_RESULT(no)
	])
])
