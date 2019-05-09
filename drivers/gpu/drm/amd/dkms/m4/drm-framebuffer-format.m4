dnl #
dnl # commit e14c23c647abfc1fed96a55ba376cd9675a54098
dnl # drm: Store a pointer to drm_format_info under drm_framebuffer
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FRAMEBUFFER_FORMAT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				#include <drm/drm_framebuffer.h>
			], [
				struct drm_framebuffer *foo = NULL;
				foo->format = NULL;
			], [
				AC_DEFINE(HAVE_DRM_FRAMEBUFFER_FORMAT, 1,
					[whether struct drm_framebuffer have format])
			])
		], [
			AC_DEFINE(HAVE_DRM_FRAMEBUFFER_FORMAT, 1,
				[whether struct drm_framebuffer have format])
		])
	])
])
