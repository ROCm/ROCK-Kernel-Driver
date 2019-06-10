dnl #
dnl # commit 2c6d1fffa1d9b0a5b5ac1a23be9ad64abe60910d
dnl # Author: Hans Verkuil <hans.verkuil@cisco.com>
dnl # Date:   Wed Jul 11 15:29:07 2018 +0200
dnl # drm: add support for DisplayPort CEC-Tunneling-over-AUX
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_CEC_CORRELATION_FUNCTIONS],
	[AC_MSG_CHECKING([whether drm_dp_cec* correlation functions are available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include<drm/drm_dp_helper.h>
	], [
		drm_dp_cec_irq(NULL);
		drm_dp_cec_register_connector(NULL, NULL, NULL);
		drm_dp_cec_unregister_connector(NULL);
		drm_dp_cec_set_edid(NULL, NULL);
		drm_dp_cec_unset_edid(NULL);
	], [drm_dp_cec_irq drm_dp_cec_register_connector drm_dp_cec_unregister_connector drm_dp_cec_set_edid drm_dp_cec_unset_edid], [drivers/gpu/drm/drm_dp_cec.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS, 1, [drm_dp_cec* correlation functions are available])
	], [
		AC_MSG_RESULT(no)
	])
])
