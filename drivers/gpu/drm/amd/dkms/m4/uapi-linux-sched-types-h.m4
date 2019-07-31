dnl #
dnl # commit 607ca46e97a1b6594b29647d98a32d545c24bdff
dnl # Author: David Howells <dhowells@redhat.com>
dnl # Date:   Sat Oct 13 10:46:48 2012 +0100
dnl #
dnl # UAPI: (Scripted) Disintegrate include/linux
dnl #
dnl # Signed-off-by: David Howells <dhowells@redhat.com>
dnl # Acked-by: Arnd Bergmann <arnd@arndb.de>
dnl # Acked-by: Thomas Gleixner <tglx@linutronix.de>
dnl # Acked-by: Michael Kerrisk <mtk.manpages@gmail.com>
dnl # Acked-by: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
dnl # Acked-by: Dave Jones <davej@redhat.com>
dnl #
AC_DEFUN([AC_AMDGPU_SCHED_TYPES_H],
	[AC_MSG_CHECKING([whether sched/types.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([uapi/linux/sched/types.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SCHED_TYPES_H, 1, [sched/types.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
