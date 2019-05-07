dnl #
dnl # commit 7faf952a3030d304334fe527be339b63e9e2745f
dnl # Author: Christian König <christian.koenig@amd.com>
dnl # Date:   Thu Aug 10 13:01:48 2017 -0400
dnl # dma-buf: add reservation_object_copy_fences (v2)
dnl #
AC_DEFUN([AC_AMDGPU_RESERVATION_OBJECT_COPY_FENCES],
	[AC_MSG_CHECKING([whether reservation_object_copy_fences() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/reservation.h>
	], [
		reservation_object_copy_fences(NULL, NULL);
	], [reservation_object_copy_fences], [drivers/dma-buf/reservation.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RESERVATION_OBJECT_COPY_FENCES, 1, [reservation_object_copy_fences() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
