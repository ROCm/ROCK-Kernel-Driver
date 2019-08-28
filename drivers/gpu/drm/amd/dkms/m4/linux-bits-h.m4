dnl #
dnl #	commit 8bd9cb51daac89337295b6f037b0486911e1b408
dnl # Author: Will Deacon <will.deacon@arm.com>
dnl # Date:   Tue Jun 19 13:53:08 2018 +0100
dnl # locking/atomics, asm-generic: Move some macros from <linux/bitops.h> to a new <linux/bits.h> file
dnl #
AC_DEFUN([AC_AMDGPU_LINUX_BITS],
	[AC_MSG_CHECKING([whether linux/bits.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/bits.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LINUX_BITS_H, 1, [whether linux/bits.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
