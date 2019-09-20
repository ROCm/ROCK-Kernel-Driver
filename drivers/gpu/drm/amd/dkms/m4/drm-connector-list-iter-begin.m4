dnl #
dnl # commit 613051dac40da1751ab269572766d3348d45a197
dnl # drm: locking&new iterators for connector_list
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_LIST_ITER_BEGIN],
	[AC_MSG_CHECKING([whether drm_connector_list_iter_begin is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_connector.h>
	],[
		drm_connector_list_iter_begin(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_CONNECTOR_LIST_ITER_BEGIN, 1, [drm_connector_list_iter_begin() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
