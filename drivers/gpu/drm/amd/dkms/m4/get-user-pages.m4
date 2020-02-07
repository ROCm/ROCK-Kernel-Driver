dnl #
dnl # commit 768ae309a96103ed02eb1e111e838c87854d8b51
dnl # mm: replace get_user_pages() write/force parameters with gup_flags
dnl #
AC_DEFUN([AC_AMDGPU_GET_USER_PAGES_REAL], [
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		get_user_pages(0, 0, 0, NULL, NULL);
	], [get_user_pages], [mm/gup.c], [
		AC_DEFINE(HAVE_5ARGS_GET_USER_PAGES, 1,
			[get_user_pages() wants 5 args])
	], [
		dnl #
		dnl # commit c12d2da56d0e07d230968ee2305aaa86b93a6832
		dnl # mm/gup: Remove the macro overload API migration helpers
		dnl #         from the get_user*() APIs
		dnl #
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/mm.h>
		], [
			get_user_pages(0, 0, 0, 0, NULL, NULL);
		], [get_user_pages], [mm/gup.c], [
			AC_DEFINE(HAVE_6ARGS_GET_USER_PAGES, 1,
				[get_user_pages() wants 6 args])
		], [
			AC_DEFINE(HAVE_8ARGS_GET_USER_PAGES, 1,
				[get_user_pages() wants 8 args])
		])
	])
])

AC_DEFUN([AC_AMDGPU_GET_USER_PAGES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_backport.h], [
			dnl #
			dnl # redhat 7.x wrap a get_user_pages()
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <asm/current.h>
				#include <linux/sched.h>
				#include <drm/drm_backport.h>
			], [
				get_user_pages(0, 0, 0, NULL, NULL);
			], [
				AC_DEFINE(HAVE_5ARGS_GET_USER_PAGES, 1,
					[get_user_pages() get wrapped in drm_backport.h and wants 5 args])
			], [
				AC_AMDGPU_GET_USER_PAGES_REAL
			])
		], [
			AC_AMDGPU_GET_USER_PAGES_REAL
		])
	])
])
