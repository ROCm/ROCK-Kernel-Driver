dnl #
dnl # commit d5cc15a0c66e207d5a7f1b92f32899cc8f380468
dnl # Author: Mahesh Kumar <mahesh1.kumar@intel.com>
dnl # Date:   Fri Jul 13 19:29:33 2018 +0530
dnl # drm: crc: Introduce verify_crc_source callback
dnl #
AC_DEFUN([AC_AMDGPU_VERIFY_CRC_SOURCE_IN_STRUCT_DRM_CRTC_FUNCS],
	[AC_MSG_CHECKING([for verify_crc_source field within drm_crtc_funcs structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_crtc.h>
	], [
		struct drm_crtc_funcs cf;
		cf.verify_crc_source = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VERIFY_CRC_SOURCE_IN_STRUCT_DRM_CRTC_FUNCS, 1, [drm_crtc_funcs structure contains verify_crc_source field])
	], [
		AC_MSG_RESULT(no)
	])
])
