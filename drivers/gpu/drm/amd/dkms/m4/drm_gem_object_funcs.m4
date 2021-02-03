dnl #
dnl # commit v4.9-rc8-1739-g6d1b81d8e25d
dnl # drm: add crtc helper drm_crtc_from_index()
dnl # commit v5.9-rc5-1077-gd693def4fd1c
dnl # drm: Remove obsolete GEM and PRIME callbacks from struct drm_driver
dnl #
AC_DEFUN([AC_AMDGPU_DRM_GEM_TTM_VMAP], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_gem_ttm_vmap], [drivers/gpu/drm/drm_gem_ttm_helper.c], [
			AC_DEFINE(HAVE_DRM_GEM_TTM_VMAP, 1, [drm_gem_ttm_vmap() is available])
		],[
			AC_KERNEL_TRY_COMPILE([
				struct vm_area_struct;
				#ifdef HAVE_DRM_DRMP_H
				#include <drm/drmP.h>
				#else
				#include <drm/drm_drv.h>
				#endif
			],[
				struct drm_driver *drv = NULL;
				drv->gem_open_object = NULL;
			],[
				AC_DEFINE(HAVE_STRUCT_DRM_DRV_GEM_OPEN_OBJECT_CALLBACK, 1,
					[drm_gem_open_object is defined in struct drm_drv])
			])
		])
	])
])
