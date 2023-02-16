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
		])
	])
])

dnl #
dnl # v5.10-1961-g6ca2ab8086af drm: automatic legacy gamma support
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_OPTIONAL], [
	AC_KERNEL_CHECK_SYMBOL_EXPORT(
		[drm_atomic_helper_legacy_gamma_set], [drivers/gpu/drm/drm_atomic_helper.c], [],
		[
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_OPTIONAL, 1,
				[HAVE_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_OPTIONAL is available])
		])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS], [
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_OPTIONAL
])
