dnl #
dnl # commit v5.0-1332-g44f8a1396e83
dnl # drm/syncobj: add new drm_syncobj_add_point interface v4
dnl #
AC_DEFUN([AC_AMDGPU_DRM_SYNCOBJ_ADD_POINT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_syncobj.h>
		],[
			drm_syncobj_add_point(NULL, NULL, NULL, 0);
		],[drm_syncobj_add_point], [drivers/gpu/drm/drm_syncobj.c], [
			AC_DEFINE(HAVE_DRM_SYNCOBJ_ADD_POINT, 1,
				[drm_syncobj_add_point() is available])
		])
	])
])