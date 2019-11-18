dnl #
dnl # commit 06d7cecdb61115de3b573682a6615b05ae993932
dnl # drm/dsc: Add native 420 and 422 support to compute_rc_params
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DSC_CONFIG_RENAME_ENABLE422],
	[AC_MSG_CHECKING([whether drm_dsc_config->simple422 is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_dsc.h>
	], [
		struct drm_dsc_config *p = NULL;
		p->simple_422 = 0;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DSC_CONFIG_RENAME_ENABLE422, 1, [drm_dsc_config->simple_422 is available])
	],[
		AC_MSG_RESULT(no)
	])
])
