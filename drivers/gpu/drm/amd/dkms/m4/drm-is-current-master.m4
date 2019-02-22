dnl #
dnl # commit b3ac9f2591061e4470834028f563ef1fd86098cf
dnl # drm: Extract drm_is_current_master
dnl # Just rolling out a bit of abstraction to be able to clean
dnl # up the master logic in the next step.
AC_DEFUN([AC_AMDGPU_DRM_IS_CURRENT_MASTER],
	[AC_MSG_CHECKING([whether drm_is_current_master() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/wait.h>
		#include <linux/kref.h>
		#include <linux/idr.h>
		#include <drm/drm_auth.h>
	],[
		drm_is_current_master(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_IS_CURRENT_MASTER, 1, [drm_is_current_master() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
