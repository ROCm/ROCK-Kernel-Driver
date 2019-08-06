dnl #
dnl # commit e14c23c647abfc1fed96a55ba376cd9675a54098
dnl # Author: Ville Syrjälä <ville.syrjala@linux.intel.com>
dnl # Date:   Fri Nov 18 21:52:55 2016 +0200
dnl # drm: Store a pointer to drm_format_info under drm_framebuffer
dnl #
dnl # There is no member format in drm_framebuffer until drm version(4.11.0)
dnl #
AC_DEFUN([AC_AMDGPU_FORMAT_IN_STRUCT_DRM_FRAMEBUFFER],
	[AC_MSG_CHECKING([whether there exist format in struct drm_framebuffer])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/sched.h>
		#include <drm/drm_fourcc.h>
		#include <drm/drm_framebuffer.h>
	], [
		struct drm_framebuffer fb;
		fb.format = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FORMAT_IN_STRUCT_DRM_FRAMEBUFFER, 1, [there exist format in struct drm_framebuffer])
	], [
		AC_MSG_RESULT(no)
	])
])
