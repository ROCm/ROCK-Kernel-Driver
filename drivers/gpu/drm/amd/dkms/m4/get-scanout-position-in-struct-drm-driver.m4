dnl #
dnl # commit v4.11-rc7-1902-g1bf6ad622b9b
dnl # drm/vblank: drop the mode argument from drm_calc_vbltimestamp_from_scanoutpos
dnl #
AC_DEFUN([AC_AMDGPU_GET_SCANOUT_POSITION_IN_DRM_DRIVER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				bool foo(struct drm_device *dev, unsigned int pipe,
					bool in_vblank_irq, int *vpos, int *hpos,
					ktime_t *stime, ktime_t *etime,
					const struct drm_display_mode *mode)
				{
					return false;
				}
			], [
				struct drm_driver *bar = NULL;
				bar->get_scanout_position = foo;
			], [
				AC_DEFINE(HAVE_GET_SCANOUT_POSITION_RETURN_BOOL, 1,
					[get_scanout_position return bool])
				AC_DEFINE(HAVE_VGA_USE_UNSIGNED_INT_PIPE, 1,
					[get_scanout_position use unsigned int pipe])
			], [
				dnl #
				dnl # commit v4.3-rc3-73-g88e72717c2de
				dnl # drm/irq: Use unsigned int pipe in public API
				dnl #
				AC_KERNEL_TRY_COMPILE([
					#include <drm/drmP.h>
					int foo(struct drm_device *dev, unsigned int pipe,
						unsigned int flags, int *vpos, int *hpos,
						ktime_t *stime, ktime_t *etime,
						const struct drm_display_mode *mode)
					{
						return 0;
					}
				], [
					struct drm_driver *bar = NULL;
					bar->get_scanout_position = foo;
				], [
					AC_DEFINE(HAVE_VGA_USE_UNSIGNED_INT_PIPE, 1,
						[get_scanout_position use unsigned int pipe])
				], [
					dnl #
					dnl # commit v4.3-rc2-44-g3bb403bf421b
					dnl # drm: Stop using linedur_ns and pixeldur_ns for vblank timestamps
					dnl #
					AC_KERNEL_TRY_COMPILE([
						#include <drm/drmP.h>
						int foo(struct drm_device *dev, int crtc,
							unsigned int flags,
							int *vpos, int *hpos,
							ktime_t *stime, ktime_t *etime,
							const struct drm_display_mode *mode)
						{
							return 0;
						}
					], [
						struct drm_driver *bar = NULL;
						bar->get_scanout_position = foo;
					], [
						AC_DEFINE(HAVE_GET_SCANOUT_POSITION_HAS_DRM_DISPLAY_MODE_ARG, 1,
							[get_scanout_position has struct drm_display_mode arg])
					], [
						dnl #
						dnl # commit v3.12-rc3-485-g8f6fce03ddaf
						dnl # drm: Push latency sensitive bits of vblank scanoutpos timestamping into kms drivers.
						dnl #
						AC_KERNEL_TRY_COMPILE([
							#include <drm/drmP.h>
							int foo(struct drm_device *dev, int crtc,
								int *vpos, int *hpos, ktime_t *stime,
								ktime_t *etime)
							{
								return 0;
							}
						], [
							struct drm_driver *bar = NULL;
							bar->get_scanout_position = foo;
						], [
							AC_DEFINE(HAVE_GET_SCANOUT_POSITION_HAS_TIMESTAMP_ARG, 1,
								[get_scanout_position has timestamp arg])
						])
					])
				])
			])
		], [
			AC_DEFINE(HAVE_GET_SCANOUT_POSITION_RETURN_BOOL, 1,
				[get_scanout_position return bool])
			AC_DEFINE(HAVE_VGA_USE_UNSIGNED_INT_PIPE, 1,
				[get_scanout_position use unsigned int pipe])
		])
	])
])
