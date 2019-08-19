dnl #
dnl # commit 752ade68cbd81d0321dfecc188f655a945551b25
dnl # treewide: use kv[mz]alloc* rather than opencoded variants
dnl #
AC_DEFUN([AC_AMDGPU_KVMALLOC_ARRAY], [
	AC_MSG_CHECKING([whether kvmalloc_array() function is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		kvmalloc_array(0, 0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KVMALLOC_ARRAY, 1, [kvmalloc_array() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
