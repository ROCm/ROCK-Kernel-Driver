dnl #
dnl # e3eff4b5d91e drm/amdgpu: Convert to CRTC VBLANK callbacks
dnl # ea702333e567 drm/amdgpu: Convert to struct drm_crtc_helper_funcs.get_scanout_position()
dnl # 7fe3f0d15aac drm: Add get_vblank_timestamp() to struct drm_crtc_funcs
dnl # f1e2b6371c12 drm: Add get_scanout_position() to struct drm_crtc_helper_funcs
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
dnl # commit v4.10-rc5-1070-g84e354839b15
dnl # drm: add vblank hooks to struct drm_crtc_funcs
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_ENABLE_VBLANK], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc_funcs *crtc_funcs = NULL;
			crtc_funcs->enable_vblank(NULL);
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_ENABLE_VBLANK, 1, [
				drm_crtc_funcs->enable_vblank() is available])
		])
	])
])

dnl #
dnl # v5.2-rc5-2034-g8fb843d179a6 drm/amd/display: add functionality to get pipe CRC source.
dnl # v4.18-rc3-759-g3b3b8448ebd1 drm/amdgpu_dm/crc: Implement verify_crc_source callback
dnl # v4.18-rc3-757-g4396551e9cf3 drm: crc: Introduce get_crc_sources callback
dnl # v4.18-rc3-756-gd5cc15a0c66e drm: crc: Introduce verify_crc_source callback
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GET_VERIFY_CRC_SOURCES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc_funcs *crtc_funcs = NULL;
			crtc_funcs->get_crc_sources(NULL, NULL);
			crtc_funcs->verify_crc_source(NULL, NULL, NULL);
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_GET_VERIFY_CRC_SOURCES, 1, [
				drm_crtc_funcs->{get,verify}_crc_sources() is available])
		])
	])
])

dnl #
dnl # v4.18-rc3-781-gc0811a7d5bef drm/crc: Cleanup crtc_crc_open function
dnl # v4.16-rc1-363-g31aec354f92c drm/amd/display: Implement interface for CRC on CRTC
dnl # v4.8-rc8-1429-g9edbf1fa600a drm: Add API for capturing frame CRCs
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_SET_CRC_SOURCE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc *crtc = NULL;
			int ret;

			ret = crtc->funcs->set_crc_source(NULL, NULL);
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_SET_CRC_SOURCE, 1,
				[crtc->funcs->set_crc_source() is available])
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_SET_CRC_SOURCE_2ARGS, 1,
				[crtc->funcs->set_crc_source() wants 2 args])
		], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_crtc.h>
			], [
				struct drm_crtc *crtc = NULL;
				int ret;

				ret = crtc->funcs->set_crc_source(NULL, NULL, NULL);
			], [
				AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_SET_CRC_SOURCE, 1,
					[crtc->funcs->set_crc_source() is available])
			])
		])
	])
])

dnl #
dnl # v4.11-rc5-1392-g6d124ff84533 drm: Add acquire ctx to ->gamma_set hook
dnl # 		int (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
dnl # 	-                        uint32_t size);
dnl # 	+                        uint32_t size,
dnl # 	+                        struct drm_modeset_acquire_ctx *ctx);
dnl # v4.7-rc1-260-g7ea772838782 drm/core: Change declaration for gamma_set.
dnl # 	-       void (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
dnl # 	-                         uint32_t start, uint32_t size);
dnl # 	+       int (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
dnl # 	+                        uint32_t size);
dnl # v4.5-rc3-706-g5488dc16fde7 drm: introduce pipe color correction properties
dnl # 	+void drm_atomic_helper_legacy_gamma_set(struct drm_crtc *crtc,
dnl # 	+                                       u16 *red, u16 *green, u16 *blue,
dnl # 	+                                       uint32_t start, uint32_t size)
dnl # v2.6.35-260-g7203425a943e drm: expand gamma_set
dnl # 		void (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
dnl # 	-                         uint32_t size);
dnl # 	+                         uint32_t start, uint32_t size);
dnl # v2.6.28-8-gf453ba046074 DRM: add mode setting support
dnl # 	+       void (*gamma_set)(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
dnl # 	+                         uint32_t size);
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc *crtc = NULL;
			int ret;

			ret = crtc->funcs->gamma_set(NULL, NULL, NULL, NULL, 0, NULL);
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_6ARGS, 1,
				[crtc->funcs->gamma_set() wants 6 args])
			AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_LEGACY_GAMMA_SET, 1,
				[drm_atomic_helper_legacy_gamma_set() is available])
		], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_crtc.h>
			], [
				struct drm_crtc *crtc = NULL;
				int ret;

				ret = crtc->funcs->gamma_set(NULL, NULL, NULL, NULL, 0);
			], [
				AC_DEFINE(HAVE_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET_5ARGS, 1,
					[crtc->funcs->gamma_set() wants 5 args])
				AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_LEGACY_GAMMA_SET, 1,
					[drm_atomic_helper_legacy_gamma_set() is available])
			], [
				AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_atomic_helper_legacy_gamma_set],
				[drivers/gpu/drm/drm_atomic_helper.c],[
					AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_LEGACY_GAMMA_SET, 1,
						[drm_atomic_helper_legacy_gamma_set() is available])
				])
			])
		])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS], [
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GET_VBLANK_TIMESTAMP
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_ENABLE_VBLANK
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GET_VERIFY_CRC_SOURCES
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_SET_CRC_SOURCE
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS_GAMMA_SET
])
