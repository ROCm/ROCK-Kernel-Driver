AC_DEFUN([AC_AMDGPU_GET_USER_PAGES], [
	AC_MSG_CHECKING([whether get_user_pages() wants 5 args])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_backport.h], [
dnl #
dnl # redhat 7.x wrap a get_user_pages()
dnl #
		AC_KERNEL_TRY_COMPILE([
			#include <linux/sched.h>
			#include <drm/drm_backport.h>
		], [
			get_user_pages(0, 0, 0, NULL, NULL);
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_5ARGS_GET_USER_PAGES, 1, [get_user_pages get wrapped in drm_backport.h and wants 5 args])
		], [
			AC_MSG_RESULT(no)
			AC_DEFINE(HAVE_8ARGS_GET_USER_PAGES, 1, [get_user_pages() wants 8 args])
		])
	], [
dnl #
dnl # commit 768ae309a96103ed02eb1e111e838c87854d8b51
dnl # mm: replace get_user_pages() write/force parameters with gup_flags
dnl #
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/mm.h>
		], [
			get_user_pages(0, 0, 0, NULL, NULL);
		], [get_user_pages], [mm/gup.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_5ARGS_GET_USER_PAGES, 1, [get_user_pages() wants 5 args])
		], [
	dnl #
	dnl # commit cde70140fed8429acf7a14e2e2cbd3e329036653
	dnl # mm/gup: Overload get_user_pages() functions
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
				AC_DEFINE(HAVE_8ARGS_GET_USER_PAGES, 1, [get_user_pages() wants 8 args])
			])
		])
	])
])
