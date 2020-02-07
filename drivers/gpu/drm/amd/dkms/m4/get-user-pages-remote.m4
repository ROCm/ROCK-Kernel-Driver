dnl #
dnl # commit 5b56d49fc31dbb0487e14ead790fc81ca9fb2c99
dnl # mm: add locked parameter to get_user_pages_remote()
dnl #
AC_DEFUN([AC_AMDGPU_GET_USER_PAGES_REMOTE_REAL], [
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL, NULL);
	], [get_user_pages_remote], [mm/gup.c], [
		AC_DEFINE(HAVE_8ARGS_GET_USER_PAGES_REMOTE, 1,
			[get_user_pages_remote() wants 8 args])
	],  [
		dnl #
		dnl # commit 9beae1ea8930
		dnl # mm: replace get_user_pages_remote() write/force parameters
		dnl #     with gup_flags
		dnl #
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/mm.h>
		], [
			get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL);
		], [get_user_pages_remote], [mm/gup.c], [
			AC_DEFINE(HAVE_7ARGS_GET_USER_PAGES_REMOTE, 1,
				[get_user_pages_remote() wants 7 args])
		])
	])
])

AC_DEFUN([AC_AMDGPU_GET_USER_PAGES_REMOTE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_backport.h], [
			dnl #
			dnl # redhat 7.x wrap a get_user_pages_remote()
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <asm/current.h>
				#include <linux/sched.h>
				#include <drm/drm_backport.h>
			], [
				get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL, NULL);
			], [
				AC_DEFINE(HAVE_8ARGS_GET_USER_PAGES_REMOTE, 1,
					[get_user_pages_remote() get wrapped in drm_backport.h and wants 8 args])
			], [
				AC_AMDGPU_GET_USER_PAGES_REMOTE_REAL
			])
		], [
			AC_AMDGPU_GET_USER_PAGES_REMOTE_REAL
		])
	])
])
