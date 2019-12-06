dnl #
dnl # commit v4.3-rc4-1-g2f8e2c877784
dnl # move io-64-nonatomic*.h out of asm-generic
dnl #
AC_DEFUN([AC_AMDGPU_LINUX_IO_64_NONATOMIC_LO_HI_H],
	[AC_MSG_CHECKING([whether linux/io-64-nonatomic-lo-hi.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/io-64-nonatomic-lo-hi.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LINUX_IO_64_NONATOMIC_LO_HI_H, 1, [linux/io-64-nonatomic-lo-hi.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])
