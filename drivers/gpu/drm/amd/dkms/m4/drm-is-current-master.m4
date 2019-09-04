dnl #
dnl # commit v4.7-rc2-612-g3b96a0b1407e
dnl # drm: document drm_auth.c
dnl #
AC_DEFUN([AC_AMDGPU_DRM_IS_CURRENT_MASTER],
	[AC_MSG_CHECKING([whether drm/drm_auth.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_auth.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_AUTH_H, 1, [drm/drm_auth.h is available])

		AC_MSG_CHECKING([whether drm_is_current_master() is available])
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drmP.h>
			#include <drm/drm_auth.h>
		],[
			drm_is_current_master(NULL);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_DRM_IS_CURRENT_MASTER, 1, [drm_is_current_master() is available])
		],[
			AC_MSG_RESULT(no)
		])
	], [
		AC_MSG_RESULT(no)
dnl #
dnl # commit v4.7-rc2-610-gb3ac9f259106
dnl # drm: Extract drm_is_current_master
dnl #
		AC_MSG_CHECKING([whether drm_is_current_master() is available])
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drmP.h>
		],[
			drm_is_current_master(NULL);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_DRM_IS_CURRENT_MASTER, 1, [drm_is_current_master() is available])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
