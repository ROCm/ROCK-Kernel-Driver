AC_DEFUN([AC_AMDGPU_GET_USER_PAGES_REMOTE], [
	AC_KERNEL_DO_BACKGROUND([
		dnl #
		dnl # v5.8-12463-g64019a2e467a
		dnl # mm/gup: remove task_struct pointer for all gup code
		dnl #
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/mm.h>
		], [
			get_user_pages_remote(NULL, 0, 0, 0, NULL, NULL, NULL);
		], [get_user_pages_remote],[mm/gup.c],[
			AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_REMOVE_TASK_STRUCT, 1,
				[get_user_pages_remote() remove task_struct pointer])
		], [
			dnl #
			dnl # commit v4.9-7744-g5b56d49fc31d
			dnl # mm: add locked parameter to get_user_pages_remote()
			dnl #
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <linux/mm.h>
			], [
				get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL, NULL);
			], [get_user_pages_remote],[mm/gup.c],[
				AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_LOCKED, 1,
					[get_user_pages_remote() wants locked parameter])
			],[
				dnl #
				dnl # commit v4.8-14096-g9beae1ea8930
				dnl # mm: replace get_user_pages_remote() write/force parameters
				dnl #     with gup_flags
				dnl #
				AC_KERNEL_TRY_COMPILE_SYMBOL([
					#include <linux/mm.h>
				], [
					get_user_pages_remote(NULL, NULL, 0, 0, 0, NULL, NULL);
				], [get_user_pages_remote],[mm/gup.c],[
					AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_GUP_FLAGS, 1,
						[get_user_pages_remote() wants gup_flags parameter])
				],[
					dnl #
					dnl # commit v6.4-rc4-55-gca5e863233e8
					dnl #  mm/gup: remove vmas parameter from get_user_pages_remote()
					dnl #
					AC_KERNEL_TRY_COMPILE_SYMBOL([
						#include <linux/mm.h>
					], [
						get_user_pages_remote(NULL, 0, 0, 0, NULL, NULL);
					], [get_user_pages_remote],[mm/gup.c],[
						AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_REMOVE_VMAS, 1,
							[get_user_pages_remote() remove argument vmas])
					],[
						AC_DEFINE(HAVE_GET_USER_PAGES_REMOTE_INTRODUCED, 1,
							[get_user_pages_remote() is introduced with initial prototype])
					])
				])
			])
		])
	])
])

