dnl #
dnl # commit: v3.19-rc5-12-ge59b4e9187bd
dnl # debugfs: Provide a file creation function
dnl # that also takes an initial size
AC_DEFUN([AC_AMDGPU_DEBUGFS_CREATE_FILE_SIZE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/debugfs.h>
		],[
			debugfs_create_file_size(NULL, 0, NULL, NULL, NULL, 0);
		],[debugfs_create_file_size], [fs/debugfs/inode.c], [
			AC_DEFINE(HAVE_DEBUGFS_CREATE_FILE_SIZE, 1,
				[debugfs_create_file_size() is available])
		])
	])
])
