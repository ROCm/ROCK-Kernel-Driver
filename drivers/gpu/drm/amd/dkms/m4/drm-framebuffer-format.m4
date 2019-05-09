dnl #
dnl # commit e14c23c647abfc1fed96a55ba376cd9675a54098
dnl # drm: Store a pointer to drm_format_info under drm_framebuffer
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FRAMEBUFFER_FORMAT],
	[AC_MSG_CHECKING([whether struct drm_framebuffer have format])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
		#include <drm/drm_framebuffer.h>
	], [
		struct drm_framebuffer *foo = NULL;
		foo->format = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FRAMEBUFFER_FORMAT, 1, [whether struct drm_framebuffer have format])
	], [
		AC_MSG_RESULT(no)
	])
])
