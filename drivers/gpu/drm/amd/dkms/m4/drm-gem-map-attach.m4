dnl #
dnl # commit 54037055a21bd956ba28b2c6ca197222a3ac19cd
dnl # Author: tianci yin <tianci.yin@amd.com>
dnl # Date:   Fri Jan 18 15:33:13 2019 +0800
dnl # drm/amdkcl: [4.19] kcl for .attach of struct dma_buf_ops
dnl #
dnl # commit a19741e5e5a9f1f02f8e3c037bde7d73d4bfae9c
dnl # Author: Christian König <christian.koenig@amd.com>
dnl # Date:   Mon May 28 11:47:52 2018 +0200
dnl # dma_buf: remove device parameter from attach callback v2
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_DRM_GEM_MAP_ATTACH],
	[AC_MSG_CHECKING([whether drm_gem_map_attach() wants 2 arguments])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_prime.h>
	],[
		drm_gem_map_attach(NULL, NULL);
	], [drm_gem_map_attach], [drivers/gpu/drm/drm_prime.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_DRM_GEM_MAP_ATTACH, 1, [drm_gem_map_attach() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
