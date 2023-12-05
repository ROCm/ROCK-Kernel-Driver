dnl #
dnl # commit v5.12-rc6-9-g4f0f586bf0c8
dnl # treewide: Change list_sort to use const pointers
dnl #
AC_DEFUN([AC_AMDGPU_LIST_CMP_FUNC_IS_CONST_PARAM], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/list_sort.h>
		], [
			list_cmp_func_t cmp = NULL;
			struct list_head a, b;
			cmp(NULL, &a, &b);
		], [
			AC_DEFINE(HAVE_LIST_CMP_FUNC_IS_CONST_PARAM, 1,
				[list_cmp_func() is const param])
		])
	])
])