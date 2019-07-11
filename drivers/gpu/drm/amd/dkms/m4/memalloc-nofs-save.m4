dnl #
dnl # commit 7dea19f9ee636cb244109a4dba426bbb3e5304b7
dnl # mm: introduce memalloc_nofs_{save,restore} API
dnl #
AC_DEFUN([AC_AMDGPU_MEMALLOC_NOFS_SAVE],
	[AC_MSG_CHECKING([whether memalloc_nofs_save() and memalloc_nofs_restore() functions are available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/sched/mm.h>
	],[
		memalloc_nofs_save();
		memalloc_nofs_restore(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MEMALLOC_NOFS_SAVE, 1, [memalloc_nofs_save() and memalloc_nofs_restore() are available])
	],[
		AC_MSG_RESULT(no)
	])
])
