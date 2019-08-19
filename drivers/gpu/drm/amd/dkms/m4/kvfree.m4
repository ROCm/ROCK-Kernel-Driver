dnl #
dnl # commit 39f1f78d53b9bcbca91967380c5f0f2305a5c55f
dnl # nick kvfree() from apparmor
dnl #
AC_DEFUN([AC_AMDGPU_KVFREE], [
	AC_MSG_CHECKING([whether kvfree() function is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	],[
		kvfree(NULL);
	], [kvfree], [mm/util.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KVFREE, 1, [kvfree() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
