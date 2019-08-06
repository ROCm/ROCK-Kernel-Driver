AC_DEFUN([AC_AMDGPU_MMGRAB_REAL],[
	dnl #
	dnl # commit v4.10-11141-g68e21be2916b
	dnl # sched/headers: Move task->mm handling methods to <linux/sched/mm.h>
	dnl #
	AC_KERNEL_TRY_COMPILE([
		#include <linux/sched/mm.h>
	], [
		mmgrab(NULL);
	], [
		AC_DEFINE(HAVE_MMGRAB, 1,
			[mmgrab() is available in linux/sched/mm.h])
	], [
		dnl #
		dnl # commit f1f1007644ffc
		dnl # mm: add new mmgrab() helper
		dnl #
		AC_KERNEL_TRY_COMPILE([
			#include <linux/sched.h>
		], [
			mmgrab(NULL);
		], [
			AC_DEFINE(HAVE_MMGRAB, 1,
				[mmgrab() is available in linux/sched.h])
		])
	])
])

AC_DEFUN([AC_AMDGPU_MMGRAB], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_backport.h], [
			dnl #
			dnl # rhel 7.x wrapp mmgrab() in drm_backport.h
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_backport.h>
			], [
				mmgrab(NULL);
			], [
				AC_DEFINE(HAVE_MMGRAB, 1,
				[mmgrab() is available in drm_backport.h])
			], [
				AC_AMDGPU_MMGRAB_REAL
			])
		], [
			AC_AMDGPU_MMGRAB_REAL
		])
	])
])
