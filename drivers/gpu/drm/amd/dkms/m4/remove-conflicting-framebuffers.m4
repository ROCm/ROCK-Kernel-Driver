dnl #
dnl # commit 46eeb2c144956e88197439b5ee5cf221a91b0a81
dnl # video/fb: Propagate error code from failing to unregister conflicting fb
dnl #
AC_DEFUN([AC_AMDGPU_REMOVE_CONFLICTING_FRAMEBUFFERS],
	[AC_MSG_CHECKING([whether remove_conflicting_framebuffers() returns int])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/fb.h>

		int (*foo)(struct apertures_struct*, const char*, bool) =
			&remove_conflicting_framebuffers;
	],[
		remove_conflicting_framebuffers(NULL, NULL, false);
	],[remove_conflicting_framebuffers], [drivers/video/fbdev/core/fbmem.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REMOVE_CONFLICTING_FRAMEBUFFERS_RETURNS_INT, 1, [kremove_conflicting_framebuffers() returns int])
	],[
		AC_MSG_RESULT(no)
	])
])
