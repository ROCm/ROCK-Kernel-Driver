dnl #
dnl # commit 4c3dbb2c312c9fafbac30d98c523b8b1f3455d78
dnl # Author: Noralf Trønnes <noralf@tronnes.org>
dnl # Date:   Sun Aug 13 15:31:44 2017 +0200
dnl # drm: Add GEM backed framebuffer library
dnl #
AC_DEFUN([AC_AMDGPU_OBJS_IN_STRUCTURE_DRM_FRAMEBUFFER],
	[AC_MSG_CHECKING([for obj field within drm_framebuffer structure])
	AC_KERNEL_TRY_COMPILE([
		#include <uapi/drm/drm.h>
		#include <linux/sched.h>
		#include <drm/drm_framebuffer.h>
	], [
		struct drm_framebuffer fb;
		fb.obj[0] = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PRIVATE_OBJS_IN_STRUCTURE_DRM_FRAMEBUFFER, 1, [drm_framebuffer structure contains obj field])
	], [
		AC_MSG_RESULT(no)
	])
])
