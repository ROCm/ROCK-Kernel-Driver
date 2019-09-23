AC_DEFUN([AC_AMDGPU_DRM_CACHE], [
dnl #
dnl # commit 913b2cb727b7a47ccf8842d54c89f1b873c6deed
dnl # drm: change func to better detect wether swiotlb is needed
dnl #
	AC_MSG_CHECKING([whether drm_need_swiotlb() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_cache.h>
	], [
		drm_need_swiotlb(0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_NEED_SWIOTLB, 1, [
			drm_need_swiotlb() is available
		])

	], [
		AC_MSG_RESULT(no)
	])
])
