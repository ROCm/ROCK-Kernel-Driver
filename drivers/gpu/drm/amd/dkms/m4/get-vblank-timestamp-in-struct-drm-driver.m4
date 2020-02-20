dnl # commit 67680d3c04645
dnl # drm: vblank: use ktime_t instead of timeval
dnl #
AC_DEFUN([AC_AMDGPU_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER],
	[AC_MSG_CHECKING([whether get_vblank_timestamp has ktime_t arg])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
		bool foo(struct drm_device *dev, unsigned int pipe,
			int *max_error,
			ktime_t *vblank_time,
			bool in_vblank_irq) { return 0; }
	], [
		struct drm_driver *bar = NULL;
		bar->get_vblank_timestamp = foo;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_HAS_KTIME_T, 1, [get_vblank_timestamp has ktime_t arg])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether get_vblank_timestamp has bool in_vblank_irq arg])
		dnl
		dnl # commit 3fcdcb270936a
		dnl # drm/vblank: Switch to bool in_vblank_irq in get_vblank_timestamp
		dnl #
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drmP.h>
			bool foo(struct drm_device *dev, unsigned int pipe,
					int *max_error,
					struct timeval *vblank_time,
					bool in_vblank_irq) { return 0; }
		], [
			struct drm_driver *bar = NULL;
			bar->get_vblank_timestamp = foo;
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_HAS_BOOL_IN_VBLANK_IRQ, 1, [get_vblank_timestamp has bool in_vblank_irq arg])
		], [
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether get_vblank_timestamp return bool])
			dnl #
			dnl # commit id d673c02c4bdbc
			dnl # drm/vblank: Switch drm_driver->get_vblank_timestamp to return a bool
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				bool foo(struct drm_device *dev, unsigned int pipe,
						int *max_error,
						struct timeval *vblank_time,
						unsigned flags) { return 0; }
			], [
				struct drm_driver *bar = NULL;
				bar->get_vblank_timestamp = foo;
			], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER_RETURN_BOOL, 1, [get_vblank_timestamp return bool])
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])
