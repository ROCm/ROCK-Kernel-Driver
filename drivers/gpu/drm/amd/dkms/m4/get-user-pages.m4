dnl #
dnl # commit 768ae309a96103ed02eb1e111e838c87854d8b51
dnl # mm: replace get_user_pages() write/force parameters with gup_flags
dnl #
AC_DEFUN([AC_AMDGPU_GET_USER_PAGES], [
	AC_MSG_CHECKING([whether get_user_pages() wants 5 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		get_user_pages(0, 0, 0, NULL, NULL);
	], [get_user_pages], [mm/gup.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_5ARGS_GET_USER_PAGES, 1, [get_user_pages() wants 5 args])
	], [
		dnl #
		dnl # commit c12d2da56d0e07d230968ee2305aaa86b93a6832
		dnl # mm/gup: Remove the macro overload API migration helpers from the get_user*() APIs
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether get_user_pages() wants 6 args])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/mm.h>
		], [
			get_user_pages(0, 0, 0, 0, NULL, NULL);
		], [get_user_pages], [mm/gup.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_6ARGS_GET_USER_PAGES, 1, [get_user_pages() wants 6 args])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
