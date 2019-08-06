dnl # commit 3c3b177a9369b26890ced004867fb32708e8ef5b
dnl # reservation: add suppport for read-only access using rcu
dnl # This adds some extra functions to deal with rcu.
dnl # reservation_object_get_fences_rcu() will obtain the list of shared
dnl # and exclusive fences without obtaining the ww_mutex.
dnl # reservation_object_wait_timeout_rcu() will wait on all fences of the
dnl # reservation_object, without obtaining the ww_mutex.
dnl # reservation_object_test_signaled_rcu() will test if all fences of the
dnl # reservation_object are signaled without using the ww_mutex.
dnl # reservation_object_get_excl and reservation_object_get_list require
dnl # the reservation object to be held, updating requires
dnl # write_seqcount_begin/end. If only the exclusive fence is needed,
dnl # rcu_dereference followed by fence_get_rcu can be used, if the shared
dnl # fences are needed it's recommended to use the supplied functions.
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_TEST_SIGNALED_RCU],
	[AC_MSG_CHECKING([whether reservation_object_test_signaled_rcu() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/reservation.h>
	],[
		reservation_object_test_signaled_rcu(NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_TEST_SIGNALED_RCU, 1, [reservation_object_test_signaled_rcu() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
