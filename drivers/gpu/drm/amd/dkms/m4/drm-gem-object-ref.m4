dnl #
dnl # commit 743e0f079a38182cb22e663b34e6a45ad3274b9e
dnl # drm/nouveau: Replace drm_gem_object_unreference_unlocked with put function
dnl #
AC_DEFUN([AC_AMDGPU_DRM_GEM_OBJECT_PUT_UNLOCKED],
	[AC_MSG_CHECKING([whether drm_gem_object_put_unlocked() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_gem.h>
	],[
		drm_gem_object_put_unlocked(NULL);
	], [drm_gem_object_put_unlocked], [drivers/gpu/drm/drm_gem.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_GEM_OBJECT_PUT_UNLOCKED, 1, [drm_gem_object_put_unlocked() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
