#dnl
#dnl commit v4.14-rc4-65-g76f8507f7a64
#dnl locking/rwsem: Add down_read_killable()
#dnl
AC_DEFUN([AC_AMDGPU_DOWN_READ_KILLABLE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/rwsem.h>
		],[
			int ret;
			ret = down_read_killable(NULL);
		],[down_read_killable], [kernel/locking/rwsem.c],[
			AC_DEFINE(HAVE_DOWN_READ_KILLABLE, 1,
				[down_read_killable() is available])]
		)
	])
])
