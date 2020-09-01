AC_DEFUN([AC_AMDGPU_GET_USER_PAGES_REMOTE], [
	AC_KERNEL_DO_BACKGROUND([
		dnl #
		dnl # v4.5-rc4-71-g1e9877902dc7
		dnl # mm/gup: Introduce get_user_pages_remote()
		dnl #
		AC_KERNEL_CHECK_SYMBOL_EXPORT([get_user_pages_remote],[mm/gup.c],
		[
			dnl #
			dnl # commit v4.9-7744-g5b56d49fc31d
			dnl # mm: add locked parameter to get_user_pages_remote()
			dnl #
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <linux/mm.h>
			], [
				get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL, NULL);
			], [get_user_pages_remote], [mm/gup.c], [
				AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_LOCKED, 1,
					[get_user_pages_remote() wants locked parameter])
			],  [
				dnl #
				dnl # commit v4.8-14096-g9beae1ea8930
				dnl # mm: replace get_user_pages_remote() write/force parameters
				dnl #     with gup_flags
				dnl #
				AC_KERNEL_TRY_COMPILE_SYMBOL([
					#include <linux/mm.h>
				], [
					get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL);
				], [get_user_pages_remote], [mm/gup.c], [
					AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_GUP_FLAGS, 1,
						[get_user_pages_remote() wants gup_flags parameter])
				])
			], [
				AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_INTRODUCED, 1,
					[get_user_pages_remote() is introduced with initial prototype])
			])
		])
	])
])
