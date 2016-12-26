dnl #
dnl # v5.5-rc2-1557-ge3eff4b5d91e drm/amdgpu: Convert to CRTC VBLANK callbacks
dnl # v5.5-rc2-1556-gea702333e567 drm/amdgpu: Convert to struct drm_crtc_helper_funcs.get_scanout_position()
dnl # v5.5-rc2-1555-g7fe3f0d15aac drm: Add get_vblank_timestamp() to struct drm_crtc_funcs
dnl # v5.5-rc2-1554-gf1e2b6371c12 drm: Add get_scanout_position() to struct drm_crtc_helper_funcs
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		],[
			struct drm_crtc_funcs *ptr = NULL;
			ptr->get_vblank_timestamp(NULL, NULL, NULL, 0);
		],[
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP,
				1,
				[struct drm_crtc_funcs->get_vblank_timestamp() is available])
		],[
			AC_AMDGPU_GET_SCANOUT_POSITION_IN_DRM_DRIVER
			AC_AMDGPU_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER
			AC_AMDGPU_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS
		])
	])
])

dnl #
dnl # v4.11-rc3-945-g41292b1fa13a
dnl # drm: Add acquire ctx parameter to ->page_flip(_target)
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_PAGE_FLIP_TARGET], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc_funcs *funcs = NULL;
			funcs->page_flip_target(NULL, NULL, NULL, 0, 0, NULL);
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_PAGE_FLIP_TARGET_CTX, 1,
				[drm_crtc_funcs->page_flip_target() wants ctx parameter])
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_PAGE_FLIP_TARGET, 1,
				[drm_crtc_funcs->page_flip_target() is available])
		], [
			dnl #
			dnl # v4.8-rc1-112-gc229bfbbd04a
			dnl # drm: Add page_flip_target CRTC hook v2
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_crtc.h>
			], [
				struct drm_crtc_funcs *funcs = NULL;
				funcs->page_flip_target(NULL, NULL, NULL, 0, 0);
			], [
				AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_PAGE_FLIP_TARGET, 1,
					[drm_crtc_funcs->page_flip_target() is available])
			])
		])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS], [
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_PAGE_FLIP_TARGET
])
