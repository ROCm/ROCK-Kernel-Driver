AC_DEFUN([AC_AMDGPU_DRM_DRIVER_GEM_OPEN_OBJECT], [
	AC_KERNEL_DO_BACKGROUND([
	dnl #
	dnl # commit v5.10-rc2-329-g49a3f51dfeee
	dnl # drm/gem: Use struct dma_buf_map in GEM vmap ops and convert GEM backends
	dnl #
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_gem.h>
		], [
			struct drm_gem_object_funcs *funcs = NULL;
			funcs->vmap(NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_GEM_OBJECT_FUNCS_VMAP_2ARGS, 1, [drm_gem_object_funcs->vmap() has 2 args])
		],[
		dnl # commit v5.9-rc5-1077-gd693def4fd1c
		dnl # drm: Remove obsolete GEM and PRIME callbacks from struct drm_driver
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				#include <drm/drm_drv.h>
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


dnl #
dnl # commit v5.17-rc2-157-g7938f4218168
dnl # dma-buf-map: Rename to iosys-map
dnl #
AC_DEFUN([AC_AMDGPU_DRM_GEM_OBJECT_FUNCS_VMAP_HAS_IOSYS_MAP_ARG], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_gem.h>
                ], [
			struct drm_gem_object_funcs *funcs = NULL;
			struct iosys_map *map = NULL;
			funcs->vmap(NULL, map);
                ], [
                        AC_DEFINE(HAVE_DRM_GEM_OBJECT_FUNCS_VMAP_HAS_IOSYS_MAP_ARG, 1,
                                [drm_gem_object_funcs.vmap hsa iosys_map arg])
                ])
        ])
])

