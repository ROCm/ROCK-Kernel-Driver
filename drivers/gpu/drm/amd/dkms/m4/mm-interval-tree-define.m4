dnl # commit 9826a516ff77c5820e591211e4f3e58ff36f46be
dnl # Author: Michel Lespinasse <walken@google.com>
dnl # Date: Mon Oct 8 16:31:35 2012 -0700
dnl # mm: interval tree updates
dnl # Update the generic interval tree code that was introduced in
dnl # "mm:replace  vma prio_tree with an interval tree".
AC_DEFUN([AC_AMDGPU_MM_INTERVAL_TREE_DEFINE],
	[AC_MSG_CHECKING([whether INTERVAL_TREE_DEFINE() is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/interval_tree_generic.h>
	],[
		#if !defined(INTERVAL_TREE_DEFINE)
		#error INTERVAL_TREE_DEFINE not #defined
		#endif
	],[
		AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_INTERVAL_TREE_DEFINE, 1, [whether INTERVAL_TREE_DEFINE() is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
