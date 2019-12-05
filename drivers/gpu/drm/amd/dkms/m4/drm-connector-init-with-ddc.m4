dnl #
dnl # commit v5.3-rc1-330-g100163df4203
dnl # drm: Add drm_connector_init() variant with ddc
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_INIT_WITH_DDC],
	[AC_MSG_CHECKING([whether drm_connector_init_with_ddc() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_connector.h>
	],[
		drm_connector_init_with_ddc(NULL, NULL, NULL, 0, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_CONNECTOR_INIT_WITH_DDC, 1, [drm_connector_init_with_ddc() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
