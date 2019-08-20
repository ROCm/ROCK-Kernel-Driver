dnl #
dnl #commit 1c0e722ee1bf41681a8cc7101b7721e52f503da9
dnl #Author: Kevin Wang <Kevin1.Wang@amd.com>
dnl #Date:   Wed Aug 22 16:49:10 2018 +0800
dnl #drm/amdkcl: [KFD] ALL in One KFD KCL Fix for 4.18 rebase
dnl #
dnl #commit 68e21be2916b359fd8afb536c1911dc014cfd03e
dnl #Author: Ingo Molnar <mingo@kernel.org>
dnl #Date:   Wed Feb 1 19:08:20 2017 +0100
dnl #sched/headers: Move task->mm handling methods to <linux/sched/mm.h>
dnl #
AC_DEFUN([AC_AMDGPU_MMGRAB],[
		AC_MSG_CHECKING([whether mmgrab() is available])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/sched.h>
				#include <linux/sched/mm.h>
		], [
				mmgrab(NULL);
		], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_MMGRAB, 1, [whether mmgrab() is available])
		], [
				AC_MSG_RESULT(no)
		])
])
