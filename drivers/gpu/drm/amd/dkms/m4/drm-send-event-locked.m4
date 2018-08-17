dnl #
dnl # commit v4.4-rc6-1039-gfb740cf2492c
dnl # drm: Create drm_send_event helpers
dnl #
dnl # commit v4.10-rc8-1407-ga8f8b1d9b870
dnl # drm: Extract drm_file.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_SEND_EVENT_LOCKED], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_file.h], [
			AC_DEFINE(HAVE_DRM_SEND_EVENT_LOCKED, 1,
				[drm_send_event_locked() function is available])
		], [
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <drm/drmP.h>
			], [
				drm_send_event_locked(NULL, NULL);
			], [drm_send_event_locked], [drivers/gpu/drm/drm_file.c], [
				AC_DEFINE(HAVE_DRM_SEND_EVENT_LOCKED, 1,
					[drm_send_event_locked() function is available])
			])
		])
	])
])
