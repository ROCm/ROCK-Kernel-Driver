dnl #
dnl # commit afaf59237843bf89823c33143beca6b262dff0ca
dnl # drm/syncobj: Rename fence_get to find_fence
dnl #
AC_DEFUN([AC_AMDGPU_DRM_SYNCOBJ_FENCE_GET],
	[AC_MSG_CHECKING([whether drm_syncobj_fence_get() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		struct drm_file;
		#include <drm/drm_syncobj.h>
	], [
		drm_syncobj_fence_get(NULL, 0, NULL);
	], [drm_syncobj_fence_get], [drivers/gpu/drm/drm_syncobj.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_SYNCOBJ_FENCE_GET, 1, [drm_syncobj_fence_get() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
