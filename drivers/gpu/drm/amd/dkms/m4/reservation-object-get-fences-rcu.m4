dnl #
dnl # commit 3c3b177a9369b26890ced004867fb32708e8ef5b
dnl # reservation: add suppport for read-only access using rcu
dnl #
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_GET_FENCES_RCU],
	[AC_MSG_CHECKING([whether reservation_object_get_fences_rcu() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/reservation.h>
	], [
		reservation_object_get_fences_rcu(NULL, NULL, NULL, NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_GET_FENCES_RCU, 1, [reservation_object_get_fences_rcu() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
