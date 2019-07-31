dnl #
dnl #  commit v4.15-28-gf3804203306e
dnl #  array_index_nospec: Sanitize speculative array de-references
dnl #
AC_DEFUN([AC_AMDGPU_LINUX_NOSPEC],
	[AC_MSG_CHECKING([whether linux/nospec.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/nospec.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LINUX_NOSPEC_H, 1, [linux/nospec.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
