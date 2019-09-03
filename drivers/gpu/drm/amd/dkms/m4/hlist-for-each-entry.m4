dnl #
dnl # commit b67bfe0d42cac56c512dd5da4b1b347a23f4b70a
dnl # hlist: drop the node parameter from iterators
dnl #
AC_DEFUN([AC_AMDGPU_HLIST_FOR_EACH_ENTRY_WITH_4ARGS],
	[AC_MSG_CHECKING([whether hlist_for_each_entry with 4args is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/list.h>
	],[
		hlist_for_each_entry(0, 0, 0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_HLIST_FOR_EACH_ENTRY_WITH_4ARGS, 1, [hlist_for_each_entry with 4args  is available])
	],[
		AC_MSG_RESULT(no)
	])
])
