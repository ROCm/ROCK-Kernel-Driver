dnl #
dnl # commit 5b56d49fc31dbb0487e14ead790fc81ca9fb2c99
dnl # mm: add locked parameter to get_user_pages_remote()
dnl #
AC_DEFUN([AC_AMDGPU_GET_USER_PAGES_REMOTE], [
	AC_MSG_CHECKING([whether get_user_pages_remote() wants 8 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL, NULL);
	], [get_user_pages_remote], [mm/gup.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_8ARGS_GET_USER_PAGES_REMOTE, 1, [get_user_pages_remote() wants 8 args])
	], [
		AC_MSG_RESULT(no)
	])
])
