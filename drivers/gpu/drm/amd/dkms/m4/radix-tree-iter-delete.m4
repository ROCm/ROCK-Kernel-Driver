dnl #
dnl # v4.10-rc5-380-g0ac398ef391b
dnl # radix-tree: Add radix_tree_iter_delete
dnl #
AC_DEFUN([AC_AMDGPU_RADIX_TREE_ITER_DELETE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/radix-tree.h>
		], [
			radix_tree_iter_delete(NULL,NULL,NULL);
		], [
			AC_DEFINE(HAVE_RADIX_TREE_ITER_DELETE, 1,
				[radix_tree_iter_delete() is available])
		])
	])
])
