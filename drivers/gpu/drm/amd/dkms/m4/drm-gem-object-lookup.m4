dnl #
dnl # commit a8ad0bd84f986072314595d05444719fdf29e412
dnl # drm: Remove unused drm_device from drm_gem_object_lookup()
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_DRM_GEM_OBJECT_LOOKUP],
	[AC_MSG_CHECKING([whether drm_gem_object_lookup() wants 2 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drmP.h>
		#include <drm/drm_gem.h>
	], [
		drm_gem_object_lookup(NULL, 0);
	], [drm_gem_object_lookup], [drivers/gpu/drm/drm_gem.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_DRM_GEM_OBJECT_LOOKUP, 1, [drm_gem_object_lookup() wants 2 args])
	], [
		AC_MSG_RESULT(no)
	])
])
