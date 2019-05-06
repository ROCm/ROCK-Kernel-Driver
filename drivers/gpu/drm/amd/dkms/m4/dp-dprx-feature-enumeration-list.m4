dnl # commit d0ce9062912062fd1f6fafd35e89aef1b852511b
dnl # Author: Nagaraju, Vathsala <vathsala.nagaraju@intel.com>
dnl # Date:   Mon Jan 2 17:00:54 2017 +0530
dnl # drm : adds Y-coordinate and Colorimetry Format
dnl #
dnl # DP_DPRX_FEATURE_ENUMERATION_LIST is not defined before drm version(4.11.0)
AC_DEFUN([AC_AMDGPU_DP_DPRX_FEATURE_ENUMERATION_LIST],
	[AC_MSG_CHECKING([whether DP_DPRX_FEATURE_ENUMERATION_LIST is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_dp_helper.h>
	],[
		#if !defined(DP_DPRX_FEATURE_ENUMERATION_LIST)
		#error DP_DPRX_FEATURE_ENUMERATION_LIST not #defined
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DP_DPRX_FEATURE_ENUMERATION_LIST, 1, [whether DP_DPRX_FEATURE_ENUMERATION_LIST is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
