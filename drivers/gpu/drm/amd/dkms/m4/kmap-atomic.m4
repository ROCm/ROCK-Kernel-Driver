dnl #
dnl #commit 980c19e3f8ca1d5d43cce588059ea78cac27062a
dnl #Author: Cong Wang <amwang@redhat.com>
dnl # highmem: mark k[un]map_atomic() with two arguments as deprecated
dnl #
AC_DEFUN([AC_AMDGPU_KMAP_ATOMIC],[
		AC_MSG_CHECKING([whether kmap_atomic() have one argument])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/highmem.h>
		], [
				kmap_atomic(NULL);
		], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_KMAP_ATOMIC_ONE_ARG, 1, [kmap_atomic() have one argument])
		], [
				AC_MSG_RESULT(no)
		])
])
