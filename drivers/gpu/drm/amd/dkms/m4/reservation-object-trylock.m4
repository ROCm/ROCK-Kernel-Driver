dnl # commit 2955b73def6712b693fc7ad82b34b3831faaa146
dnl # dma-buf/reservation: Wrap ww_mutex_trylock
dnl # In a similar fashion to reservation_object_lock() and
dnl # reservation_object_unlock(), ww_mutex_trylock is also useful and so is
dnl # worth wrapping for consistency.
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_TRYLOCK],
	[AC_MSG_CHECKING([whether reservation_object_trylock() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/reservation.h>
	],[
		reservation_object_trylock(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_TRYLOCK, 1, [reservation_object_trylock() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
