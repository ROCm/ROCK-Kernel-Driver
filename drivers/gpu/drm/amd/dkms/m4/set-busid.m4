dnl #
dnl # commit v3.17-rc2-232-g915b4d11b8b9
dnl # drm: add driver->set_busid() callback
dnl #
dnl # commit v4.12-rc1-201-g5c484cee7ef9
dnl # drm: Remove drm_driver->set_busid hook
dnl #
AC_DEFUN([AC_AMDGPU_VERIFY_SET_BUSID_IN_STRUCT_DRM_DRIVER],
	[AC_MSG_CHECKING([drm_driver have set_busid])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
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
	], [
		AC_MSG_RESULT(no)
	])
])
