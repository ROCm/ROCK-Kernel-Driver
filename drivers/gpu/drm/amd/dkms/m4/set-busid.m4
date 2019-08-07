dnl # commit 5c484cee7ef9c4fd29fa0ba09640d55960977145
dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
dnl # drm: Remove drm_driver->set_busid hook
dnl #
AC_DEFUN([AC_AMDGPU_VERIFY_SET_BUSID_IN_STRUCT_DRM_DRIVER],
	[AC_MSG_CHECKING([drm_driver have set_busid])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
	], [
		struct drm_driver *bar = NULL;
		bar->set_busid(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_BUSID_IN_STRUCT_DRM_DRIVER, 1, [drm_driver have set_busid])
	], [
		AC_MSG_RESULT(no)
	])
])
