dnl #
dnl # commit 82975c46da8275a2a212cd94049bbef9bb961da2
dnl # perf: Export perf_event_update_userpage
dnl #   Export perf_event_update_userpage() so that PMU driver using them,
dnl #   can be built as modules
dnl #
AC_DEFUN([AC_AMDGPU_PERF_EVENT_UPDATE_USERPAGE],
	[AC_MSG_CHECKING([whether perf_event_update_userpage is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/perf_event.h>
	],[
		perf_event_update_userpage(NULL);
	],[perf_event_update_userpage],[kernel/events/core.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PERF_EVENT_UPDATE_USERPAGE, 1, [whether  perf_event_update_userpage is available])
	],[
		AC_MSG_RESULT(no)
	])
])
