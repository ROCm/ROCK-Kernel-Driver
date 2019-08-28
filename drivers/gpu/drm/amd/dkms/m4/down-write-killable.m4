dnl #
dnl # commit 916633a403702549d37ea353e63a68e5b0dc27ad
dnl # locking/rwsem: Provide down_write_killable()
dnl #
AC_DEFUN([AC_AMDGPU_DOWN_WRITE_KILLABLE],
	[AC_MSG_CHECKING([whether down_write_killable() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/rwsem.h>
	],[
		down_write_killable(NULL);
	],[down_write_killable],[kernel/locking/rwsem.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DOWN_WRITE_KILLABLE, 1, [down_write_killable() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
