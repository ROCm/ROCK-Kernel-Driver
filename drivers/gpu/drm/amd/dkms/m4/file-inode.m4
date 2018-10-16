dnl #
dnl # commit dd37978c50bc8b354e5c4633f69387f16572fdac
dnl # cache the value of file_inode() in struct file
dnl #
AC_DEFUN([AC_AMDGPU_FILE_INODE],
	[AC_MSG_CHECKING([whether file_inode() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		file_inode(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_INODE, 1, [file_inode() is available])
	],[
	AC_MSG_RESULT(no)
	])
])
