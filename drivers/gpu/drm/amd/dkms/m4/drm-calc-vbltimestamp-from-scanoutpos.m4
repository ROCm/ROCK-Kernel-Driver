dnl #
dnl # commit 67680d3c0464
dnl # drm: vblank: use ktime_t instead of timeval
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <drm/drmP.h>
			], [
				drm_calc_vbltimestamp_from_scanoutpos(NULL, 0, NULL, (ktime_t *)NULL, 0);
			], [drm_calc_vbltimestamp_from_scanoutpos], [drivers/gpu/drm/drm_vblank.c], [
				AC_DEFINE(HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_USE_KTIMER_T_ARG, 1,
					[drm_calc_vbltimestamp_from_scanoutpos() use ktime_t arg])
			], [
				dnl #
				dnl # commit 1bf6ad622b9be
				dnl # drm/vblank: drop the mode argument from drm_calc_vbltimestamp_from_scanoutpos
				dnl #
				AC_KERNEL_TRY_COMPILE_SYMBOL([
					#include <drm/drmP.h>
				], [
					drm_calc_vbltimestamp_from_scanoutpos(NULL, 0, NULL, (struct timeval *)NULL, 0);
				], [drm_calc_vbltimestamp_from_scanoutpos], [drivers/gpu/drm/drm_vblank.c drivers/gpu/drm/drm_irq.c], [
					AC_DEFINE(HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_DROP_MOD_ARG, 1,
						[drm_calc_vbltimestamp_from_scanoutpos() drop mode arg])
				], [
					dnl #
					dnl # commit eba1f35dfe14
					dnl # drm: Move timestamping constants into drm_vblank_crtc
					dnl #
					AC_KERNEL_TRY_COMPILE_SYMBOL([
						#include <drm/drmP.h>
					], [
						drm_calc_vbltimestamp_from_scanoutpos(NULL, 0, NULL, NULL, 0, (const struct drm_display_mode *)NULL);
					], [drm_calc_vbltimestamp_from_scanoutpos], [drivers/gpu/drm/drm_irq.c], [
						AC_DEFINE(HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_HAVE_MODE_ARG, 1,
							[drm_calc_vbltimestamp_from_scanoutpos() remove crtc arg])
					], [
						dnl #
						dnl # commit 7da903ef0485
						dnl # drm: Pass the display mode to drm_calc_vbltimestamp_from_scanoutpos()
						dnl #
						AC_KERNEL_TRY_COMPILE_SYMBOL([
							#include <drm/drmP.h>
						], [
							drm_calc_vbltimestamp_from_scanoutpos(NULL, 0, NULL, NULL, 0, (const struct drm_crtc *)NULL, (const struct drm_display_mode *)NULL);
						], [drm_calc_vbltimestamp_from_scanoutpos], [drivers/gpu/drm/drm_irq.c], [
							AC_DEFINE(HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_HAVE_CRTC_MODE_ARG, 1,
								[drm_calc_vbltimestamp_from_scanoutpos() have the crtc & mode arg])
						])
					])
				])
			])
		], [
			AC_DEFINE(HAVE_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS_USE_KTIMER_T_ARG, 1,
				[drm_calc_vbltimestamp_from_scanoutpos() use ktime_t arg])
		])
	])
])
