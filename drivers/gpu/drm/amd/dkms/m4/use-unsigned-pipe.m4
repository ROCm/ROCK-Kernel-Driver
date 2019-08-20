dnl # commit 88e72717c2de4181d8a6de1b04315953ad2bebdf
dnl # Author: Thierry Reding <treding@nvidia.com>
dnl # drm/irq: Use unsigned int pipe in public API
dnl #
AC_DEFUN([AC_AMDGPU_USE_UNSIGNED_INT_PIPE],
	[AC_MSG_CHECKING([Use unsigned int pipe in public API])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
		void foo(struct drm_device *dev, unsigned int pipe){}
	], [
		struct drm_driver *bar = NULL;
		bar->disable_vblank = foo;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VGA_USE_UNSIGNED_INT_PIPE, 1, [Use unsigned int pipe in public API])
	], [
		AC_MSG_RESULT(no)
	])
])
