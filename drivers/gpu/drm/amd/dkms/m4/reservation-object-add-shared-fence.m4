dnl #
dnl # commit 04a5faa8cbe5a8eaf152cb88959ba6360c26e702
dnl # Author: Maarten Lankhorst <maarten.lankhorst@canonical.com>
dnl # Date:   Tue Jul 1 12:57:54 2014 +0200
dnl # reservation: update api and add some helpers
dnl #
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_ADD_SHARED_FENCE],
	[AC_MSG_CHECKING([whether reservation_object_add_shared_fence() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/reservation.h>
	], [
		reservation_object_add_shared_fence(NULL, NULL);
	], [reservation_object_add_shared_fence], [drivers/dma-buf/reservation.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_ADD_SHARED_FENCE, 1, [reservation_object_add_shared_fence() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
