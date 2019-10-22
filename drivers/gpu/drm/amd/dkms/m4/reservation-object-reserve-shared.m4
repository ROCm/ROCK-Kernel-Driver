dnl #
dnl # commit ca05359f1e64cf8303ee532e50efe4ab7563d4a9
dnl # dma-buf: allow reserving more than one shared fence slot
dnl #
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_RESERVE_SHARED],
	[AC_MSG_CHECKING([whether reservation_object_reserve_shared() has p, i interface])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/reservation.h>
	], [
		reservation_object_reserve_shared(NULL, 0);
	], [reservation_object_reserve_shared], [drivers/dma-buf/reservation.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_RESERVE_SHARED_P_I, 1, [reservation_object_reserve_shared() has p, i interface])
	], [
		AC_MSG_RESULT(no)
	])
])
