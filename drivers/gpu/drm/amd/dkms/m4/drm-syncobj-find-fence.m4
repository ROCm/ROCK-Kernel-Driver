dnl #
dnl # commit 649fdce23cdf516e69aa8c18f4b44c62127f0e83
dnl # drm: add flags to drm_syncobj_find_fence
dnl #
AC_DEFUN([AC_AMDGPU_DRM_SYNCOBJ_FIND_FENCE], [
	AC_MSG_CHECKING([whether drm_syncobj_find_fence() wants 5 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		struct drm_file;
		#include <drm/drm_syncobj.h>
	], [
		drm_syncobj_find_fence(NULL, 0, 0, 0, NULL);
	], [drm_syncobj_find_fence], [drivers/gpu/drm/drm_syncobj.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_5ARGS_DRM_SYNCOBJ_FIND_FENCE, 1, whether drm_syncobj_find_fence() wants 5 args)
		AC_DEFINE(HAVE_DRM_SYNCOBJ_FIND_FENCE, 1, drm_syncobj_find_fence() is available)
	], [
dnl #
dnl # commit 0a6730ea27b68c7ac4171c29a816c29d26a9637a
dnl # drm: expand drm_syncobj_find_fence to support timeline point v2
dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether drm_syncobj_find_fence() wants 4 args])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			struct drm_file;
			#include <drm/drm_syncobj.h>
		], [
			drm_syncobj_find_fence(NULL, 0, 0, NULL);
		], [drm_syncobj_find_fence], [drivers/gpu/drm/drm_syncobj.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_4ARGS_DRM_SYNCOBJ_FIND_FENCE, 1, whether drm_syncobj_find_fence() wants 4 args)
			AC_DEFINE(HAVE_DRM_SYNCOBJ_FIND_FENCE, 1, drm_syncobj_find_fence() is available)
		], [
dnl #
dnl # commit afaf59237843bf89823c33143beca6b262dff0ca
dnl # drm/syncobj: Rename fence_get to find_fence
dnl #
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether drm_syncobj_find_fence() wants 3 args])
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				struct drm_file;
				#include <drm/drm_syncobj.h>
			], [
				drm_syncobj_find_fence(NULL, 0, NULL);
			], [drm_syncobj_find_fence], [drivers/gpu/drm/drm_syncobj.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_3ARGS_DRM_SYNCOBJ_FIND_FENCE, 1, whether drm_syncobj_find_fence() wants 3 args)
				AC_DEFINE(HAVE_DRM_SYNCOBJ_FIND_FENCE, 1, drm_syncobj_find_fence() is available)
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])
