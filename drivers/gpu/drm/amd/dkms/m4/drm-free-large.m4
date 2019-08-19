dnl #
dnl # commit 72e942dd846f98e2d35aad5436d77a878ef05c5e
dnl # Author: Dave Airlie <airlied@redhat.com>
dnl # Date:   Tue Mar 9 06:33:26 2010 +0000
dnl # drm/ttm: use drm calloc large and free large
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FREE_LARGE], [
	AC_MSG_CHECKING([whether drm_free_large() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/kernel.h>
		#ifndef SIZE_MAX
		#define SIZE_MAX (~0UL)
		#endif
		#include <linux/slab.h>
		#include <linux/mm.h>
		#include <drm/drm_mem_util.h>
	],[
		drm_free_large(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FREE_LARGE, 1, [drm_free_large() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
