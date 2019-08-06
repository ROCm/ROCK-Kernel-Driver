dnl # commit 122020af856181c24fe45903e43e3cc987c175f7
dnl # dma-buf: Provide wrappers for reservation's lock
dnl # Joonas complained that writing ww_mutex_lock(&resv->lock, ctx) was too
dnl # intrusive compared to reservation_object_lock(resv, ctx);
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_LOCK],
	[AC_MSG_CHECKING([whether reservation_object_lock() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/reservation.h>
	],[
		reservation_object_lock(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_LOCK, 1, [reservation_object_lock() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
