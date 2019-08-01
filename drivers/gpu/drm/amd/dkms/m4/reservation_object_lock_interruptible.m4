dnl #
dnl # commit 5d276a1acaa80f4b7a76577510a2b1835ce01f4f
dnl # dma-buf: add reservation_object_lock_interruptible()
dnl #
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_LOCK_INTERRUPTIBLE],
	[AC_MSG_CHECKING([whether reservation_object_lock_interruptible() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/reservation.h>
	], [
		reservation_object_lock_interruptible(NULL, NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_LOCK_INTERRUPTIBLE, 1, [reservation_object_lock_interruptible() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
